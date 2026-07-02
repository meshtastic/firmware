#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_TLS_API) && defined(ARCH_RP2040)

#include "FSCommon.h"
#include "concurrency/OSThread.h"
#include "ethCert.h"
#include <Arduino.h>
#include <string.h>

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

#include <mbedtls/asn1.h>
#include <mbedtls/ecp.h>
#include <mbedtls/error.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include <pico/rand.h>

// v2: cert layout now includes KeyUsage + ExtendedKeyUsage(serverAuth).
// Bumping the file names invalidates v1 caches (without EKU NSS / Firefox
// rejects the cert with a non-overridable "Secure Connection Failed").
static constexpr const char *CERT_PATH = "/eth_cert_v2.der";
static constexpr const char *KEY_PATH = "/eth_key_v2.der";
static constexpr const char *IP_PATH = "/eth_cert_ip_v2.txt";

// Random callback for mbedtls - sources entropy from the RP2350 ROSC TRNG via
// pico-sdk get_rand_64(). Used directly as f_rng in mbedtls calls so we don't
// have to plumb a full mbedtls_entropy_context + ctr_drbg. The hardware TRNG
// is cryptographically suitable per pico-sdk docs (ROSC + whitening).
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

static bool readBinary(const char *path, std::vector<uint8_t> &out)
{
    File f = FSCom.open(path, FILE_O_READ);
    if (!f)
        return false;
    size_t sz = f.size();
    out.clear();
    out.reserve(sz);
    while (f.available())
        out.push_back((uint8_t)f.read());
    f.close();
    return out.size() == sz && sz > 0;
}

static bool writeBinary(const char *path, const uint8_t *buf, size_t len)
{
    File f = FSCom.open(path, FILE_O_WRITE);
    if (!f)
        return false;
    size_t w = f.write(buf, len);
    f.flush();
    f.close();
    return w == len;
}

static bool readText(const char *path, String &out)
{
    File f = FSCom.open(path, FILE_O_READ);
    if (!f)
        return false;
    out = "";
    while (f.available())
        out += (char)f.read();
    f.close();
    return out.length() > 0;
}

static bool writeText(const char *path, const String &s)
{
    File f = FSCom.open(path, FILE_O_WRITE);
    if (!f)
        return false;
    size_t w = f.write((const uint8_t *)s.c_str(), s.length());
    f.flush();
    f.close();
    return w == s.length();
}

// Generate ECDSA P-256 key + self-signed X.509 cert with CN=<ip>,
// SAN(IP=<ip>), validity 2024-01-01 to 2034-01-01. Fills out on success.
static bool generateCert(IPAddress ip, EthCertMaterial &out)
{
    int ret;
    mbedtls_pk_context pk;
    mbedtls_x509write_cert crt;

    mbedtls_pk_init(&pk);
    mbedtls_x509write_crt_init(&crt);

    bool ok = false;
    // Buffers live on the heap so the OSThread stack stays small. Originally
    // certBuf[2048] + keyBuf[1024] were locals; combined with mbedtls ECDSA's
    // own deep call stack that pushed past the ~8 KB core0 stack budget on
    // arduino-pico and the board reboot-looped between "starting cert
    // pipeline" and the first "generated…" log.
    std::vector<unsigned char> certBuf(2048);
    std::vector<unsigned char> keyBuf(1024);

    do {
        // 1. ECDSA P-256 keypair
        ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
        if (ret != 0) {
            LOG_ERROR("ETH CERT: pk_setup failed -0x%04x", -ret);
            break;
        }
        ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), picoRand, nullptr);
        if (ret != 0) {
            LOG_ERROR("ETH CERT: ecp_gen_key failed -0x%04x", -ret);
            break;
        }

        // 2. Cert fields
        String subjectName = "CN=" + ip.toString() + ",O=Meshtastic,C=US";
        ret = mbedtls_x509write_crt_set_subject_name(&crt, subjectName.c_str());
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_subject_name failed -0x%04x", -ret);
            break;
        }
        ret = mbedtls_x509write_crt_set_issuer_name(&crt, subjectName.c_str());
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_issuer_name failed -0x%04x", -ret);
            break;
        }

        mbedtls_x509write_crt_set_subject_key(&crt, &pk);
        mbedtls_x509write_crt_set_issuer_key(&crt, &pk);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);

        unsigned char serial[16];
        picoRand(nullptr, serial, sizeof(serial));
        ret = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_serial_raw failed -0x%04x", -ret);
            break;
        }

        ret = mbedtls_x509write_crt_set_validity(&crt, "20240101000000", "20340101000000");
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_validity failed -0x%04x", -ret);
            break;
        }

        ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_basic_constraints failed -0x%04x", -ret);
            break;
        }

        // KeyUsage: digitalSignature lets the cert sign TLS handshake
        // messages (ECDHE-ECDSA key exchange). keyEncipherment is required
        // by NSS/Firefox even though TLS 1.2 ECDHE doesn't actually use it.
        ret = mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_key_usage failed -0x%04x", -ret);
            break;
        }

        // ExtendedKeyUsage: serverAuth. NSS / Firefox refuse to treat a cert
        // as a TLS server cert without this extension since 2023 - the error
        // surfaces as a non-overridable "Secure Connection Failed" with no
        // "Accept the Risk" path.
        mbedtls_asn1_sequence ekuSeq;
        ekuSeq.buf.tag = MBEDTLS_ASN1_OID;
        ekuSeq.buf.p = (unsigned char *)MBEDTLS_OID_SERVER_AUTH;
        ekuSeq.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
        ekuSeq.next = nullptr;
        ret = mbedtls_x509write_crt_set_ext_key_usage(&crt, &ekuSeq);
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_ext_key_usage failed -0x%04x", -ret);
            break;
        }

        // SAN extension: IP address (4 bytes). Browsers require SAN match,
        // CN alone is ignored since RFC 6125 / Chrome 58.
        unsigned char ipBytes[4] = {ip[0], ip[1], ip[2], ip[3]};
        mbedtls_x509_san_list san;
        san.next = nullptr;
        san.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
        san.node.san.unstructured_name.tag = 0;
        san.node.san.unstructured_name.p = ipBytes;
        san.node.san.unstructured_name.len = sizeof(ipBytes);
        ret = mbedtls_x509write_crt_set_subject_alternative_name(&crt, &san);
        if (ret != 0) {
            LOG_ERROR("ETH CERT: set_san failed -0x%04x", -ret);
            break;
        }

        // 3. Serialize cert + key to DER. mbedtls writes DER at the END of
        // the buffer; ret = length, with the bytes living at (buf + size - len).
        ret = mbedtls_x509write_crt_der(&crt, certBuf.data(), certBuf.size(), picoRand, nullptr);
        if (ret < 0) {
            LOG_ERROR("ETH CERT: x509write_crt_der failed -0x%04x", -ret);
            break;
        }
        size_t certLen = (size_t)ret;
        out.certDer.assign(certBuf.data() + certBuf.size() - certLen, certBuf.data() + certBuf.size());

        ret = mbedtls_pk_write_key_der(&pk, keyBuf.data(), keyBuf.size());
        if (ret < 0) {
            LOG_ERROR("ETH CERT: pk_write_key_der failed -0x%04x", -ret);
            break;
        }
        size_t keyLen = (size_t)ret;
        out.keyDer.assign(keyBuf.data() + keyBuf.size() - keyLen, keyBuf.data() + keyBuf.size());

        ok = true;
    } while (false);

    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&pk);
    return ok;
}

// Sanity-check a cached cert/key pair before trusting it. A reset between the
// three persist writes in ensureCertForIp() can leave a truncated DER on the FS;
// parsing both here catches that so we regenerate instead of handing the TLS
// server a bad pair (which then hard-disables HTTPS until the files change).
static bool certKeyParse(const std::vector<uint8_t> &certDer, const std::vector<uint8_t> &keyDer)
{
    if (certDer.empty() || keyDer.empty())
        return false;
    mbedtls_x509_crt crt;
    mbedtls_pk_context pk;
    mbedtls_x509_crt_init(&crt);
    mbedtls_pk_init(&pk);
    // Both DERs must parse AND form a matching pair: parsing alone would accept a
    // truncation-free but mismatched cert/key (e.g. a new cert written over an old
    // key when the IP commit-marker survived a failed clear), which TLS would then
    // reject at handshake time.
    bool ok = mbedtls_x509_crt_parse_der(&crt, certDer.data(), certDer.size()) == 0 &&
              mbedtls_pk_parse_key(&pk, keyDer.data(), keyDer.size(), nullptr, 0, picoRand, nullptr) == 0 &&
              mbedtls_pk_check_pair(&crt.pk, &pk, picoRand, nullptr) == 0;
    mbedtls_pk_free(&pk);
    mbedtls_x509_crt_free(&crt);
    return ok;
}

bool ensureCertForIp(IPAddress ip, EthCertMaterial &out)
{
    String ipStr = ip.toString();

    // Try cache. The IP file is a commit-marker written last (and cleared first)
    // by the persist step below, so a present matching marker means cert+key were
    // both fully written; certKeyParse() is belt-and-suspenders against truncation.
    String savedIp;
    if (readText(IP_PATH, savedIp)) {
        savedIp.trim();
        if (savedIp == ipStr && readBinary(CERT_PATH, out.certDer) && readBinary(KEY_PATH, out.keyDer)) {
            if (certKeyParse(out.certDer, out.keyDer)) {
                LOG_INFO("ETH CERT: loaded from FS (%u B cert + %u B key, IP %s)", (unsigned)out.certDer.size(),
                         (unsigned)out.keyDer.size(), ipStr.c_str());
                return true;
            }
            LOG_WARN("ETH CERT: cached cert/key failed to parse (partial write?), regenerating");
            out.certDer.clear();
            out.keyDer.clear();
        }
        if (savedIp != ipStr) {
            LOG_INFO("ETH CERT: cached IP %s != current %s, regenerating", savedIp.c_str(), ipStr.c_str());
        }
    }

    LOG_INFO("ETH CERT: generating ECDSA P-256 self-signed cert for IP %s...", ipStr.c_str());
    uint32_t t0 = millis();
    if (!generateCert(ip, out)) {
        LOG_ERROR("ETH CERT: generation failed");
        return false;
    }
    uint32_t dt = millis() - t0;
    LOG_INFO("ETH CERT: generated %u B cert + %u B key in %u ms", (unsigned)out.certDer.size(), (unsigned)out.keyDer.size(),
             (unsigned)dt);

    // Persist (best-effort). Failure here means we'll regen on next boot, but the
    // current process still has the in-memory cert ready for use.
    //
    // Clear the IP commit-marker FIRST so a reset mid-write can't leave the marker
    // pointing at a half-written (or stale-paired) cert/key - the load path only
    // trusts the cache when the marker matches. Writing the marker LAST commits the
    // new pair atomically w.r.t. the loader.
    writeText(IP_PATH, "");
    if (!writeBinary(CERT_PATH, out.certDer.data(), out.certDer.size()) ||
        !writeBinary(KEY_PATH, out.keyDer.data(), out.keyDer.size()) || !writeText(IP_PATH, ipStr)) {
        LOG_WARN("ETH CERT: persist failed - will regenerate next boot");
    } else {
        LOG_INFO("ETH CERT: persisted to LittleFS");
    }

    return true;
}

// Worker that defers cert gen off the Periodic thread (which has a tight stack
// and ticks every 5s alongside reconnect / NTP / MQTT). Waits for a non-zero IP,
// generates/loads the cert for it, then keeps polling at CERT_RECHECK_MS so a
// DHCP lease change to a new IP regenerates the cert - the SAN must track the
// current address or browsers reject the new one. The steady-state poll is just
// localIP() + compare; ECDSA keygen only reruns when the IP actually changes,
// and it runs on this thread's own stack (not the Periodic's).
//
// Phase 2.1-bis. The previous attempt called ensureCertForIp() inline from
// reconnectETH() and reboot-looped the board: probable stack overflow during
// ECDSA keygen + DER encoding + LittleFS write all on the Periodic stack.
class EthCertThread : public concurrency::OSThread
{
  public:
    EthCertThread() : concurrency::OSThread("EthCert") {}

  protected:
    int32_t runOnce() override
    {
        IPAddress ip = Ethernet.localIP();
        if (ip == IPAddress(0, 0, 0, 0))
            return 250; // DHCP not yet bound; check again in 250 ms

        if (ready_ && ip == certIp_)
            return CERT_RECHECK_MS; // cert already matches the current IP

        // First cert, or the lease moved us to a new IP. ensureCertForIp()
        // regenerates whenever its saved IP != ip, so the cert SAN follows.
        bool ok = ensureCertForIp(ip, material_);
        if (!ok) {
            LOG_ERROR("ETH CERT: pipeline FAILED - TLS server will not start");
            // Don't leave isReady() reporting true with empty material: a later TLS
            // teardown (e.g. a W5500 reset) would then fail initTlsContext() and stay
            // disabled. Clear readiness so the TLS worker waits and the next poll
            // retries from scratch.
            ready_ = false;
            certIp_ = IPAddress(0, 0, 0, 0);
            material_.certDer.clear();
            material_.keyDer.clear();
            return CERT_RECHECK_MS; // retry on the next poll
        }
        certIp_ = ip;
        ready_ = true;
        certGeneration_++; // bump so the TLS worker reloads + rebinds with the new cert
        return CERT_RECHECK_MS;
    }

  private:
    static constexpr uint32_t CERT_RECHECK_MS = 30000; // re-poll IP to catch DHCP lease changes
    bool ready_ = false;
    IPAddress certIp_ = IPAddress(0, 0, 0, 0);
    uint32_t certGeneration_ = 0;
    EthCertMaterial material_;

  public:
    bool isReady() const { return ready_; }
    uint32_t generation() const { return certGeneration_; }
    const EthCertMaterial &cert() const { return material_; }
};

static EthCertThread *certThread = nullptr;
static EthCertMaterial emptyMaterial;

void initEthCertThread()
{
    if (certThread)
        return;
    certThread = new EthCertThread();
    LOG_INFO("ETH CERT: deferred worker scheduled (waits for DHCP, runs once)");
}

bool isEthCertReady()
{
    return certThread && certThread->isReady();
}

const EthCertMaterial &getEthCert()
{
    return (certThread && certThread->isReady()) ? certThread->cert() : emptyMaterial;
}

uint32_t getEthCertGeneration()
{
    return certThread ? certThread->generation() : 0;
}

#endif // HAS_ETHERNET && HAS_ETHERNET_TLS_API && ARCH_RP2040
