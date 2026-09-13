#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "NodeDB.h"
#include "UptimeClock.h"
#include "graphics/Screen.h"
#include "main.h"
#include "mesh/http/WebServer.h"
#include "mesh/wifi/WiFiAPClient.h"
#include "sleep.h"
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>
#include <Throttle.h>
#include <WebServer.h>
#include <WiFi.h>

#if HAS_ETHERNET && defined(ARCH_ESP32)
#include <ETH.h>
#endif // HAS_ETHERNET

#ifdef ARCH_ESP32
#include "esp_task_wdt.h"
#include <esp_heap_caps.h>
#include <mbedtls/ssl.h>
#endif

// Persistent Data Storage
#include <Preferences.h>
Preferences prefs;

/*
  Including the esp32_https_server library will trigger a compile time error. I've
  tracked it down to a reoccurrance of this bug:
    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=57824
  The work around is described here:
    https://forums.xilinx.com/t5/Embedded-Development-Tools/Error-with-Standard-Libaries-in-Zynq/td-p/450032

  Long story short is we need "#undef str" before including the esp32_https_server.
    - Jm Casler (jm@casler.org) Oct 2020
*/
#undef str

// Includes for the https server
//   https://github.com/fhessel/esp32_https_server
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <HTTPSServer.hpp>
#include <HTTPServer.hpp>
#include <SSLCert.hpp>

// The HTTPS Server comes in a separate namespace. For easier use, include it here.
using namespace httpsserver;
#include "mesh/http/ContentHandler.h"

static const uint32_t ACTIVE_THRESHOLD_MS = 5000;
static const uint32_t MEDIUM_THRESHOLD_MS = 30000;
static const int32_t ACTIVE_INTERVAL_MS = 50;
static const int32_t MEDIUM_INTERVAL_MS = 200;
static const int32_t IDLE_INTERVAL_MS = 1000;

// Maximum concurrent HTTPS connections (reduced from default 4 to save memory)
static const uint8_t MAX_HTTPS_CONNECTIONS = 2;

// What a new TLS session has to be able to allocate before it can do anything.
//
// mbedtls_ssl_setup() makes two independent calloc() calls - one inbound record buffer, one
// outbound - and each has to come back as a single contiguous block:
//   MBEDTLS_SSL_{IN,OUT}_CONTENT_LEN + MBEDTLS_SSL_HEADER_LEN + MBEDTLS_SSL_PAYLOAD_OVERHEAD.
// The two overhead terms are private to the library's ssl_misc.h, so round them up here instead of
// reaching into it; over-stating them only makes the gate below slightly stricter.
static const size_t TLS_RECORD_OVERHEAD = 512;
static const size_t TLS_IN_BUFFER_BYTES = MBEDTLS_SSL_IN_CONTENT_LEN + TLS_RECORD_OVERHEAD;
static const size_t TLS_OUT_BUFFER_BYTES = MBEDTLS_SSL_OUT_CONTENT_LEN + TLS_RECORD_OVERHEAD;

// The rest of a handshake - handshake params, the transform, the peer certificate - is many small
// blocks rather than one big one, so a free-heap sum is the right instrument for that part.
static const uint32_t TLS_HANDSHAKE_SLACK = 8192;

// mbedTLS allocates through esp_mbedtls_mem_calloc(), which is pinned to one heap by
// CONFIG_MBEDTLS_{INTERNAL,EXTERNAL}_MEM_ALLOC. Probe the heap it will actually ask, or the answer
// is about memory mbedTLS is not allowed to use.
#if defined(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC)
#define TLS_PROBE_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#elif defined(CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC)
#define TLS_PROBE_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define TLS_PROBE_CAPS (MALLOC_CAP_8BIT)
#endif

/// Can the heap still hand out the contiguous record buffers a new TLS session needs?
///
/// A free-heap total is a sum over every free block and says nothing about the largest one, so on a
/// fragmented heap it waves the connection through: the accept succeeds, mbedtls_ssl_setup() then
/// fails with MBEDTLS_ERR_SSL_ALLOC_FAILED, and the client gets a TCP reset instead of waiting in
/// the listen backlog (#6960). Ask the allocator the same question mbedTLS is about to ask it.
/// Deliberately not heap_caps_get_largest_free_block(): that walks every block while holding the
/// allocator lock and tripped the interrupt watchdog on PSRAM boards (#11666).
///
/// The answer is advisory, not a guarantee. loop() services the open connections before it accepts,
/// so the heap can still turn between this question and mbedtls_ssl_setup(); a handshake that loses
/// that race fails exactly as it does today, with a reset. This makes it rare rather than routine.
static bool canAllocateTlsSession()
{
    if (ESP.getFreeHeap() < TLS_IN_BUFFER_BYTES + TLS_OUT_BUFFER_BYTES + TLS_HANDSHAKE_SLACK)
        return false;

    // Hold both at once, exactly as mbedtls_ssl_setup() does - one can well fit where two do not.
    void *in = heap_caps_malloc(TLS_IN_BUFFER_BYTES, TLS_PROBE_CAPS);
    void *out = in ? heap_caps_malloc(TLS_OUT_BUFFER_BYTES, TLS_PROBE_CAPS) : nullptr;
    const bool fits = in && out;
    // Observable accesses, so LTO cannot judge the pair dead and delete the question.
    if (in)
        *static_cast<volatile char *>(in) = 0;
    if (out)
        *static_cast<volatile char *>(out) = 0;
    heap_caps_free(out);
    heap_caps_free(in);
    return fits;
}

// HTTPSServer that can service and reap the connections it already holds without accepting new ones,
// so a low-heap pause doesn't freeze open TLS sessions (and their heap) in place. Needs the protected table.
class MeshHTTPSServer : public HTTPSServer
{
  public:
    using HTTPSServer::HTTPSServer;

    /// Give back the slots of connections that have closed. Costs nothing and touches no socket.
    ///
    /// Has to run before the heap is judged: HTTPServer::loop() reaps and accepts in a single pass,
    /// so a connection it frees during that pass opens a slot after any measurement we made and
    /// gets accepted unmeasured. Reaping here first means loop()'s own reap pass can no longer turn
    /// up a slot we did not already know about, and hands back the ~21 kB a finished session held.
    void reapClosedConnections()
    {
        if (!_running)
            return;
        for (uint8_t i = 0; i < _maxConnections; i++) {
            if (_connections[i] && _connections[i]->isClosed()) {
                delete _connections[i];
                _connections[i] = nullptr;
            }
        }
    }

    /// The first half of HTTPServer::loop(): drive and reap existing connections, accept nothing.
    void serviceExistingConnections()
    {
        if (!_running)
            return;
        reapClosedConnections();
        for (uint8_t i = 0; i < _maxConnections; i++) {
            if (_connections[i])
                _connections[i]->loop();
        }
    }

    /// Whether loop() could accept this time round, i.e. whether the heap question is worth asking.
    ///
    /// Deliberately only asks about slots, not about whether a client is waiting right now. loop()
    /// runs its own select() *after* it has synchronously driven every open connection, and driving
    /// one means running a handler that writes a whole response body over a blocking socket - easily
    /// milliseconds. A client that arrives during that window is invisible to any select() we could
    /// do beforehand and would be accepted with no heap behind it. The slot scan has no such window:
    /// loop() picks its free slot from the same state we just read.
    bool hasFreeConnectionSlot()
    {
        if (!_running)
            return false;
        for (uint8_t i = 0; i < _maxConnections; i++) {
            if (!_connections[i])
                return true;
        }
        return false;
    }
};

static SSLCert *cert;
static MeshHTTPSServer *secureServer;
static HTTPServer *insecureServer;

volatile bool isWebServerReady;
volatile bool isCertReady;

static void handleWebResponse()
{
    if (isWifiAvailable()) {

        if (isWebServerReady) {
            // Check heap before HTTPS processing - SSL requires significant memory
            if (secureServer) {
                // Reap first: it settles the slot picture judged below, and hands back the ~21 kB a
                // finished session was holding so the probe measures the heap we will really have.
                secureServer->reapClosedConnections();
                // Only pay for the probe when the answer can change something: with every slot
                // busy, loop() cannot accept anyway and is just the servicing half.
                if (!secureServer->hasFreeConnectionSlot() || canAllocateTlsSession()) {
                    secureServer->loop();
                } else {
                    // Low heap: accept nothing new, but keep servicing open connections so they can time out
                    // and free their contexts - skipping them pins the heap below the threshold for good.
                    secureServer->serviceExistingConnections();
                    static uint32_t lastHeapWarning = 0;
                    if (lastHeapWarning == 0 || !Throttle::isWithinTimespanMs(lastHeapWarning, 30000)) {
                        LOG_WARN("No contiguous heap for a TLS session (%u free), not accepting HTTPS connections",
                                 ESP.getFreeHeap());
                        lastHeapWarning = millis();
                    }
                }
            }
            insecureServer->loop();
        }
    }
}

static void taskCreateCert(void *parameter)
{
    prefs.begin("MeshtasticHTTPS", false);

    LOG_INFO("Checking if we have a saved SSL Certificate");

    size_t pkLen = prefs.getBytesLength("PK");
    size_t certLen = prefs.getBytesLength("cert");

    if (pkLen && certLen) {
        LOG_INFO("Existing SSL Certificate found");

        uint8_t *pkBuffer = new uint8_t[pkLen];
        prefs.getBytes("PK", pkBuffer, pkLen);

        uint8_t *certBuffer = new uint8_t[certLen];
        prefs.getBytes("cert", certBuffer, certLen);

        cert = new SSLCert(certBuffer, certLen, pkBuffer, pkLen);

        LOG_DEBUG("Retrieved Private Key: %d Bytes", cert->getPKLength());
        LOG_DEBUG("Retrieved Certificate: %d Bytes", cert->getCertLength());
    } else {

        LOG_INFO("Creating the certificate. This may take a while. Please wait");
        yield();
        cert = new SSLCert();
        yield();
        int createCertResult = createSelfSignedCert(*cert, KEYSIZE_2048, "CN=meshtastic.local,O=Meshtastic,C=US",
                                                    "20190101000000", "20300101000000");
        yield();

        if (createCertResult != 0) {
            LOG_ERROR("Creating the certificate failed");
        } else {
            LOG_INFO("Creating the certificate was successful");

            LOG_DEBUG("Created Private Key: %d Bytes", cert->getPKLength());

            LOG_DEBUG("Created Certificate: %d Bytes", cert->getCertLength());

            prefs.putBytes("PK", (uint8_t *)cert->getPKData(), cert->getPKLength());
            prefs.putBytes("cert", (uint8_t *)cert->getCertData(), cert->getCertLength());
        }
    }

    isCertReady = true;

    // Must delete self, can't just fall out
    vTaskDelete(NULL);
}

void createSSLCert()
{
    if (isWifiAvailable() && !isCertReady) {
        bool runLoop = false;

        // Create a new process just to handle creating the cert.
        //   This is a workaround for Bug: https://github.com/fhessel/esp32_https_server/issues/48
        //  jm@casler.org (Oct 2020)
        xTaskCreate(taskCreateCert, /* Task function. */
                    "createCert",   /* String with name of task. */
                    // 16384,          /* Stack size in bytes. */
                    8192,  /* Stack size in bytes. */
                    NULL,  /* Parameter passed as input of the task */
                    16,    /* Priority of the task. */
                    NULL); /* Task handle. */

        LOG_DEBUG("Waiting for SSL Cert to be generated");
        while (!isCertReady) {
            if ((millis() / 500) % 2) {
                if (runLoop) {
                    LOG_DEBUG(".");

                    yield();
                    esp_task_wdt_reset();
#if HAS_SCREEN
                    if (millis() / 1000 >= 3) {
                        if (screen)
                            screen->setSSLFrames();
                    }
#endif
                }
                runLoop = false;
            } else {
                runLoop = true;
            }
        }
        LOG_INFO("SSL Cert Ready");
    }
}

WebServerThread *webServerThread;

WebServerThread::WebServerThread() : concurrency::OSThread("WebServer")
{
    if (!config.network.wifi_enabled && !config.network.eth_enabled) {
        disable();
    }
    lastActivityTime = Time::getMillis();
}

void WebServerThread::markActivity()
{
    lastActivityTime = Time::getMillis();
}

int32_t WebServerThread::getAdaptiveInterval()
{
    if (Throttle::isWithinTimespanMs(lastActivityTime, ACTIVE_THRESHOLD_MS)) {
        return ACTIVE_INTERVAL_MS;
    } else if (Throttle::isWithinTimespanMs(lastActivityTime, MEDIUM_THRESHOLD_MS)) {
        return MEDIUM_INTERVAL_MS;
    } else {
        return IDLE_INTERVAL_MS;
    }
}

int32_t WebServerThread::runOnce()
{
    if (!config.network.wifi_enabled && !config.network.eth_enabled) {
        disable();
    }

    handleWebResponse();

    if (requestRestart && (millis() / 1000) > requestRestart) {
        ESP.restart();
    }

    return getAdaptiveInterval();
}

void initWebServer()
{
    LOG_DEBUG("Init Web Server");

    // We can now use the new certificate to setup our server as usual.
    secureServer = new MeshHTTPSServer(cert, 443, MAX_HTTPS_CONNECTIONS);
    insecureServer = new HTTPServer();

    registerHandlers(insecureServer, secureServer);

    if (secureServer) {
        LOG_INFO("Start Secure Web Server");
        secureServer->start();
    }
    LOG_INFO("Start Insecure Web Server");
    insecureServer->start();
    if (insecureServer->isRunning()) {
        LOG_INFO("Web Servers Ready! :-) ");
        isWebServerReady = true;
    } else {
        LOG_ERROR("Web Servers Failed! ;-( ");
    }
}
#endif
