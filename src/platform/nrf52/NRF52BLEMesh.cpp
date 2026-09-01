#include "configuration.h"

#if HAS_BLE_MESH && defined(ARCH_NRF52)

#include "NRF52BLEMesh.h"
#include "NRF52Bluetooth.h"
#include "main.h"
#include "mesh/Router.h"
#include <bluefruit.h>

static_assert(BLE_MESH_ADV_TOTAL_MAX <= BLE_GAP_ADV_SET_DATA_SIZE_EXTENDED_MAX_SUPPORTED,
              "advertisement budget must fit the SoftDevice's extended adv data limit");

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
    bluetoothReady = false;
    isRunning = true;
    LOG_INFO("BLE mesh started (waiting for Bluetooth ready)");
}

void NRF52BLEMesh::onBluetoothReady()
{
    if (!isRunning || bluetoothReady)
        return;

    bluetoothReady = true;
    Bluefruit.setEventCallback(onBleEvent);
    startScanning();
    LOG_DEBUG("BLE mesh Bluetooth ready");
}

void NRF52BLEMesh::stop()
{
    if (!isRunning)
        return;

    platformEndAdvertising();
    stopScanning();
    isRunning = false;
    LOG_INFO("BLE mesh stopped");
}

bool NRF52BLEMesh::platformBeginAdvertising(const uint8_t *adv, size_t len)
{
    if (len > sizeof(advBuf))
        return false;

    // Copy into our own storage: sd_ble_gap_adv_set_configure retains the pointer rather than
    // copying, so the caller's buffer must not be the one the SoftDevice reads from.
    memcpy(advBuf, adv, len);
    advBufLen = (uint8_t)len;

    ble_gap_adv_data_t gapAdvData = {};
    gapAdvData.adv_data.p_data = advBuf;
    gapAdvData.adv_data.len = advBufLen;
    gapAdvData.scan_rsp_data.p_data = NULL;
    gapAdvData.scan_rsp_data.len = 0;

    ble_gap_adv_params_t advParams = {};
    advParams.properties.type = BLE_GAP_ADV_TYPE_EXTENDED_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED;
    advParams.p_peer_addr = NULL;
    advParams.filter_policy = BLE_GAP_ADV_FP_ANY;
    advParams.interval = BLE_MESH_ADV_INTERVAL;
    advParams.duration = 0; // bounded by max_adv_evts, not by time
    advParams.primary_phy = BLE_GAP_PHY_1MBPS;
    advParams.secondary_phy = BLE_GAP_PHY_1MBPS;
    advParams.max_adv_evts = BLE_MESH_ADV_EVENTS;

    if (!ownsDedicatedSet && advHandle == BLE_GAP_ADV_SET_HANDLE_NOT_SET) {
        // First attempt: ask the SoftDevice for a set of our own.
        uint8_t handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
        uint32_t err = sd_ble_gap_adv_set_configure(&handle, &gapAdvData, &advParams);
        if (err == NRF_SUCCESS) {
            advHandle = handle;
            ownsDedicatedSet = true;
            LOG_INFO("BLE mesh using dedicated adv set %u", advHandle);
        } else {
            // No spare set. Share handle 0 with the phone advertisement, which means suspending it
            // for the length of each burst and restoring it afterwards.
            LOG_WARN("BLE mesh: no spare adv set (0x%x), sharing the phone's", err);
            advHandle = 0;
            ownsDedicatedSet = false;
        }
    }

    if (!ownsDedicatedSet) {
        Bluefruit.Advertising.stop();
        uint32_t err = sd_ble_gap_adv_set_configure(&advHandle, &gapAdvData, &advParams);
        if (err != NRF_SUCCESS) {
            LOG_WARN("BLE mesh adv configure failed: 0x%x", err);
            if (nrf52Bluetooth)
                nrf52Bluetooth->resumeAdvertising();
            return false;
        }
    } else {
        uint32_t err = sd_ble_gap_adv_set_configure(&advHandle, &gapAdvData, &advParams);
        if (err != NRF_SUCCESS) {
            LOG_WARN("BLE mesh adv reconfigure failed: 0x%x", err);
            return false;
        }
    }

    uint32_t err = sd_ble_gap_adv_start(advHandle, BLE_CONN_CFG_TAG_DEFAULT);
    if (err != NRF_SUCCESS) {
        LOG_WARN("BLE mesh adv start failed: 0x%x", err);
        if (!ownsDedicatedSet && nrf52Bluetooth)
            nrf52Bluetooth->resumeAdvertising();
        return false;
    }

    advActive = true;
    return true;
}

bool NRF52BLEMesh::platformAdvertisingActive()
{
    // Cleared by BLE_GAP_EVT_ADV_SET_TERMINATED once max_adv_evts have gone out.
    return advActive;
}

void NRF52BLEMesh::platformEndAdvertising()
{
    if (advHandle != BLE_GAP_ADV_SET_HANDLE_NOT_SET) {
        uint32_t err = sd_ble_gap_adv_stop(advHandle);
        if (err != NRF_SUCCESS && err != NRF_ERROR_INVALID_STATE)
            LOG_WARN("BLE mesh adv stop failed: 0x%x", err);
    }
    advActive = false;

    // Hand the shared set back to the phone.
    if (!ownsDedicatedSet && nrf52Bluetooth)
        nrf52Bluetooth->resumeAdvertising();
}

void NRF52BLEMesh::startScanning()
{
    if (!bluetoothReady)
        return;

    bleMeshScanReportData.len = sizeof(bleMeshScanBuffer);
    bleMeshScanParams.interval = BLE_MESH_SCAN_INTERVAL;
    bleMeshScanParams.window = BLE_MESH_SCAN_WINDOW;

    uint32_t err = sd_ble_gap_scan_start(&bleMeshScanParams, &bleMeshScanReportData);
    if (err == NRF_SUCCESS) {
        LOG_DEBUG("BLE mesh scanning started");
    } else if (err == NRF_ERROR_INVALID_STATE) {
        LOG_DEBUG("BLE mesh scanning already active");
    } else {
        // NRF_ERROR_NOT_SUPPORTED / INVALID_STATE here usually means the central role is not
        // enabled: Bluefruit.begin() defaults to zero central links, and scanning needs one.
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

        // The SoftDevice pauses scanning after each report; hand the buffer back to resume.
        bleMeshScanReportData.len = sizeof(bleMeshScanBuffer);
        uint32_t err = sd_ble_gap_scan_start(NULL, &bleMeshScanReportData);
        if (err != NRF_SUCCESS && err != NRF_ERROR_INVALID_STATE) {
            LOG_WARN("BLE mesh scan resume failed: 0x%x", err);
        }
        break;
    }
    case BLE_GAP_EVT_ADV_SET_TERMINATED:
        // max_adv_evts reached - the burst for the current frame is done.
        if (event->evt.gap_evt.params.adv_set_terminated.adv_handle == instance->advHandle)
            instance->advActive = false;
        break;
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
    if (!isRunning || !report->data.p_data)
        return;

    const uint8_t *data = report->data.p_data;
    uint16_t len = report->data.len;
    uint16_t offset = 0;

    while (offset + 1 < len) {
        uint8_t adLen = data[offset];
        if (adLen == 0 || offset + adLen >= len)
            break;

        uint8_t adType = data[offset + 1];
        if (adType == BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA && adLen >= 4) {
            uint16_t companyId = data[offset + 2] | (data[offset + 3] << 8);
            if (companyId == BLE_MESH_COMPANY_ID && data[offset + 4] == BLE_MESH_PROTOCOL_VERSION) {
                const uint8_t *payload = &data[offset + 5];
                size_t payloadLen = adLen - 4;

                updatePeer(report->peer_addr, report->rssi);
                deliverToRouter(payload, payloadLen, report->rssi);
                return;
            }
        }
        offset += adLen + 1;
    }
}

void NRF52BLEMesh::updatePeer(const ble_gap_addr_t &addr, int8_t rssi)
{
    uint32_t now = millis();

    for (uint8_t i = 0; i < peerCount; i++) {
        if (memcmp(&peers[i].addr, &addr, sizeof(ble_gap_addr_t)) == 0) {
            peers[i].rssi = rssi;
            peers[i].lastSeenMs = now;
            return;
        }
    }

    if (peerCount >= BLE_MESH_MAX_PEERS)
        pruneStale();

    if (peerCount < BLE_MESH_MAX_PEERS) {
        peers[peerCount].addr = addr;
        peers[peerCount].rssi = rssi;
        peers[peerCount].lastSeenMs = now;
        peers[peerCount].nodeNum = 0; // unknown until we decode a packet from them
        peerCount++;
        LOG_DEBUG("BLE mesh new peer (%u total)", peerCount);
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
