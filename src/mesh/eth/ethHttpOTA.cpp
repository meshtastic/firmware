#include "configuration.h"

// ETHERNET_OTA needs the unified ethApiHandlers routing now that the OTA
// handlers run through IStreamReadWrite + handleApiClient. If a future
// build wants OTA without the rest of the HTTP API (unlikely), the handlers
// would have to be invoked directly.
#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA) && defined(HAS_ETHERNET_API)

#include "ethApiHandlers.h"
#include "ethHttpOTA.h"
#include "ethStreamAdapter.h"
#include "otaShared.h"
#include "NodeDB.h"
#include <Arduino.h>
#include <ErriezCRC32.h>
#include <Updater.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARCH_RP2040
#include <hardware/watchdog.h>
#define FEED_WATCHDOG() watchdog_update()
#else
#define FEED_WATCHDOG() ((void)0)
#endif

static constexpr uint32_t NONCE_TTL_MS = 30000;
static constexpr uint32_t BODY_TIMEOUT_MS = 30000;
static constexpr size_t CHUNK_SIZE = 1024;

static EthernetServer *httpServer = nullptr;

static uint8_t s_nonce[OTAShared::NONCE_SIZE];
static uint32_t s_nonceIssuedMs = 0;
static bool s_nonceValid = false;

static bool hexNibble(char c, uint8_t &out)
{
    if (c >= '0' && c <= '9') {
        out = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        out = c - 'a' + 10;
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        out = c - 'A' + 10;
        return true;
    }
    return false;
}

static bool hexToBytes(const char *hex, size_t hexLen, uint8_t *out, size_t expectedLen)
{
    if (hexLen != expectedLen * 2)
        return false;
    for (size_t i = 0; i < expectedLen; i++) {
        uint8_t hi, lo;
        if (!hexNibble(hex[i * 2], hi) || !hexNibble(hex[i * 2 + 1], lo))
            return false;
        out[i] = (hi << 4) | lo;
    }
    return true;
}

static void writeStatus(IStreamReadWrite &client, int code, const char *codeName)
{
    client.print("HTTP/1.1 ");
    client.print(code);
    client.print(" ");
    client.print(codeName);
    client.print("\r\n");
}

static void writeCORS(IStreamReadWrite &client, const char *methods)
{
    client.print("Access-Control-Allow-Origin: *\r\n");
    client.print("Access-Control-Allow-Methods: ");
    client.print(methods);
    client.print("\r\n");
    client.print("Access-Control-Allow-Headers: Content-Type, X-OTA-Nonce, X-OTA-Auth, X-OTA-Size, X-OTA-CRC32\r\n");
    client.print("Access-Control-Allow-Private-Network: true\r\n");
}

static void sendJSONError(IStreamReadWrite &client, int code, const char *codeName, const char *err)
{
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", err);
    writeStatus(client, code, codeName);
    writeCORS(client, "GET, PUT, OPTIONS");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(n);
    client.print("\r\nConnection: close\r\n\r\n");
    client.write((const uint8_t *)body, (size_t)n);
}

void handleOtaOptions(IStreamReadWrite &client, const char *methods)
{
    writeStatus(client, 204, "No Content");
    writeCORS(client, methods);
    client.print("Content-Length: 0\r\n");
    client.print("Connection: close\r\n\r\n");
}

// Minimal JSON string escaping for owner names (quotes / backslash / control chars).
static void jsonEscapeInfo(char *dst, size_t dstsize, const char *src)
{
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < dstsize; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c >= 0x20) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

void handleOtaInfo(IStreamReadWrite &client)
{
    // owner.* and config.* come from NodeDB; clientarea reads these for the node card
    // (long/short name + role) and falls back to the bare IP when they are absent.
    char longName[2 * sizeof(owner.long_name)];
    char shortName[2 * sizeof(owner.short_name)];
    jsonEscapeInfo(longName, sizeof(longName), owner.long_name);
    jsonEscapeInfo(shortName, sizeof(shortName), owner.short_name);

    char body[384];
    int n = snprintf(body, sizeof(body),
                     "{\"pio_env\":\"%s\",\"firmware_version\":\"%s\",\"hw_model\":%d,"
                     "\"owner_long_name\":\"%s\",\"owner_short_name\":\"%s\",\"role\":%d,"
                     "\"uptime_s\":%lu}",
                     optstr(APP_ENV), optstr(APP_VERSION), (int)HW_VENDOR, longName, shortName,
                     (int)config.device.role, (unsigned long)(millis() / 1000));
    if (n < 0)
        n = 0;
    else if (n >= (int)sizeof(body))
        n = (int)sizeof(body) - 1; // truncated; Content-Length must match bytes actually sent

    writeStatus(client, 200, "OK");
    writeCORS(client, "GET, OPTIONS");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(n);
    client.print("\r\nConnection: close\r\n\r\n");
    client.write((const uint8_t *)body, (size_t)n);
}

void handleOtaNonce(IStreamReadWrite &client)
{
    if (OTAShared::authCooldownActive()) {
        LOG_WARN("ETH HTTP OTA: Nonce request rejected (cooldown)");
        sendJSONError(client, 429, "Too Many Requests", "cooldown");
        return;
    }

    OTAShared::genNonce(s_nonce);
    s_nonceIssuedMs = millis();
    s_nonceValid = true;

    writeStatus(client, 200, "OK");
    writeCORS(client, "GET, OPTIONS");
    client.print("Content-Type: application/octet-stream\r\n");
    client.print("Cache-Control: no-store\r\n");
    client.print("Content-Length: ");
    client.print((int)OTAShared::NONCE_SIZE);
    client.print("\r\nConnection: close\r\n\r\n");
    client.write(s_nonce, OTAShared::NONCE_SIZE);
    LOG_INFO("ETH HTTP OTA: Issued nonce");
}

void handleOtaUpload(IStreamReadWrite &client, const Request &req)
{
    uint8_t reqNonce[OTAShared::NONCE_SIZE];
    uint8_t reqAuth[OTAShared::HASH_SIZE];

    if (!hexToBytes(req.xOtaNonce.c_str(), req.xOtaNonce.length(), reqNonce, OTAShared::NONCE_SIZE) ||
        !hexToBytes(req.xOtaAuth.c_str(), req.xOtaAuth.length(), reqAuth, OTAShared::HASH_SIZE)) {
        LOG_WARN("ETH HTTP OTA: Bad nonce/auth header format");
        sendJSONError(client, 400, "Bad Request", "bad_headers");
        return;
    }

    if (req.xOtaSize <= 0 || (size_t)req.xOtaSize > OTAShared::MAX_FW_SIZE) {
        sendJSONError(client, 400, "Bad Request", "size");
        return;
    }
    size_t declaredSize = (size_t)req.xOtaSize;

    if (req.xOtaCrc32.length() == 0) {
        sendJSONError(client, 400, "Bad Request", "crc_header");
        return;
    }
    char *end = nullptr;
    uint32_t declaredCrc = (uint32_t)strtoul(req.xOtaCrc32.c_str(), &end, 16);
    if (end == req.xOtaCrc32.c_str() || *end != '\0') {
        sendJSONError(client, 400, "Bad Request", "crc_header");
        return;
    }

    if (req.contentLength != (long)declaredSize) {
        sendJSONError(client, 400, "Bad Request", "length_mismatch");
        return;
    }

    if (!s_nonceValid || (millis() - s_nonceIssuedMs) > NONCE_TTL_MS ||
        !OTAShared::constTimeEq(reqNonce, s_nonce, OTAShared::NONCE_SIZE)) {
        LOG_WARN("ETH HTTP OTA: Nonce invalid/expired");
        s_nonceValid = false;
        OTAShared::noteAuthFailure();
        sendJSONError(client, 401, "Unauthorized", "nonce");
        return;
    }

    uint8_t expected[OTAShared::HASH_SIZE];
    OTAShared::computeAuthHash(s_nonce, OTAShared::NONCE_SIZE, OTAShared::psk(), OTAShared::pskSize(), expected);
    if (!OTAShared::constTimeEq(reqAuth, expected, OTAShared::HASH_SIZE)) {
        LOG_WARN("ETH HTTP OTA: Authentication failed");
        s_nonceValid = false;
        OTAShared::noteAuthFailure();
        sendJSONError(client, 401, "Unauthorized", "auth");
        return;
    }

    // Consume nonce
    s_nonceValid = false;

    if (!Update.begin(declaredSize)) {
        LOG_ERROR("ETH HTTP OTA: Update.begin() failed, error=%u", Update.getError());
        sendJSONError(client, 500, "Internal Server Error", "begin");
        return;
    }

    LOG_INFO("ETH HTTP OTA: Receiving %u bytes, CRC=0x%08X", (unsigned)declaredSize, (unsigned)declaredCrc);

    uint8_t buf[CHUNK_SIZE];
    size_t remaining = declaredSize;
    size_t totalReceived = 0;
    uint32_t crc = CRC32_INITIAL;
    uint32_t lastActivity = millis();

    while (remaining > 0) {
        if (!client.connected()) {
            LOG_WARN("ETH HTTP OTA: Client disconnected during transfer");
            Update.end(false);
            return;
        }
        int avail = client.available();
        if (avail <= 0) {
            if (millis() - lastActivity > BODY_TIMEOUT_MS) {
                LOG_WARN("ETH HTTP OTA: Timeout (%u/%u)", (unsigned)totalReceived, (unsigned)declaredSize);
                Update.end(false);
                sendJSONError(client, 408, "Request Timeout", "timeout");
                return;
            }
            FEED_WATCHDOG();
            delay(1);
            continue;
        }
        size_t cap = sizeof(buf);
        if (remaining < cap)
            cap = remaining;
        if ((size_t)avail < cap)
            cap = (size_t)avail;
        int got = client.read(buf, cap);
        if (got <= 0)
            continue;

        if (Update.write(buf, (size_t)got) != (size_t)got) {
            LOG_ERROR("ETH HTTP OTA: Update.write failed, error=%u", Update.getError());
            Update.end(false);
            sendJSONError(client, 500, "Internal Server Error", "write");
            return;
        }

        crc = crc32Update(buf, (size_t)got, crc);
        remaining -= (size_t)got;
        totalReceived += (size_t)got;
        lastActivity = millis();
        FEED_WATCHDOG();

        if (totalReceived % (declaredSize / 10 + 1) < (size_t)got) {
            LOG_INFO("ETH HTTP OTA: %u%% (%u/%u)", (unsigned)(100ULL * totalReceived / declaredSize),
                     (unsigned)totalReceived, (unsigned)declaredSize);
        }
    }

    uint32_t computedCRC = crc32Final(crc);
    if (computedCRC != declaredCrc) {
        LOG_ERROR("ETH HTTP OTA: CRC mismatch (expected=0x%08X, computed=0x%08X)", (unsigned)declaredCrc,
                  (unsigned)computedCRC);
        Update.end(false);
        sendJSONError(client, 400, "Bad Request", "crc");
        return;
    }

    if (!Update.end(true)) {
        LOG_ERROR("ETH HTTP OTA: Update.end() failed, error=%u", Update.getError());
        sendJSONError(client, 500, "Internal Server Error", "finalize");
        return;
    }

    LOG_INFO("ETH HTTP OTA: Update staged (%u bytes). Rebooting...", (unsigned)declaredSize);

    const char *body = "{\"ok\":true}";
    int bodyLen = (int)strlen(body);
    writeStatus(client, 200, "OK");
    writeCORS(client, "GET, PUT, OPTIONS");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(bodyLen);
    client.print("\r\nConnection: close\r\n\r\n");
    client.write((const uint8_t *)body, (size_t)bodyLen);
    client.flush();
    delay(50); // let TX buffer drain onto the wire

#ifdef ARCH_RP2040
    rp2040.reboot();
#endif
}

void initEthHttpOTA()
{
    if (!httpServer) {
        httpServer = new EthernetServer(ETH_HTTP_OTA_PORT);
        httpServer->begin();
        LOG_INFO("ETH HTTP OTA: Server listening on TCP port %d", ETH_HTTP_OTA_PORT);
    }
}

void ethHttpOTALoop()
{
    if (!httpServer)
        return;

    EthernetClient client = httpServer->accept();
    if (client) {
        // Reuse the unified handleApiClient routing — it knows the OTA paths
        // via handleOtaNonce/Upload/Info exposed in ethHttpOTA.h. This means
        // the OTA endpoints are now reachable on port :80, :443 (TLS), and
        // :4244 (legacy) with one set of handlers.
        EthernetClientStream stream(client);
        handleApiClient(stream);
        client.stop();
    }
}

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
