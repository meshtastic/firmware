#include "configuration.h"

#if HAS_BLE_MESH && defined(ARCH_ESP32)

#include "ESP32BLEMesh.h"
#include "main.h"
#include "mesh/Router.h"

#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#else
#include "nimble/nimble/host/include/host/ble_gap.h"
#include "nimble/nimble/host/include/host/ble_hs_adv.h"
#endif

void ESP32BLEMesh::start()
{
    if (isRunning) {
        LOG_DEBUG("BLE mesh already running");
        return;
    }

    memset(peers, 0, sizeof(peers));
    peerCount = 0;
    bluetoothReady = false;
    isRunning = true;
    LOG_INFO("BLE mesh started (waiting for Bluetooth ready)");
}

void ESP32BLEMesh::onBluetoothReady()
{
    if (!isRunning)
        return;

    if (bluetoothReady)
        return;

    bluetoothReady = true;
    startScanning();
    LOG_DEBUG("BLE mesh Bluetooth ready");
}

void ESP32BLEMesh::stop()
{
    if (!isRunning)
        return;

    stopScanning();
    isRunning = false;
    LOG_INFO("BLE mesh stopped");
}

bool ESP32BLEMesh::onSend(const meshtastic_MeshPacket *mp)
{
    if (!isRunning || !mp)
        return false;

    if (!bluetoothReady) {
        LOG_DEBUG("BLE mesh TX deferred, Bluetooth not ready");
        return false;
    }

    // Don't echo packets that arrived via BLE mesh
    if (mp->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_MESH) {
        LOG_DEBUG("Drop BLE mesh echo");
        return false;
    }

    uint8_t buffer[meshtastic_MeshPacket_size];
    size_t encodedLen = encodeForBLE(mp, buffer, sizeof(buffer));
    if (encodedLen == 0) {
        LOG_WARN("BLE mesh encode failed");
        return false;
    }

    // Build manufacturer data: company ID (2 bytes) + version (1 byte) + protobuf payload
    size_t mfgDataLen = 2 + 1 + encodedLen;

    // Extended advertising can carry up to 255 bytes total.
    // Reserve 5 bytes for the flags and manufacturer-data AD headers.
    if (mfgDataLen > 250) {
        LOG_WARN("BLE mesh packet too large: %u bytes", mfgDataLen);
        return false;
    }

    uint8_t mfgData[250];
    mfgData[0] = (uint8_t)(BLE_MESH_COMPANY_ID & 0xFF);
    mfgData[1] = (uint8_t)((BLE_MESH_COMPANY_ID >> 8) & 0xFF);
    mfgData[2] = BLE_MESH_PROTOCOL_VERSION;
    memcpy(&mfgData[3], buffer, encodedLen);

    // Stop scanning while we advertise
    stopScanning();

    // Build raw advertisement data
    uint8_t advBuf[251];
    size_t advLen = 0;

    // Flags AD structure (3 bytes)
    advBuf[advLen++] = 2; // length
    advBuf[advLen++] = BLE_HS_ADV_TYPE_FLAGS;
    advBuf[advLen++] = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Manufacturer data AD structure
    advBuf[advLen++] = (uint8_t)(mfgDataLen + 1); // length (type byte + data)
    advBuf[advLen++] = BLE_HS_ADV_TYPE_MFG_DATA;
    memcpy(&advBuf[advLen], mfgData, mfgDataLen);
    advLen += mfgDataLen;

    const bool priorityRetries = shouldUsePriorityRetries(mp);
    const uint8_t extraRetries = priorityRetries ? BLE_MESH_PRIORITY_EXTRA_RETRIES : 0;
    bool sentAny = false;

    for (uint8_t attempt = 0; attempt <= extraRetries; ++attempt) {
        if (attempt > 0) {
            const uint16_t retryDelayMs =
                random(BLE_MESH_PRIORITY_RETRY_JITTER_MIN_MS, BLE_MESH_PRIORITY_RETRY_JITTER_MAX_MS + 1);
            delay(retryDelayMs);
        }

        if (sendAdvertisementBurst(advBuf, advLen, mp->id, encodedLen)) {
            sentAny = true;
        }
    }

    // Resume scanning
    startScanning();

    return sentAny;
}

bool ESP32BLEMesh::sendAdvertisementBurst(const uint8_t *advBuf, size_t advLen, PacketId packetId, size_t encodedLen)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
    // Use extended advertising if available (ESP32-S3, C3, C6)
    struct ble_gap_ext_adv_params extAdvParams = {};
    extAdvParams.connectable = 0;
    extAdvParams.scannable = 0;
    extAdvParams.directed = 0;
    extAdvParams.high_duty_directed = 0;
    extAdvParams.legacy_pdu = 0;
    extAdvParams.anonymous = 0;
    extAdvParams.include_tx_power = 0;
    extAdvParams.scan_req_notif = 0;
    extAdvParams.itvl_min = BLE_MESH_ADV_INTERVAL;
    extAdvParams.itvl_max = BLE_MESH_ADV_INTERVAL;
    extAdvParams.channel_map = 0; // all channels
    extAdvParams.own_addr_type = BLE_OWN_ADDR_PUBLIC;
    extAdvParams.primary_phy = BLE_HCI_LE_PHY_1M;
    extAdvParams.secondary_phy = BLE_HCI_LE_PHY_1M;
    extAdvParams.tx_power = 127; // host picks max

    // Use instance 1 to avoid conflicting with phone advertising on instance 0
    const uint8_t meshAdvInstance = 1;
    int8_t selectedTxPower = 0;

    int rc = ble_gap_ext_adv_configure(meshAdvInstance, &extAdvParams, &selectedTxPower, onGapEvent, this);
    if (rc != 0) {
        LOG_WARN("BLE mesh ext adv configure failed: %d", rc);
        return false;
    }

    struct os_mbuf *advData = os_msys_get_pkthdr(advLen, 0);
    if (!advData) {
        LOG_WARN("BLE mesh: failed to allocate mbuf");
        ble_gap_ext_adv_remove(meshAdvInstance);
        return false;
    }
    rc = os_mbuf_append(advData, advBuf, advLen);
    if (rc != 0) {
        os_mbuf_free_chain(advData);
        ble_gap_ext_adv_remove(meshAdvInstance);
        return false;
    }

    rc = ble_gap_ext_adv_set_data(meshAdvInstance, advData);
    if (rc != 0) {
        LOG_WARN("BLE mesh ext adv set data failed: %d", rc);
        ble_gap_ext_adv_remove(meshAdvInstance);
        return false;
    }

    // Advertise for BLE_MESH_ADV_COUNT * 100ms, then stop.
    rc = ble_gap_ext_adv_start(meshAdvInstance, BLE_MESH_ADV_COUNT, 0);
    if (rc != 0) {
        LOG_WARN("BLE mesh ext adv start failed: %d", rc);
        ble_gap_ext_adv_remove(meshAdvInstance);
        return false;
    }

    LOG_DEBUG("BLE mesh adv sent (id=%u, len=%u)", packetId, encodedLen);
    delay(BLE_MESH_ADV_BURST_MS);
    ble_gap_ext_adv_stop(meshAdvInstance);
    ble_gap_ext_adv_remove(meshAdvInstance);
    return true;
#else
    // Legacy advertising fallback (ESP32 classic - BLE 4.2, limited to 31 bytes)
    if (advLen > 31) {
        LOG_WARN("BLE mesh packet too large for legacy adv: %u bytes", advLen);
        return false;
    }

    struct ble_gap_adv_params legacyAdvParams = {};
    legacyAdvParams.conn_mode = BLE_GAP_CONN_MODE_NON;
    legacyAdvParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
    legacyAdvParams.itvl_min = BLE_MESH_ADV_INTERVAL;
    legacyAdvParams.itvl_max = BLE_MESH_ADV_INTERVAL;
    legacyAdvParams.channel_map = 0; // all channels

    int rc = ble_gap_adv_set_data(advBuf, advLen);
    if (rc != 0) {
        LOG_WARN("BLE mesh legacy adv set data failed: %d", rc);
        return false;
    }

    const int32_t durationMs = BLE_MESH_ADV_BURST_MS;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, durationMs / 10, &legacyAdvParams, onGapEvent, this);
    if (rc != 0) {
        LOG_WARN("BLE mesh legacy adv start failed: %d", rc);
        return false;
    }

    LOG_DEBUG("BLE mesh adv sent (id=%u, len=%u)", packetId, encodedLen);
    delay(durationMs);
    ble_gap_adv_stop();
    return true;
#endif
}

bool ESP32BLEMesh::shouldUsePriorityRetries(const meshtastic_MeshPacket *mp) const
{
    if (!mp)
        return false;

    // Prioritize retries for ack-sensitive traffic and direct messages only.
    const bool isDirect = (mp->to != 0) && !isBroadcast(mp->to);
    return mp->want_ack || isDirect;
}

void ESP32BLEMesh::startScanning()
{
    if (!bluetoothReady)
        return;

#if MYNEWT_VAL(BLE_EXT_ADV)
    struct ble_gap_ext_disc_params uncodedParams = {};
    uncodedParams.itvl = BLE_MESH_SCAN_INTERVAL;
    uncodedParams.window = BLE_MESH_SCAN_WINDOW;
    uncodedParams.passive = 1;

    int rc = ble_gap_ext_disc(BLE_OWN_ADDR_PUBLIC,
                              0, // duration 0 => forever
                              0, // period
                              0, // no duplicate filtering
                              BLE_HCI_SCAN_FILT_NO_WL,
                              0, // not limited
                              &uncodedParams, NULL, onGapEvent, this);
#else
    struct ble_gap_disc_params scanParams = {};
    scanParams.passive = 1; // Passive scan to save power
    scanParams.itvl = BLE_MESH_SCAN_INTERVAL;
    scanParams.window = BLE_MESH_SCAN_WINDOW;
    scanParams.filter_duplicates = 0; // We want to see repeated mesh advertisements
    scanParams.limited = 0;
    scanParams.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &scanParams, onGapEvent, this);
#endif
    if (rc == 0) {
        LOG_DEBUG("BLE mesh scanning started");
    } else if (rc == BLE_HS_EALREADY) {
        LOG_DEBUG("BLE mesh scanning already active");
    } else {
        LOG_WARN("BLE mesh scan start failed: %d", rc);
    }
}

void ESP32BLEMesh::stopScanning()
{
    if (!bluetoothReady)
        return;

    ble_gap_disc_cancel();
}

int ESP32BLEMesh::onGapEvent(struct ble_gap_event *event, void *arg)
{
    auto *self = static_cast<ESP32BLEMesh *>(arg);
    if (!self)
        return 0;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        self->handleAdvertisement(&event->disc);
        break;
#if MYNEWT_VAL(BLE_EXT_ADV)
    case BLE_GAP_EVENT_EXT_DISC:
        self->handleExtendedAdvertisement(&event->ext_disc);
        break;
#endif
    case BLE_GAP_EVENT_DISC_COMPLETE:
        // Scanning timed out or was stopped - restart if still running
        if (self->isRunning) {
            self->startScanning();
        }
        break;
    default:
        break;
    }

    return 0;
}

void ESP32BLEMesh::handleAdvertisement(const struct ble_gap_disc_desc *desc)
{
    if (!isRunning || !desc)
        return;

    handleAdvertisementData(desc->addr, desc->rssi, desc->data, desc->length_data);
}

#if MYNEWT_VAL(BLE_EXT_ADV)
void ESP32BLEMesh::handleExtendedAdvertisement(const struct ble_gap_ext_disc_desc *desc)
{
    if (!isRunning || !desc)
        return;

    if (desc->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE || !desc->data)
        return;

    handleAdvertisementData(desc->addr, desc->rssi, desc->data, desc->length_data);
}
#endif

void ESP32BLEMesh::handleAdvertisementData(const ble_addr_t &addr, int8_t rssi, const uint8_t *data, uint8_t len)
{
    if (!isRunning || !data)
        return;

    // Parse advertisement data to find manufacturer-specific data
    uint16_t offset = 0;

    while (offset < len) {
        uint8_t adLen = data[offset];
        if (adLen == 0 || offset + adLen >= len)
            break;

        uint8_t adType = data[offset + 1];
        if (adType == BLE_HS_ADV_TYPE_MFG_DATA && adLen >= 4) {
            // Check company ID
            uint16_t companyId = data[offset + 2] | (data[offset + 3] << 8);
            if (companyId == BLE_MESH_COMPANY_ID) {
                // Check protocol version
                uint8_t version = data[offset + 4];
                if (version == BLE_MESH_PROTOCOL_VERSION) {
                    // Extract protobuf payload (skip company ID + version = 3 bytes)
                    const uint8_t *payload = &data[offset + 5];
                    size_t payloadLen = adLen - 4; // adLen includes type byte, subtract type + company(2) + version(1)

                    LOG_DEBUG("BLE mesh RX: rssi=%d, len=%u", rssi, payloadLen);
                    updatePeer(addr, rssi);
                    deliverToRouter(payload, payloadLen);
                    return;
                }
            }
        }
        offset += adLen + 1;
    }
}

void ESP32BLEMesh::updatePeer(const ble_addr_t &addr, int8_t rssi)
{
    uint32_t now = millis();

    // Check if peer already known
    for (uint8_t i = 0; i < peerCount; i++) {
        if (memcmp(&peers[i].addr, &addr, sizeof(ble_addr_t)) == 0) {
            peers[i].rssi = rssi;
            peers[i].lastSeenMs = now;
            return;
        }
    }

    // Add new peer
    if (peerCount < BLE_MESH_MAX_PEERS) {
        peers[peerCount].addr = addr;
        peers[peerCount].rssi = rssi;
        peers[peerCount].lastSeenMs = now;
        peers[peerCount].nodeNum = 0;
        peerCount++;
        LOG_DEBUG("BLE mesh new peer (%u total)", peerCount);
    } else {
        // Replace oldest peer
        pruneStale();
        if (peerCount < BLE_MESH_MAX_PEERS) {
            peers[peerCount].addr = addr;
            peers[peerCount].rssi = rssi;
            peers[peerCount].lastSeenMs = now;
            peers[peerCount].nodeNum = 0;
            peerCount++;
        }
    }
}

void ESP32BLEMesh::pruneStale()
{
    uint32_t now = millis();
    uint8_t writeIdx = 0;

    for (uint8_t i = 0; i < peerCount; i++) {
        if (now - peers[i].lastSeenMs < BLE_MESH_PEER_TIMEOUT_MS) {
            if (writeIdx != i)
                peers[writeIdx] = peers[i];
            writeIdx++;
        }
    }
    peerCount = writeIdx;
}

#endif // HAS_BLE_MESH && ARCH_ESP32
