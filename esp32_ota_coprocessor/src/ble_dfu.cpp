#include "ble_dfu.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// Nordic Secure DFU — Control Point opcodes
// ---------------------------------------------------------------------------
#define DFU_OP_CREATE_OBJECT    0x01
#define DFU_OP_SET_PRN          0x02
#define DFU_OP_CALC_CHECKSUM    0x03
#define DFU_OP_EXECUTE          0x04
#define DFU_OP_SELECT           0x06
#define DFU_OP_RESPONSE         0x60

// Object types for Create / Select
#define DFU_OBJ_COMMAND         0x01  // init packet (.dat)
#define DFU_OBJ_DATA            0x02  // firmware binary (.bin)

// Result codes inside DFU_OP_RESPONSE
#define DFU_RES_SUCCESS         0x01
#define DFU_RES_OPCODE_NOT_SUP  0x02
#define DFU_RES_INVALID_PARAM   0x03
#define DFU_RES_INSUFFICIENT    0x04
#define DFU_RES_INVALID_OBJ     0x05
#define DFU_RES_UNSUPPORTED     0x07
#define DFU_RES_OPERATION_FAIL  0x0A

// ---------------------------------------------------------------------------
// Notification state (written from BLE task, read from main task)
// ---------------------------------------------------------------------------
static volatile bool   s_notif_ready = false;
static uint8_t         s_notif_buf[20];
static volatile size_t s_notif_len   = 0;

static void ctrlNotifyCb(NimBLERemoteCharacteristic * /*pChar*/,
                         uint8_t *data, size_t len, bool /*isNotify*/)
{
    size_t n = len < sizeof(s_notif_buf) ? len : sizeof(s_notif_buf);
    memcpy(s_notif_buf, data, n);
    s_notif_len   = n;
    s_notif_ready = true;
}

// ---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3 / Nordic DFU uses this exact polynomial)
// ---------------------------------------------------------------------------
static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

// Wait up to `ms` for a notification from the Control Point.
static bool waitNotif(uint32_t ms = 8000)
{
    s_notif_ready = false;
    uint32_t t = millis();
    while (!s_notif_ready) {
        if (millis() - t > ms) {
            Serial.println("[DFU] Timeout waiting for notification");
            return false;
        }
        delay(1);
    }
    return true;
}

// Write a command to the Control Point (with response) and wait for notification.
static bool ctrlWrite(NimBLERemoteCharacteristic *pCtrl,
                      const uint8_t *data, size_t len)
{
    if (!pCtrl->writeValue(data, len, true)) {
        Serial.println("[DFU] Control Point write failed");
        return false;
    }
    return waitNotif();
}

// Check that the last notification is a success response for the given opcode.
static bool checkResponse(uint8_t expected_op)
{
    if (s_notif_len < 3) {
        Serial.printf("[DFU] Short response (%u bytes)\n", (unsigned)s_notif_len);
        return false;
    }
    if (s_notif_buf[0] != DFU_OP_RESPONSE) {
        Serial.printf("[DFU] Unexpected opcode 0x%02X (expected 0x60)\n", s_notif_buf[0]);
        return false;
    }
    if (s_notif_buf[1] != expected_op) {
        Serial.printf("[DFU] Response for op 0x%02X, expected 0x%02X\n",
                      s_notif_buf[1], expected_op);
        return false;
    }
    if (s_notif_buf[2] != DFU_RES_SUCCESS) {
        Serial.printf("[DFU] Operation 0x%02X failed, result=0x%02X\n",
                      expected_op, s_notif_buf[2]);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Select an object and return its max_size (used for data segments).
// Response layout after 3-byte header: max_size(4) | offset(4) | crc32(4)
// ---------------------------------------------------------------------------
static bool selectObject(NimBLERemoteCharacteristic *pCtrl,
                         uint8_t obj_type, uint32_t *out_max_size)
{
    uint8_t cmd[2] = {DFU_OP_SELECT, obj_type};
    if (!ctrlWrite(pCtrl, cmd, sizeof(cmd))) return false;
    if (!checkResponse(DFU_OP_SELECT))       return false;

    if (s_notif_len < 15) {
        Serial.println("[DFU] Select response too short");
        return false;
    }
    // Bytes [3..6] = max_size LE32
    uint32_t max_size;
    memcpy(&max_size, &s_notif_buf[3], 4);
    *out_max_size = max_size;
    Serial.printf("[DFU] Select 0x%02X: max_size=%u offset=%u crc=0x%08X\n",
                  obj_type, max_size,
                  (unsigned)(s_notif_buf[7]  | (s_notif_buf[8]<<8)  |
                             (s_notif_buf[9]<<16) | (s_notif_buf[10]<<24)),
                  (unsigned)(s_notif_buf[11] | (s_notif_buf[12]<<8) |
                             (s_notif_buf[13]<<16) | (s_notif_buf[14]<<24)));
    return true;
}

// Create a new object of `obj_type` with the given size.
static bool createObject(NimBLERemoteCharacteristic *pCtrl,
                         uint8_t obj_type, uint32_t size)
{
    uint8_t cmd[6];
    cmd[0] = DFU_OP_CREATE_OBJECT;
    cmd[1] = obj_type;
    cmd[2] = (size >> 0)  & 0xFF;
    cmd[3] = (size >> 8)  & 0xFF;
    cmd[4] = (size >> 16) & 0xFF;
    cmd[5] = (size >> 24) & 0xFF;
    if (!ctrlWrite(pCtrl, cmd, sizeof(cmd))) return false;
    return checkResponse(DFU_OP_CREATE_OBJECT);
}

// Request a CRC from the target and verify it matches `expected_crc`.
static bool verifyCrc(NimBLERemoteCharacteristic *pCtrl, uint32_t expected_crc)
{
    uint8_t cmd = DFU_OP_CALC_CHECKSUM;
    if (!ctrlWrite(pCtrl, &cmd, 1)) return false;
    if (!checkResponse(DFU_OP_CALC_CHECKSUM)) return false;

    // Response bytes [3..6] = offset, [7..10] = crc32
    if (s_notif_len < 11) {
        Serial.println("[DFU] CRC response too short");
        return false;
    }
    uint32_t remote_crc;
    memcpy(&remote_crc, &s_notif_buf[7], 4);

    if (remote_crc != expected_crc) {
        Serial.printf("[DFU] CRC mismatch: local=0x%08X remote=0x%08X\n",
                      expected_crc, remote_crc);
        return false;
    }
    return true;
}

// Execute the current object.
static bool executeObject(NimBLERemoteCharacteristic *pCtrl)
{
    uint8_t cmd = DFU_OP_EXECUTE;
    if (!ctrlWrite(pCtrl, &cmd, 1)) return false;
    return checkResponse(DFU_OP_EXECUTE);
}

// ---------------------------------------------------------------------------
// Write all bytes of a file section [offset, offset+size) to the Packet char.
// Returns the CRC32 of the data written (starting from crc_init).
// ---------------------------------------------------------------------------
static bool writeFileChunks(NimBLERemoteCharacteristic *pPkt,
                             File &f, size_t size, size_t chunk,
                             uint32_t *inout_crc)
{
    uint8_t buf[512];
    size_t  remaining = size;

    while (remaining > 0) {
        size_t n = min(remaining, min(chunk, sizeof(buf)));
        int    r = f.read(buf, n);
        if (r <= 0) {
            Serial.println("[DFU] File read error");
            return false;
        }
        size_t got = (size_t)r;
        *inout_crc = crc32_update(*inout_crc, buf, got);

        // Write without response; retry if the TX buffer is full
        size_t sent = 0;
        while (sent < got) {
            size_t to_send = min(got - sent, chunk);
            if (!pPkt->writeValue(buf + sent, to_send, false)) {
                delay(5);
                continue;
            }
            sent += to_send;
        }
        remaining -= got;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Send the init packet (.dat) — always fits in one Command Object
// ---------------------------------------------------------------------------
static bool sendInitPacket(NimBLERemoteCharacteristic *pCtrl,
                           NimBLERemoteCharacteristic *pPkt,
                           const char *dat_path)
{
    File f = LittleFS.open(dat_path, "r");
    if (!f) {
        Serial.printf("[DFU] Cannot open %s\n", dat_path);
        return false;
    }
    size_t dat_size = f.size();
    Serial.printf("[DFU] Init packet: %u bytes\n", (unsigned)dat_size);

    uint32_t max_size = 0;
    if (!selectObject(pCtrl, DFU_OBJ_COMMAND, &max_size)) { f.close(); return false; }

    if (dat_size > max_size) {
        Serial.printf("[DFU] Init packet (%u) > max_size (%u)\n",
                      (unsigned)dat_size, max_size);
        f.close();
        return false;
    }

    if (!createObject(pCtrl, DFU_OBJ_COMMAND, (uint32_t)dat_size)) {
        f.close(); return false;
    }

    uint32_t crc  = 0xFFFFFFFF;
    size_t   chunk = DFU_CHUNK_SIZE;

    if (!writeFileChunks(pPkt, f, dat_size, chunk, &crc)) {
        f.close(); return false;
    }
    f.close();

    crc ^= 0xFFFFFFFF; // finalize CRC32

    if (!verifyCrc(pCtrl, crc))  return false;
    if (!executeObject(pCtrl))   return false;

    Serial.println("[DFU] Init packet sent and executed OK");
    return true;
}

// ---------------------------------------------------------------------------
// Send the firmware binary (.bin) — split into segments of max_size bytes
// ---------------------------------------------------------------------------
static bool sendFirmwareBin(NimBLERemoteCharacteristic *pCtrl,
                            NimBLERemoteCharacteristic *pPkt,
                            const char *bin_path)
{
    File f = LittleFS.open(bin_path, "r");
    if (!f) {
        Serial.printf("[DFU] Cannot open %s\n", bin_path);
        return false;
    }
    size_t bin_size = f.size();
    Serial.printf("[DFU] Firmware binary: %u bytes\n", (unsigned)bin_size);

    uint32_t max_size = 0;
    if (!selectObject(pCtrl, DFU_OBJ_DATA, &max_size)) { f.close(); return false; }

    size_t   chunk    = DFU_CHUNK_SIZE;
    size_t   remaining = bin_size;
    uint32_t crc       = 0xFFFFFFFF; // running CRC across the whole file

    while (remaining > 0) {
        uint32_t seg_size = (uint32_t)min(remaining, (size_t)max_size);
        Serial.printf("[DFU] Data object: %u bytes (%u remaining)\n",
                      seg_size, (unsigned)remaining);

        if (!createObject(pCtrl, DFU_OBJ_DATA, seg_size)) { f.close(); return false; }

        uint32_t seg_crc = crc; // CRC continues across segments
        if (!writeFileChunks(pPkt, f, seg_size, chunk, &seg_crc)) {
            f.close(); return false;
        }
        crc = seg_crc;

        uint32_t check_crc = crc ^ 0xFFFFFFFF;
        if (!verifyCrc(pCtrl, check_crc))  { f.close(); return false; }
        if (!executeObject(pCtrl))         { f.close(); return false; }

        remaining -= seg_size;
    }

    f.close();
    Serial.println("[DFU] Firmware binary sent and executed OK");
    return true;
}

// ---------------------------------------------------------------------------
// BLE scan — find "DfuTarg" advertisement
// ---------------------------------------------------------------------------
static NimBLEAdvertisedDevice *scanForDfuTarget()
{
    NimBLEScan *pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(45);
    pScan->setWindow(15);

    for (int attempt = 0; attempt < DFU_SCAN_RETRIES; attempt++) {
        Serial.printf("[BLE] Scanning for \"%s\" (attempt %d/%d)...\n",
                      DFU_TARGET_NAME, attempt + 1, DFU_SCAN_RETRIES);

        NimBLEScanResults results = pScan->start(DFU_SCAN_SECS, false);

        for (int i = 0; i < results.getCount(); i++) {
            NimBLEAdvertisedDevice dev = results.getDevice(i);
            if (dev.getName() == DFU_TARGET_NAME) {
                Serial.printf("[BLE] Found \"%s\" at %s\n",
                              DFU_TARGET_NAME, dev.getAddress().toString().c_str());
                // Allocate on heap so pointer outlives scan results
                return new NimBLEAdvertisedDevice(dev);
            }
        }
        pScan->clearResults();
        Serial.println("[BLE] Not found, retrying...");
    }

    Serial.println("[BLE] DFU target not found after all retries");
    return nullptr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool runBleNordicDfu(const char *dat_path, const char *bin_path)
{
    NimBLEDevice::init("");
    NimBLEDevice::setMTU(512); // negotiate up to 512; actual = min(ours, target's)

    // 1. Scan
    NimBLEAdvertisedDevice *pTarget = scanForDfuTarget();
    if (!pTarget) return false;

    // 2. Connect
    NimBLEClient *pClient = NimBLEDevice::createClient();
    Serial.printf("[BLE] Connecting to %s...\n",
                  pTarget->getAddress().toString().c_str());

    if (!pClient->connect(pTarget)) {
        Serial.println("[BLE] Connection failed");
        delete pTarget;
        NimBLEDevice::deleteClient(pClient);
        return false;
    }
    delete pTarget;
    Serial.printf("[BLE] Connected, MTU=%u\n", pClient->getMTU());

    // Derive usable payload size from negotiated MTU
    size_t mtu_payload = (size_t)(pClient->getMTU() - 3);
    // DFU_CHUNK_SIZE is the compile-time cap; use the smaller of the two
    // (writeFileChunks already caps to DFU_CHUNK_SIZE, this is informational)
    Serial.printf("[BLE] Effective chunk size: %u bytes\n",
                  (unsigned)min(mtu_payload, (size_t)DFU_CHUNK_SIZE));

    // 3. Discover DFU service
    NimBLERemoteService *pSvc = pClient->getService(DFU_SERVICE_UUID);
    if (!pSvc) {
        Serial.println("[BLE] DFU service not found");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        return false;
    }

    NimBLERemoteCharacteristic *pCtrl = pSvc->getCharacteristic(DFU_CTRL_UUID);
    NimBLERemoteCharacteristic *pPkt  = pSvc->getCharacteristic(DFU_PKT_UUID);

    if (!pCtrl || !pPkt) {
        Serial.println("[BLE] DFU characteristics not found");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        return false;
    }

    // 4. Subscribe to Control Point notifications
    if (!pCtrl->subscribe(true, ctrlNotifyCb)) {
        Serial.println("[BLE] Failed to subscribe to Control Point");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        return false;
    }
    delay(100); // let subscription settle

    // 5. Run DFU
    bool ok = sendInitPacket(pCtrl, pPkt, dat_path) &&
              sendFirmwareBin(pCtrl, pPkt, bin_path);

    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);

    if (ok) {
        Serial.println("[DFU] Complete — nRF52 will reboot with new firmware");
    } else {
        Serial.println("[DFU] Failed");
    }
    return ok;
}
