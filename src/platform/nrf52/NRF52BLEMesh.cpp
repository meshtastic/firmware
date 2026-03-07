#include "configuration.h"

#if HAS_BLE_MESH && defined(ARCH_NRF52)

#include "NRF52BLEMesh.h"
#include "main.h"
#include "mesh/Router.h"
#include <bluefruit.h>

NRF52BLEMesh *NRF52BLEMesh::instance = nullptr;

static uint8_t bleMeshScanBuffer[BLE_GAP_SCAN_BUFFER_EXTENDED_MAX_SUPPORTED];
static ble_data_t bleMeshScanReportData = {.p_data = bleMeshScanBuffer, .len = sizeof(bleMeshScanBuffer)};
static ble_gap_scan_params_t bleMeshScanParams = {
    .extended = 1,
    .report_incomplete_evts = 0,
    .active = 0,
    .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,
    .scan_phys = BLE_GAP_PHY_1MBPS,
    .interval = BLE_MESH_SCAN_INTERVAL,
    .window = BLE_MESH_SCAN_WINDOW,
    .timeout = 0,
    .channel_mask = {0, 0, 0, 0, 0},
};

void NRF52BLEMesh::start()
{
    if (isRunning) {
        LOG_DEBUG("BLE mesh already running");
        return;
    }

    instance = this;
    memset(peers, 0, sizeof(peers));
    peerCount = 0;

    isRunning = true;
    LOG_INFO("BLE mesh started");
}

void NRF52BLEMesh::onBluetoothReady()
{
    if (!isRunning)
        return;

    LOG_DEBUG("BLE mesh Bluetooth ready");
    Bluefruit.setEventCallback(onBleEvent);
    startScanning();
}

void NRF52BLEMesh::stop()
{
    if (!isRunning)
        return;

    stopScanning();
    isRunning = false;
    LOG_INFO("BLE mesh stopped");
}

bool NRF52BLEMesh::onSend(const meshtastic_MeshPacket *mp)
{
    if (!isRunning || !mp)
        return false;

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

    // Stop scanning during mesh advertisement burst
    stopScanning();

    static uint8_t adv_buf[BLE_GAP_ADV_SET_DATA_SIZE_EXTENDED_MAX_SUPPORTED];

    size_t adv_len = 0;
    adv_buf[adv_len++] = 2;
    adv_buf[adv_len++] = BLE_GAP_AD_TYPE_FLAGS;
    adv_buf[adv_len++] = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    adv_buf[adv_len++] = (uint8_t)(mfgDataLen + 1);
    adv_buf[adv_len++] = BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA;
    memcpy(&adv_buf[adv_len], mfgData, mfgDataLen);
    adv_len += mfgDataLen;

    Bluefruit.Advertising.stop();
    delay(BLE_MESH_ADV_SWITCH_SETTLE_MS);

    const bool priorityRetries = shouldUsePriorityRetries(mp);
    const uint8_t extraRetries = priorityRetries ? BLE_MESH_PRIORITY_EXTRA_RETRIES : 0;
    bool sentAny = false;

    for (uint8_t attempt = 0; attempt <= extraRetries; ++attempt) {
        if (attempt > 0) {
            const uint16_t retryDelayMs =
                random(BLE_MESH_PRIORITY_RETRY_JITTER_MIN_MS, BLE_MESH_PRIORITY_RETRY_JITTER_MAX_MS + 1);
            delay(retryDelayMs);
        }

        if (sendAdvertisementBurst(adv_buf, adv_len, mp->id, encodedLen)) {
            sentAny = true;
        }
    }

    // Restore phone advertising through the normal Bluefruit setup path.
    if (config.bluetooth.enabled && nrf52Bluetooth) {
        nrf52Bluetooth->resumeAdvertising();
    }

    // Resume scanning
    startScanning();

    return sentAny;
}

bool NRF52BLEMesh::sendAdvertisementBurst(const uint8_t *advBuf, size_t advLen, PacketId packetId, size_t encodedLen)
{
    static ble_gap_adv_data_t gapAdvData;
    static ble_gap_adv_params_t advParams;

    gapAdvData.adv_data.p_data = const_cast<uint8_t *>(advBuf);
    gapAdvData.adv_data.len = advLen;
    gapAdvData.scan_rsp_data.p_data = NULL;
    gapAdvData.scan_rsp_data.len = 0;

    memset(&advParams, 0, sizeof(advParams));
    advParams.properties.type = BLE_GAP_ADV_TYPE_EXTENDED_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED;
    advParams.p_peer_addr = NULL;
    advParams.filter_policy = BLE_GAP_ADV_FP_ANY;
    advParams.interval = BLE_MESH_ADV_INTERVAL;
    advParams.duration = 0;
    advParams.primary_phy = BLE_GAP_PHY_1MBPS;
    advParams.secondary_phy = BLE_GAP_PHY_1MBPS;
    advParams.max_adv_evts = BLE_MESH_ADV_COUNT;

    uint8_t meshAdvHandle = 0;
    uint32_t stopErr = sd_ble_gap_adv_stop(meshAdvHandle);
    if (stopErr != NRF_SUCCESS && stopErr != NRF_ERROR_INVALID_STATE) {
        LOG_WARN("BLE mesh adv stop failed: 0x%x", stopErr);
    }
    delay(BLE_MESH_ADV_SWITCH_SETTLE_MS);

    uint32_t err = sd_ble_gap_adv_set_configure(&meshAdvHandle, &gapAdvData, &advParams);
    if (err != NRF_SUCCESS) {
        LOG_WARN("BLE mesh adv configure failed: 0x%x", err);
        return false;
    }

    err = sd_ble_gap_adv_start(meshAdvHandle, BLE_CONN_CFG_TAG_DEFAULT);
    if (err != NRF_SUCCESS) {
        LOG_WARN("BLE mesh adv start failed: 0x%x", err);
        return false;
    }

    LOG_DEBUG("BLE mesh adv sent (id=%u, len=%u)", packetId, encodedLen);
    delay(BLE_MESH_ADV_COUNT * 10 + BLE_MESH_ADV_BURST_GUARD_MS);

    stopErr = sd_ble_gap_adv_stop(meshAdvHandle);
    if (stopErr != NRF_SUCCESS && stopErr != NRF_ERROR_INVALID_STATE) {
        LOG_WARN("BLE mesh adv stop after send failed: 0x%x", stopErr);
    }
    delay(BLE_MESH_ADV_SWITCH_SETTLE_MS);
    return true;
}

bool NRF52BLEMesh::shouldUsePriorityRetries(const meshtastic_MeshPacket *mp) const
{
    if (!mp)
        return false;

    // Prioritize retries for ack-sensitive traffic and direct messages only.
    const bool isDirect = (mp->to != 0) && !isBroadcast(mp->to);
    return mp->want_ack || isDirect;
}

void NRF52BLEMesh::startScanning()
{
    bleMeshScanReportData.len = sizeof(bleMeshScanBuffer);
    bleMeshScanParams.interval = BLE_MESH_SCAN_INTERVAL;
    bleMeshScanParams.window = BLE_MESH_SCAN_WINDOW;

    uint32_t err = sd_ble_gap_scan_start(&bleMeshScanParams, &bleMeshScanReportData);
    if (err == NRF_SUCCESS) {
        LOG_DEBUG("BLE mesh scanning started");
    } else if (err == NRF_ERROR_INVALID_STATE) {
        LOG_DEBUG("BLE mesh scanning already active");
    } else {
        LOG_WARN("BLE mesh scan start failed: 0x%x", err);
    }
}

void NRF52BLEMesh::stopScanning()
{
    uint32_t err = sd_ble_gap_scan_stop();
    if (err != NRF_SUCCESS && err != NRF_ERROR_INVALID_STATE) {
        LOG_WARN("BLE mesh scan stop failed: 0x%x", err);
    }
}

void NRF52BLEMesh::onBleEvent(ble_evt_t *event)
{
    if (!instance || !instance->isRunning || !event)
        return;

    switch (event->header.evt_id) {
    case BLE_GAP_EVT_ADV_REPORT: {
        ble_gap_evt_adv_report_t *report = &event->evt.gap_evt.params.adv_report;

        if (report->type.status == BLE_GAP_ADV_DATA_STATUS_COMPLETE) {
            instance->handleScanResult(report);
        }

        bleMeshScanReportData.len = sizeof(bleMeshScanBuffer);
        uint32_t err = sd_ble_gap_scan_start(NULL, &bleMeshScanReportData);
        if (err != NRF_SUCCESS && err != NRF_ERROR_INVALID_STATE) {
            LOG_WARN("BLE mesh scan resume failed: 0x%x", err);
        }
        break;
    }
    case BLE_GAP_EVT_TIMEOUT:
        if (event->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_SCAN) {
            instance->startScanning();
        }
        break;
    default:
        break;
    }
}

void NRF52BLEMesh::handleScanResult(ble_gap_evt_adv_report_t *report)
{
    if (!isRunning)
        return;

    // Parse advertisement data to find manufacturer-specific data
    uint8_t *data = report->data.p_data;
    uint16_t len = report->data.len;
    uint16_t offset = 0;

    while (offset < len) {
        uint8_t adLen = data[offset];
        if (adLen == 0 || offset + adLen >= len)
            break;

        uint8_t adType = data[offset + 1];
        if (adType == BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA && adLen >= 4) {
            // Check company ID
            uint16_t companyId = data[offset + 2] | (data[offset + 3] << 8);
            if (companyId == BLE_MESH_COMPANY_ID) {
                // Check protocol version
                uint8_t version = data[offset + 4];
                if (version == BLE_MESH_PROTOCOL_VERSION) {
                    // Extract protobuf payload (skip company ID + version = 3 bytes)
                    const uint8_t *payload = &data[offset + 5];
                    size_t payloadLen = adLen - 4; // adLen includes type byte, subtract type + company(2) + version(1)

                    LOG_DEBUG("BLE mesh RX: rssi=%d, len=%u", report->rssi, payloadLen);
                    updatePeer(report->peer_addr, report->rssi);
                    deliverToRouter(payload, payloadLen);
                    return;
                }
            }
        }
        offset += adLen + 1;
    }
}

void NRF52BLEMesh::updatePeer(const ble_gap_addr_t &addr, int8_t rssi)
{
    uint32_t now = millis();

    // Check if peer already known
    for (uint8_t i = 0; i < peerCount; i++) {
        if (memcmp(&peers[i].addr, &addr, sizeof(ble_gap_addr_t)) == 0) {
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
        peers[peerCount].nodeNum = 0; // Unknown until we decode a packet from them
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

void NRF52BLEMesh::pruneStale()
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

#endif // HAS_BLE_MESH && ARCH_NRF52
