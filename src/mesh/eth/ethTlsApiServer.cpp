#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_TLS_API) && defined(ARCH_RP2040)

#include "concurrency/OSThread.h"
#include "ethApiHandlers.h"
#include "ethCert.h"
#include "ethTlsApiServer.h"
#include <Arduino.h>

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <hardware/watchdog.h>
#include <pico/rand.h>

#ifndef ETH_TLS_API_PORT
#define ETH_TLS_API_PORT 443
#endif

// Adaptive poll intervals (mirror ethApiServer.cpp).
static constexpr uint32_t ACTIVE_THRESHOLD_MS = 5000;
static constexpr uint32_t MEDIUM_THRESHOLD_MS = 30000;
static constexpr int32_t ACTIVE_INTERVAL_MS = 20;
static constexpr int32_t MEDIUM_INTERVAL_MS = 100;
static constexpr int32_t IDLE_INTERVAL_MS = 500;
// Matches the keep-alive idle window in ethApiHandlers - if the handler
// loop calls read() and netRecv blocked for 10 s, the 3 s idle deadline
// inside parseRequest would be irrelevant and the OSThread would stay stuck
// long after a quiet browser closed its end of the TCP socket.
static constexpr uint32_t RECV_TIMEOUT_MS = 3000;

// Reuse the picoRand callback semantics from ethCert.cpp. Local copy so we
// don't have to expose it through a header; the cost is two trivial functions.
static int picoRand(void * /*ctx*/, unsigned char *out, size_t len)
{
    while (len > 0) {
        uint64_t r = get_rand_64();
        size_t to_copy = len > sizeof(r) ? sizeof(r) : len;
        memcpy(out, &r, to_copy);
        out += to_copy;
        len -= to_copy;
    }
    return 0;
}

// One-shot TLS context lives in BSS - keeps mbedtls allocations off the
// OSThread stack (lesson from Phase 2.1-bis: stack budget is tight on M33).
static EthernetServer *tlsServer = nullptr;
static mbedtls_x509_crt certChain;
static mbedtls_pk_context pkKey;
static mbedtls_ssl_config sslConf;
static mbedtls_ssl_context ssl;
static bool tlsReady = false;

// Adapter: route mbedtls_ssl_set_bio() through the EthernetClient instance
// that runOnce() is currently servicing. The void* ctx we hand mbedtls is a
// pointer to the EthernetClient.
static int netSend(void *ctx, const unsigned char *buf, size_t len)
{
    auto *client = static_cast<EthernetClient *>(ctx);
    if (!client->connected())
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;

    // Block-with-yield until the W5500 TX buffer can absorb the chunk.
    // Returning WANT_WRITE without delay made mbedtls_ssl_handshake() spin
    // at ~180k iter/s when Chrome was slow to drain the socket during the
    // ECDHE-ECDSA ServerKeyExchange - the original code logged exactly that
    // signature (ret=-0x6880 / WANT_WRITE) tight-looping forever. Firefox
    // happened to read fast enough that the buffer never filled.
    uint32_t t0 = millis();
    while (true) {
        if (!client->connected())
            return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
        size_t w = client->write(buf, len);
        if (w > 0)
            return (int)w;
        if (millis() - t0 > RECV_TIMEOUT_MS)
            return MBEDTLS_ERR_SSL_TIMEOUT;
        watchdog_update();
        delay(2);
    }
}

static int netRecv(void *ctx, unsigned char *buf, size_t len)
{
    auto *client = static_cast<EthernetClient *>(ctx);
    // Block-with-timeout: spin until bytes arrive, the peer closes, or we
    // exceed the per-recv budget. Pure non-blocking (return WANT_READ) would
    // require mbedtls_ssl_handshake to be driven from the runOnce dispatcher
    // - overkill for the Phase 2.2 skeleton with a single in-flight session.
    //
    // Pet the 8 s hardware watchdog from inside the poll loop. We sit here
    // for up to RECV_TIMEOUT_MS waiting for the next keep-alive request, and
    // a quiet client can string two such waits back-to-back (6 s) plus the
    // earlier handshake/handler time - easily past the watchdog deadline.
    // The main loop()'s watchdog_update() never runs while the OSThread is
    // inside serveClient(), so it has to be done here.
    uint32_t t0 = millis();
    while (client->available() == 0) {
        if (!client->connected())
            return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
        if (millis() - t0 > RECV_TIMEOUT_MS)
            return MBEDTLS_ERR_SSL_TIMEOUT;
        watchdog_update();
        delay(2);
    }
    int n = client->read(buf, len);
    if (n <= 0)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return n;
}

// Bridges mbedtls_ssl_read/write to the request handlers via the same
// IStreamReadWrite interface the plain-HTTP server uses.
class MbedTlsStream : public IStreamReadWrite
{
  public:
    MbedTlsStream(mbedtls_ssl_context *s, EthernetClient *c) : ssl_(s), client_(c) {}

    size_t write(uint8_t b) override
    {
        int r = mbedtls_ssl_write(ssl_, &b, 1);
        return r > 0 ? 1 : 0;
    }
    size_t write(const uint8_t *buf, size_t len) override
    {
        size_t total = 0;
        while (total < len) {
            int r = mbedtls_ssl_write(ssl_, buf + total, len - total);
            if (r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_READ)
                continue;
            if (r <= 0)
                break;
            total += (size_t)r;
        }
        return total;
    }

    int available() override
    {
        // mbedtls buffers internally; ssl_get_bytes_avail reports what is
        // already decoded. If 0, peek a record via read into a 1-byte buffer.
        size_t pending = mbedtls_ssl_get_bytes_avail(ssl_);
        if (pending > 0)
            return (int)pending;
        // Best-effort: report network bytes (rough proxy - handlers usually
        // call read() in a loop and tolerate slow streams).
        return client_->available();
    }
    int read() override
    {
        uint8_t b;
        int r = mbedtls_ssl_read(ssl_, &b, 1);
        return r == 1 ? (int)b : -1;
    }
    int read(uint8_t *buf, size_t len) override
    {
        int r = mbedtls_ssl_read(ssl_, buf, len);
        return r > 0 ? r : -1;
    }

    bool connected() override { return client_->connected(); }
    void flush() override { client_->flush(); }
    IPAddress remoteIP() override { return client_->remoteIP(); }

  private:
    mbedtls_ssl_context *ssl_;
    EthernetClient *client_;
};

class EthTlsApiServerThread : public concurrency::OSThread
{
  public:
    EthTlsApiServerThread() : concurrency::OSThread("EthTlsApi") { lastActivityMs = millis(); }

  protected:
    int32_t runOnce() override
    {
        // Phase A: wait for the cert worker, then rebuild if the cert was
        // regenerated (a DHCP lease change to a new IP bumps the generation, and
        // the SAN must follow or browsers reject the new address).
        if (tlsReady && getEthCertGeneration() != loadedCertGen_) {
            LOG_INFO("ETH TLS: cert regenerated (gen %u->%u), reloading + rebinding", (unsigned)loadedCertGen_,
                     (unsigned)getEthCertGeneration());
            deInitEthTlsApiServer(); // frees ctx, drops the listener, clears tlsReady
        }
        if (!tlsReady) {
            if (!isEthCertReady())
                return 500;
            if (!initTlsContext())
                return INT32_MAX; // hard fail - TLS server stays disabled
            loadedCertGen_ = getEthCertGeneration();
            tlsReady = true;
        }

        // Phase B: accept + serve one client.
        if (!tlsServer)
            return INT32_MAX;

        EthernetClient client = tlsServer->accept();
        if (client) {
            lastActivityMs = millis();
            serveClient(client);
            client.stop();
        }

        uint32_t since = millis() - lastActivityMs;
        if (since < ACTIVE_THRESHOLD_MS)
            return ACTIVE_INTERVAL_MS;
        if (since < MEDIUM_THRESHOLD_MS)
            return MEDIUM_INTERVAL_MS;
        return IDLE_INTERVAL_MS;
    }

  private:
    uint32_t lastActivityMs;
    uint32_t loadedCertGen_ = 0; // cert generation the current TLS context was built from

    bool initTlsContext()
    {
        const EthCertMaterial &cert = getEthCert();
        if (cert.certDer.empty() || cert.keyDer.empty()) {
            LOG_ERROR("ETH TLS: cert material is empty, refusing to start TLS server");
            return false;
        }

        mbedtls_x509_crt_init(&certChain);
        mbedtls_pk_init(&pkKey);
        mbedtls_ssl_config_init(&sslConf);
        mbedtls_ssl_init(&ssl);

        int ret;

        ret = mbedtls_x509_crt_parse_der(&certChain, cert.certDer.data(), cert.certDer.size());
        if (ret != 0) {
            LOG_ERROR("ETH TLS: x509_crt_parse_der failed -0x%04x", -ret);
            return false;
        }

        ret = mbedtls_pk_parse_key(&pkKey, cert.keyDer.data(), cert.keyDer.size(), nullptr, 0, picoRand, nullptr);
        if (ret != 0) {
            LOG_ERROR("ETH TLS: pk_parse_key failed -0x%04x", -ret);
            return false;
        }

        ret = mbedtls_ssl_config_defaults(&sslConf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) {
            LOG_ERROR("ETH TLS: ssl_config_defaults failed -0x%04x", -ret);
            return false;
        }

        mbedtls_ssl_conf_rng(&sslConf, picoRand, nullptr);
        mbedtls_ssl_conf_authmode(&sslConf, MBEDTLS_SSL_VERIFY_NONE);

        // TLS 1.3 is compiled out via mbedtls_user_config.h (it would crash
        // on Chrome's ClientHello extensions). These calls pin runtime to
        // 1.2 too as a defense-in-depth: if a future user config flip
        // re-enables 1.3 code, the config layer still won't negotiate it.
        mbedtls_ssl_conf_max_tls_version(&sslConf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_min_tls_version(&sslConf, MBEDTLS_SSL_VERSION_TLS1_2);

        ret = mbedtls_ssl_conf_own_cert(&sslConf, &certChain, &pkKey);
        if (ret != 0) {
            LOG_ERROR("ETH TLS: conf_own_cert failed -0x%04x", -ret);
            return false;
        }

        ret = mbedtls_ssl_setup(&ssl, &sslConf);
        if (ret != 0) {
            LOG_ERROR("ETH TLS: ssl_setup failed -0x%04x", -ret);
            return false;
        }

        tlsServer = new EthernetServer(ETH_TLS_API_PORT);
        tlsServer->begin();
        LOG_INFO("ETH TLS: server listening on TCP port %d", ETH_TLS_API_PORT);
        return true;
    }

    void serveClient(EthernetClient &client)
    {
        LOG_INFO("ETH TLS: client connected from %s", client.remoteIP().toString().c_str());

        mbedtls_ssl_session_reset(&ssl);
        mbedtls_ssl_set_bio(&ssl, &client, netSend, netRecv, nullptr);

        uint32_t t0 = millis();
        int ret;
        do {
            ret = mbedtls_ssl_handshake(&ssl);
            watchdog_update();
        } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret != 0) {
            char err[80];
            mbedtls_strerror(ret, err, sizeof(err));
            LOG_WARN("ETH TLS: handshake failed -0x%04x (%s) after %u ms", -ret, err, (unsigned)(millis() - t0));
            return;
        }
        LOG_INFO("ETH TLS: handshake OK in %u ms, ciphersuite=%s", (unsigned)(millis() - t0), mbedtls_ssl_get_ciphersuite(&ssl));

        MbedTlsStream stream(&ssl, &client);
        handleApiClient(stream);

        mbedtls_ssl_close_notify(&ssl);
    }
};

static EthTlsApiServerThread *tlsThread = nullptr;

void initEthTlsApiServer()
{
    if (tlsThread)
        return;
    tlsThread = new EthTlsApiServerThread();
    LOG_INFO("ETH TLS: server worker scheduled (waits for cert ready)");
}

void deInitEthTlsApiServer()
{
    // A W5500 chip reset leaves tlsServer bound to a dead socket and the cached
    // mbedTLS context stale. Reset the worker back to Phase A (free the context,
    // drop the listener, clear tlsReady) WITHOUT deleting the OSThread - its next
    // runOnce re-waits for isEthCertReady() and rebuilds the context + rebinds
    // TCP/443. Safe to free here: this runs in reconnectETH (ethConnect thread),
    // and the cooperative scheduler guarantees tlsThread is not mid-runOnce, so
    // nothing is using these contexts right now.
    if (tlsServer) {
        delete tlsServer;
        tlsServer = nullptr;
    }
    if (tlsReady) {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&sslConf);
        mbedtls_pk_free(&pkKey);
        mbedtls_x509_crt_free(&certChain);
        tlsReady = false;
    }
}

#endif // HAS_ETHERNET && HAS_ETHERNET_TLS_API && ARCH_RP2040
