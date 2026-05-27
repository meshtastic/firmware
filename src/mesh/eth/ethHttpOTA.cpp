#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#include "ethHttpOTA.h"
#include "otaShared.h"
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

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

static constexpr uint32_t NONCE_TTL_MS = 30000;
static constexpr uint32_t HEADER_TIMEOUT_MS = 5000;
static constexpr uint32_t BODY_TIMEOUT_MS = 30000;
static constexpr size_t CHUNK_SIZE = 1024;

static EthernetServer *httpServer = nullptr;

static uint8_t s_nonce[OTAShared::NONCE_SIZE];
static uint32_t s_nonceIssuedMs = 0;
static bool s_nonceValid = false;

struct ReqHeaders {
    char method[8];
    char path[64];
    long contentLength;
    char xOtaNonce[OTAShared::NONCE_SIZE * 2 + 1];
    char xOtaAuth[OTAShared::HASH_SIZE * 2 + 1];
    long xOtaSize;
    char xOtaCrc32[16];
};

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

static bool headerEq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        char cb = *b;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return false;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

// Returns line length (excl. CRLF) on success.
// Returns -1 if line is longer than bufSize-1 (line is truncated in buf, but still drained to LF).
// Returns -2 on timeout / disconnect.
static int readLine(EthernetClient &client, char *buf, size_t bufSize, uint32_t timeoutMs)
{
    size_t len = 0;
    bool truncated = false;
    uint32_t start = millis();
    for (;;) {
        if (!client.connected())
            return -2;
        int avail = client.available();
        if (avail <= 0) {
            if (millis() - start > timeoutMs)
                return -2;
            FEED_WATCHDOG();
            delay(1);
            continue;
        }
        int c = client.read();
        if (c < 0)
            continue;
        if (c == '\n') {
            if (len > 0 && len < bufSize && buf[len - 1] == '\r')
                len--;
            if (len < bufSize)
                buf[len] = '\0';
            else
                buf[bufSize - 1] = '\0';
            return truncated ? -1 : (int)len;
        }
        if (len + 1 < bufSize) {
            buf[len++] = (char)c;
        } else {
            truncated = true; // keep draining until LF
        }
    }
}

static bool parseRequest(EthernetClient &client, ReqHeaders &h)
{
    h.method[0] = 0;
    h.path[0] = 0;
    h.contentLength = -1;
    h.xOtaNonce[0] = 0;
    h.xOtaAuth[0] = 0;
    h.xOtaSize = -1;
    h.xOtaCrc32[0] = 0;

    // Browsers can send long headers (User-Agent, sec-ch-ua, Referer with cookies, etc.).
    // 1KB covers realistic worst case; truncation falls back to "ignore this header".
    char line[1024];

    int n = readLine(client, line, sizeof(line), HEADER_TIMEOUT_MS);
    if (n <= 0)
        return false; // request line must fit and be well-formed

    char *p = line;
    char *sp = strchr(p, ' ');
    if (!sp)
        return false;
    *sp = 0;
    strncpy(h.method, p, sizeof(h.method) - 1);
    h.method[sizeof(h.method) - 1] = 0;

    p = sp + 1;
    sp = strchr(p, ' ');
    if (!sp)
        return false;
    *sp = 0;
    strncpy(h.path, p, sizeof(h.path) - 1);
    h.path[sizeof(h.path) - 1] = 0;

    while (true) {
        n = readLine(client, line, sizeof(line), HEADER_TIMEOUT_MS);
        if (n == -2)
            return false;        // timeout/disconnect: real failure
        if (n == -1)
            continue;            // header too long for our buffer — skip, keep going
        if (n == 0)
            break;               // end of headers

        char *colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = 0;
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t')
            val++;

        if (headerEq(line, "Content-Length")) {
            h.contentLength = strtol(val, nullptr, 10);
        } else if (headerEq(line, "X-OTA-Nonce")) {
            strncpy(h.xOtaNonce, val, sizeof(h.xOtaNonce) - 1);
            h.xOtaNonce[sizeof(h.xOtaNonce) - 1] = 0;
        } else if (headerEq(line, "X-OTA-Auth")) {
            strncpy(h.xOtaAuth, val, sizeof(h.xOtaAuth) - 1);
            h.xOtaAuth[sizeof(h.xOtaAuth) - 1] = 0;
        } else if (headerEq(line, "X-OTA-Size")) {
            h.xOtaSize = strtol(val, nullptr, 10);
        } else if (headerEq(line, "X-OTA-CRC32")) {
            strncpy(h.xOtaCrc32, val, sizeof(h.xOtaCrc32) - 1);
            h.xOtaCrc32[sizeof(h.xOtaCrc32) - 1] = 0;
        }
    }
    return true;
}

static void writeStatus(EthernetClient &client, int code, const char *codeName)
{
    client.print("HTTP/1.1 ");
    client.print(code);
    client.print(" ");
    client.print(codeName);
    client.print("\r\n");
}

static void writeCORS(EthernetClient &client, const char *methods)
{
    client.print("Access-Control-Allow-Origin: *\r\n");
    client.print("Access-Control-Allow-Methods: ");
    client.print(methods);
    client.print("\r\n");
    client.print("Access-Control-Allow-Headers: Content-Type, X-OTA-Nonce, X-OTA-Auth, X-OTA-Size, X-OTA-CRC32\r\n");
    client.print("Access-Control-Allow-Private-Network: true\r\n");
}

static void sendJSONError(EthernetClient &client, int code, const char *codeName, const char *err)
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

static void handleOptions(EthernetClient &client, const char *methods)
{
    writeStatus(client, 204, "No Content");
    writeCORS(client, methods);
    client.print("Content-Length: 0\r\n");
    client.print("Connection: close\r\n\r\n");
}

static void handleInfo(EthernetClient &client)
{
    char body[256];
    int n = snprintf(body, sizeof(body),
                     "{\"pio_env\":\"%s\",\"firmware_version\":\"%s\",\"hw_model\":%d,\"uptime_s\":%lu}",
                     optstr(APP_ENV), optstr(APP_VERSION), (int)HW_VENDOR, (unsigned long)(millis() / 1000));

    writeStatus(client, 200, "OK");
    writeCORS(client, "GET, OPTIONS");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(n);
    client.print("\r\nConnection: close\r\n\r\n");
    client.write((const uint8_t *)body, (size_t)n);
}

static void handleNonce(EthernetClient &client)
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

static void handleUpload(EthernetClient &client, const ReqHeaders &h)
{
    uint8_t reqNonce[OTAShared::NONCE_SIZE];
    uint8_t reqAuth[OTAShared::HASH_SIZE];

    if (!hexToBytes(h.xOtaNonce, strlen(h.xOtaNonce), reqNonce, OTAShared::NONCE_SIZE) ||
        !hexToBytes(h.xOtaAuth, strlen(h.xOtaAuth), reqAuth, OTAShared::HASH_SIZE)) {
        LOG_WARN("ETH HTTP OTA: Bad nonce/auth header format");
        sendJSONError(client, 400, "Bad Request", "bad_headers");
        return;
    }

    if (h.xOtaSize <= 0 || (size_t)h.xOtaSize > OTAShared::MAX_FW_SIZE) {
        sendJSONError(client, 400, "Bad Request", "size");
        return;
    }
    size_t declaredSize = (size_t)h.xOtaSize;

    if (h.xOtaCrc32[0] == 0) {
        sendJSONError(client, 400, "Bad Request", "crc_header");
        return;
    }
    char *end = nullptr;
    uint32_t declaredCrc = (uint32_t)strtoul(h.xOtaCrc32, &end, 16);
    if (end == h.xOtaCrc32 || *end != '\0') {
        sendJSONError(client, 400, "Bad Request", "crc_header");
        return;
    }

    if (h.contentLength != (long)declaredSize) {
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
        size_t got = client.read(buf, cap);
        if (got == 0)
            continue;

        if (Update.write(buf, got) != got) {
            LOG_ERROR("ETH HTTP OTA: Update.write failed, error=%u", Update.getError());
            Update.end(false);
            sendJSONError(client, 500, "Internal Server Error", "write");
            return;
        }

        crc = crc32Update(buf, got, crc);
        remaining -= got;
        totalReceived += got;
        lastActivity = millis();
        FEED_WATCHDOG();

        if (totalReceived % (declaredSize / 10 + 1) < got) {
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
    // Explicitly close the connection so the browser sees a clean FIN before
    // the W5500 goes dark on reboot. Without this, fetch() in Chrome rejects
    // with "Failed to fetch" even though it received the response bytes.
    client.stop();
    delay(500);

#ifdef ARCH_RP2040
    rp2040.reboot();
#endif
}

static void handleClient(EthernetClient &client)
{
    LOG_INFO("ETH HTTP OTA: Client connected from %u.%u.%u.%u", client.remoteIP()[0], client.remoteIP()[1],
             client.remoteIP()[2], client.remoteIP()[3]);

    ReqHeaders h;
    if (!parseRequest(client, h)) {
        sendJSONError(client, 400, "Bad Request", "parse");
        return;
    }

    LOG_DEBUG("ETH HTTP OTA: %s %s", h.method, h.path);

    if (strcmp(h.method, "OPTIONS") == 0) {
        const char *methods = (strcmp(h.path, "/api/v1/ota") == 0) ? "PUT, OPTIONS" : "GET, OPTIONS";
        handleOptions(client, methods);
    } else if (strcmp(h.method, "GET") == 0 && strcmp(h.path, "/api/v1/info") == 0) {
        handleInfo(client);
    } else if (strcmp(h.method, "GET") == 0 && strcmp(h.path, "/api/v1/ota/nonce") == 0) {
        handleNonce(client);
    } else if (strcmp(h.method, "PUT") == 0 && strcmp(h.path, "/api/v1/ota") == 0) {
        handleUpload(client, h);
    } else {
        sendJSONError(client, 404, "Not Found", "not_found");
    }
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
        handleClient(client);
        client.stop();
    }
}

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
