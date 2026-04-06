#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#include "ethOTA.h"
#include <ErriezCRC32.h>
#include <SHA256.h>
#include <Updater.h>
#ifdef ARCH_RP2040
#include <hardware/watchdog.h>
#define FEED_WATCHDOG() watchdog_update()
#else
#define FEED_WATCHDOG() ((void)0)
#endif

/// Protocol header sent by the upload tool
struct __attribute__((packed)) OTAHeader {
    uint8_t magic[4];      // "MOTA" (Meshtastic OTA)
    uint32_t firmwareSize; // Size of the firmware payload in bytes (little-endian)
    uint32_t crc32;        // CRC32 of the entire firmware payload
};

/// Response codes sent back to the client
enum OTAResponse : uint8_t {
    OTA_OK = 0x00,
    OTA_ERR_CRC = 0x01,
    OTA_ERR_SIZE = 0x02,
    OTA_ERR_WRITE = 0x03,
    OTA_ERR_MAGIC = 0x04,
    OTA_ERR_BEGIN = 0x05,
    OTA_ERR_TIMEOUT = 0x06,
    OTA_ACK = 0x06, // ACK uses ASCII ACK character
    OTA_ERR_AUTH = 0x07,
};

static const uint32_t OTA_TIMEOUT_MS = 30000;       // 30s inactivity timeout
static const size_t OTA_CHUNK_SIZE = 1024;           // 1KB receive buffer
static const uint32_t OTA_AUTH_COOLDOWN_MS = 5000;   // 5s cooldown after failed auth
static const size_t OTA_NONCE_SIZE = 32;
static const size_t OTA_HASH_SIZE = 32;

// OTA PSK — override via USERPREFS_OTA_PSK in userPrefs.jsonc
#ifdef USERPREFS_OTA_PSK
static const uint8_t otaPSK[] = USERPREFS_OTA_PSK;
#else
// Default PSK (CHANGE THIS for production deployments)
static const uint8_t otaPSK[] = {0x6d, 0x65, 0x73, 0x68, 0x74, 0x61, 0x73, 0x74, 0x69, 0x63, 0x5f,
                                  0x6f, 0x74, 0x61, 0x5f, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74,
                                  0x5f, 0x70, 0x73, 0x6b, 0x5f, 0x76, 0x31, 0x21, 0x21, 0x21};
// = "meshtastic_ota_default_psk_v1!!!"
#endif
static const size_t otaPSKSize = sizeof(otaPSK);

static EthernetServer *otaServer = nullptr;
static uint32_t lastAuthFailure = 0;

static bool readExact(EthernetClient &client, uint8_t *buf, size_t len)
{
    size_t received = 0;
    uint32_t lastActivity = millis();

    while (received < len) {
        if (!client.connected()) {
            return false;
        }
        int avail = client.available();
        if (avail > 0) {
            size_t toRead = min((size_t)avail, len - received);
            size_t got = client.read(buf + received, toRead);
            received += got;
            lastActivity = millis();
        } else {
            if (millis() - lastActivity > OTA_TIMEOUT_MS) {
                return false;
            }
            delay(1);
        }
        FEED_WATCHDOG();
    }
    return true;
}

/// Compute SHA256(nonce || psk) for challenge-response authentication
static void computeAuthHash(const uint8_t *nonce, size_t nonceLen, const uint8_t *psk, size_t pskLen, uint8_t *hashOut)
{
    SHA256 sha;
    sha.reset();
    sha.update(nonce, nonceLen);
    sha.update(psk, pskLen);
    sha.finalize(hashOut, OTA_HASH_SIZE);
}

/// Challenge-response authentication. Returns true if client is authenticated.
static bool authenticateClient(EthernetClient &client)
{
    // Rate-limit after failed auth
    if (lastAuthFailure != 0 && (millis() - lastAuthFailure) < OTA_AUTH_COOLDOWN_MS) {
        LOG_WARN("ETH OTA: Auth cooldown active, rejecting connection");
        client.write(OTA_ERR_AUTH);
        return false;
    }

    // Generate random nonce
    uint8_t nonce[OTA_NONCE_SIZE];
    for (size_t i = 0; i < OTA_NONCE_SIZE; i += 4) {
        uint32_t r = random();
        size_t remaining = OTA_NONCE_SIZE - i;
        memcpy(nonce + i, &r, min((size_t)4, remaining));
    }

    // Send nonce to client
    client.write(nonce, OTA_NONCE_SIZE);

    // Read client's response: SHA256(nonce || PSK)
    uint8_t clientHash[OTA_HASH_SIZE];
    if (!readExact(client, clientHash, OTA_HASH_SIZE)) {
        LOG_WARN("ETH OTA: Timeout reading auth response");
        lastAuthFailure = millis();
        return false;
    }

    // Compute expected hash
    uint8_t expectedHash[OTA_HASH_SIZE];
    computeAuthHash(nonce, OTA_NONCE_SIZE, otaPSK, otaPSKSize, expectedHash);

    // Constant-time comparison to prevent timing attacks
    uint8_t diff = 0;
    for (size_t i = 0; i < OTA_HASH_SIZE; i++) {
        diff |= clientHash[i] ^ expectedHash[i];
    }

    if (diff != 0) {
        LOG_WARN("ETH OTA: Authentication failed");
        client.write(OTA_ERR_AUTH);
        lastAuthFailure = millis();
        return false;
    }

    // Auth success — send ACK
    client.write(OTA_ACK);
    LOG_INFO("ETH OTA: Authentication successful");
    return true;
}

static void handleOTAClient(EthernetClient &client)
{
    LOG_INFO("ETH OTA: Client connected from %u.%u.%u.%u", client.remoteIP()[0], client.remoteIP()[1],
             client.remoteIP()[2], client.remoteIP()[3]);

    // Step 1: Challenge-response authentication
    if (!authenticateClient(client)) {
        return;
    }

    // Step 2: Read 12-byte header
    OTAHeader hdr;
    if (!readExact(client, (uint8_t *)&hdr, sizeof(hdr))) {
        LOG_WARN("ETH OTA: Timeout reading header");
        return;
    }

    // Validate magic
    if (memcmp(hdr.magic, "MOTA", 4) != 0) {
        LOG_WARN("ETH OTA: Invalid magic");
        client.write(OTA_ERR_MAGIC);
        return;
    }

    LOG_INFO("ETH OTA: Firmware size=%u, CRC32=0x%08X", hdr.firmwareSize, hdr.crc32);

    // Sanity check on size (must be > 0 and fit in LittleFS)
    if (hdr.firmwareSize == 0 || hdr.firmwareSize > 1024 * 1024) {
        LOG_WARN("ETH OTA: Invalid firmware size");
        client.write(OTA_ERR_SIZE);
        return;
    }

    // Begin the update — this opens firmware.bin on LittleFS
    if (!Update.begin(hdr.firmwareSize)) {
        LOG_ERROR("ETH OTA: Update.begin() failed, error=%u", Update.getError());
        client.write(OTA_ERR_BEGIN);
        return;
    }

    // ACK the header — client can start sending firmware data
    client.write(OTA_ACK);

    // Receive firmware in chunks
    uint8_t buf[OTA_CHUNK_SIZE];
    size_t remaining = hdr.firmwareSize;
    uint32_t crc = CRC32_INITIAL;
    uint32_t lastActivity = millis();
    size_t totalReceived = 0;

    while (remaining > 0) {
        if (!client.connected()) {
            LOG_WARN("ETH OTA: Client disconnected during transfer");
            Update.end(false);
            return;
        }

        int avail = client.available();
        if (avail <= 0) {
            if (millis() - lastActivity > OTA_TIMEOUT_MS) {
                LOG_WARN("ETH OTA: Timeout during transfer (%u/%u bytes)", totalReceived, hdr.firmwareSize);
                client.write(OTA_ERR_TIMEOUT);
                Update.end(false);
                return;
            }
            delay(1);
            FEED_WATCHDOG();
            continue;
        }

        size_t toRead = min((size_t)avail, min(remaining, sizeof(buf)));
        size_t got = client.read(buf, toRead);
        if (got == 0)
            continue;

        // Write to Updater (LittleFS firmware.bin)
        size_t written = Update.write(buf, got);
        if (written != got) {
            LOG_ERROR("ETH OTA: Write failed (wrote %u of %u), error=%u", written, got, Update.getError());
            client.write(OTA_ERR_WRITE);
            Update.end(false);
            return;
        }

        crc = crc32Update(buf, got, crc);
        remaining -= got;
        totalReceived += got;
        lastActivity = millis();
        FEED_WATCHDOG();

        // Progress log every ~10%
        if (totalReceived % (hdr.firmwareSize / 10 + 1) < got) {
            LOG_INFO("ETH OTA: %u%% (%u/%u bytes)", (uint32_t)(100ULL * totalReceived / hdr.firmwareSize), totalReceived,
                     hdr.firmwareSize);
        }
    }

    // Verify CRC32
    uint32_t computedCRC = crc32Final(crc);
    if (computedCRC != hdr.crc32) {
        LOG_ERROR("ETH OTA: CRC mismatch (expected=0x%08X, computed=0x%08X)", hdr.crc32, computedCRC);
        client.write(OTA_ERR_CRC);
        Update.end(false);
        return;
    }

    // Finalize — this calls picoOTA.commit() which stages the update for the bootloader
    if (!Update.end(true)) {
        LOG_ERROR("ETH OTA: Update.end() failed, error=%u", Update.getError());
        client.write(OTA_ERR_WRITE);
        return;
    }

    LOG_INFO("ETH OTA: Update staged successfully (%u bytes). Rebooting...", hdr.firmwareSize);
    client.write(OTA_OK);
    client.flush();
    delay(500);

    // Reboot — the built-in bootloader will apply the update from LittleFS
    rp2040.reboot();
}

void initEthOTA()
{
    if (!otaServer) {
        otaServer = new EthernetServer(ETH_OTA_PORT);
        otaServer->begin();
        LOG_INFO("ETH OTA: Server listening on TCP port %d", ETH_OTA_PORT);
    }
}

void ethOTALoop()
{
    if (!otaServer)
        return;

    EthernetClient client = otaServer->accept();
    if (client) {
        handleOTAClient(client);
        client.stop();
    }
}

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
