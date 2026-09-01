#include "configuration.h"

#if HAS_BLE_MESH && defined(ARCH_ESP32)

#include "ESP32BLEMesh.h"
#include "main.h"
#include "mesh/Router.h"
#include "nimble/NimbleBluetooth.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "nimble/hci_common.h"

#if BLE_MESH_USE_EXT_ADV
static_assert(BLE_MESH_ADV_TOTAL_MAX <= BLE_HCI_MAX_EXT_ADV_DATA_LEN,
              "advertisement budget must fit NimBLE's unfragmented ext-adv data limit");
#endif

void ESP32BLEMesh::start()
{
    if (isRunning) {
        LOG_DEBUG("BLE mesh already running");
        return;
    }

    memset(peers, 0, sizeof(peers));
    peerCount = 0;
    isRunning = true;
    LOG_INFO("BLE mesh started (waiting for Bluetooth ready)");
}

bool ESP32BLEMesh::platformReady()
{
    // Poll rather than wait for a callback, so this does not depend on whether NimbleBluetooth or
    // this handler was constructed first.
    //
    // isActive(), not ble_hs_synced(): the host syncs well before NimbleBluetooth::setup() has
    // registered its service and started advertising, and starting a scan in that window races the
    // stack's own GAP configuration. Wait for the PhoneAPI side to be fully up.
    return nimbleBluetooth && nimbleBluetooth->isActive();
}

void ESP32BLEMesh::onBluetoothReady()
{
    if (!isRunning)
        return;

    startScanning();
    LOG_INFO("BLE mesh Bluetooth ready, scanning");
}

void ESP32BLEMesh::stop()
{
    if (!isRunning)
        return;

    platformEndAdvertising();
    stopScanning();
    isRunning = false;
    LOG_INFO("BLE mesh stopped");
}

#if BLE_MESH_USE_EXT_ADV
bool ESP32BLEMesh::configureAdvInstance()
{
    if (advInstanceConfigured)
        return true;

    struct ble_gap_ext_adv_params params = {};
    // Non-connectable, non-scannable, non-legacy: a pure broadcast. legacy_pdu must stay 0 or we are
    // back to the 31-byte limit, which cannot hold a mesh frame at all.
    params.connectable = 0;
    params.scannable = 0;
    params.directed = 0;
    params.high_duty_directed = 0;
    params.legacy_pdu = 0;
    params.anonymous = 0;
    params.include_tx_power = 0;
    params.scan_req_notif = 0;
    params.itvl_min = BLE_MESH_ADV_INTERVAL;
    params.itvl_max = BLE_MESH_ADV_INTERVAL;
    params.channel_map = 0; // all channels
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.tx_power = 127; // controller picks its maximum
    params.sid = 0;

    int8_t selectedTxPower = 0;
    int rc = ble_gap_ext_adv_configure(BLE_MESH_ADV_INSTANCE, &params, &selectedTxPower, onGapEvent, this);
    if (rc != 0) {
        LOG_WARN("BLE mesh ext adv configure failed: %d", rc);
        return false;
    }

    // Configured once and left in place. Reconfiguring per packet costs a full GAP round trip on
    // every send for no benefit - only the data changes between frames.
    advInstanceConfigured = true;
    return true;
}
#endif

bool ESP32BLEMesh::platformBeginAdvertising(const uint8_t *adv, size_t len)
{
#if BLE_MESH_USE_EXT_ADV
    if (!configureAdvInstance())
        return false;

    struct os_mbuf *advData = os_msys_get_pkthdr(len, 0);
    if (!advData) {
        LOG_WARN("BLE mesh: failed to allocate mbuf");
        return false;
    }
    if (os_mbuf_append(advData, adv, len) != 0) {
        os_mbuf_free_chain(advData);
        return false;
    }

    // set_data takes ownership on success and frees on failure - do not touch advData after this.
    int rc = ble_gap_ext_adv_set_data(BLE_MESH_ADV_INSTANCE, advData);
    if (rc != 0) {
        LOG_WARN("BLE mesh ext adv set data failed: %d", rc);
        return false;
    }

    // (instance, duration, max_events). Bounded by max_events, NOT by duration: duration is in 10ms
    // units, so passing the event count there advertises for 30ms and then stops, which is not what
    // a repeat count means.
    rc = ble_gap_ext_adv_start(BLE_MESH_ADV_INSTANCE, 0, BLE_MESH_ADV_EVENTS);
    if (rc != 0) {
        LOG_WARN("BLE mesh ext adv start failed: %d", rc);
        return false;
    }
    return true;
#else
    // Legacy advertising fallback (ESP32 classic - BLE 4.2, 31 bytes total). A mesh frame is far
    // larger than that, so this path effectively never carries one; it exists so the build is
    // uniform across ESP32 parts rather than because classic ESP32 can join a BLE mesh.
    if (len > 31) {
        LOG_DEBUG("BLE mesh: %u bytes exceeds legacy advertising capacity, not sent", (unsigned)len);
        return false;
    }

    struct ble_gap_adv_params legacyParams = {};
    legacyParams.conn_mode = BLE_GAP_CONN_MODE_NON;
    legacyParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
    legacyParams.itvl_min = BLE_MESH_ADV_INTERVAL;
    legacyParams.itvl_max = BLE_MESH_ADV_INTERVAL;
    legacyParams.channel_map = 0;

    if (ble_gap_adv_set_data(adv, len) != 0)
        return false;
    // Duration in 10ms units; approximate the same burst length the extended path gets.
    return ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_MESH_ADV_EVENTS * 10, &legacyParams, onGapEvent, this) == 0;
#endif
}

bool ESP32BLEMesh::platformAdvertisingActive()
{
#if BLE_MESH_USE_EXT_ADV
    return ble_gap_ext_adv_active(BLE_MESH_ADV_INSTANCE) != 0;
#else
    return ble_gap_adv_active() != 0;
#endif
}

void ESP32BLEMesh::platformEndAdvertising()
{
#if BLE_MESH_USE_EXT_ADV
    // Stop but do NOT remove: the instance stays configured for the next frame.
    ble_gap_ext_adv_stop(BLE_MESH_ADV_INSTANCE);
#else
    ble_gap_adv_stop();
#endif
}

void ESP32BLEMesh::startScanning()
{
    if (!platformReady())
        return;

#ifdef BLE_MESH_TX_ONLY
    // Broadcast-only node: never scan. Also the isolation switch for the ESP32 fault - if the
    // build is stable with this set and boot-loops without it, the fault is in the scan start.
    LOG_INFO("BLE mesh: TX-only build, not scanning");
    return;
#endif

#if BLE_MESH_USE_EXT_ADV
    struct ble_gap_ext_disc_params uncodedParams = {};
    uncodedParams.itvl = BLE_MESH_SCAN_INTERVAL;
    uncodedParams.window = BLE_MESH_SCAN_WINDOW;
    uncodedParams.passive = 1; // never scan-request; the payload is all in the advertisement

    // filter_duplicates MUST stay 0. The controller de-duplicates on advertiser address, not on
    // payload, so enabling it would deliver one report per neighbour and then go silent - every
    // subsequent mesh frame from that node filtered away as a "duplicate" advertisement.
    int rc = ble_gap_ext_disc(BLE_OWN_ADDR_PUBLIC, 0 /* duration: forever */, 0 /* period */, 0 /* filter_duplicates */,
                              BLE_HCI_SCAN_FILT_NO_WL, 0 /* limited */, &uncodedParams, NULL, onGapEvent, this);
#else
    struct ble_gap_disc_params scanParams = {};
    scanParams.passive = 1;
    scanParams.itvl = BLE_MESH_SCAN_INTERVAL;
    scanParams.window = BLE_MESH_SCAN_WINDOW;
    scanParams.filter_duplicates = 0; // see above
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
    if (!platformReady())
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
#if BLE_MESH_USE_EXT_ADV
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

#if BLE_MESH_USE_EXT_ADV
void ESP32BLEMesh::handleExtendedAdvertisement(const struct ble_gap_ext_disc_desc *desc)
{
    if (!isRunning || !desc)
        return;

    // We never chain on send, so anything flagged INCOMPLETE is some other advertiser's.
    if (desc->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE || !desc->data)
        return;

    handleAdvertisementData(desc->addr, desc->rssi, desc->data, desc->length_data);
}
#endif

void ESP32BLEMesh::handleAdvertisementData(const ble_addr_t &addr, int8_t rssi, const uint8_t *data, uint8_t len)
{
    if (!isRunning || !data)
        return;

    // Walk AD structures looking for ours; advertisements routinely carry several.
    uint16_t offset = 0;
    while (offset + 1 < len) {
        uint8_t adLen = data[offset];
        // An AD structure spans data[offset] .. data[offset + adLen]; anything else is truncated.
        if (adLen == 0 || offset + adLen >= len)
            break;

        uint8_t adType = data[offset + 1];
        if (adType == BLE_HS_ADV_TYPE_MFG_DATA && adLen >= 4) {
            uint16_t companyId = data[offset + 2] | (data[offset + 3] << 8);
            if (companyId == BLE_MESH_COMPANY_ID && data[offset + 4] == BLE_MESH_PROTOCOL_VERSION) {
                // Skip the type byte, the 2-byte company ID and the 1-byte version.
                const uint8_t *payload = &data[offset + 5];
                size_t payloadLen = adLen - 4;

                updatePeer(addr, rssi);
                deliverToRouter(payload, payloadLen, rssi);
                return;
            }
        }
        offset += adLen + 1;
    }
}

void ESP32BLEMesh::updatePeer(const ble_addr_t &addr, int8_t rssi)
{
    uint32_t now = millis();

    for (uint8_t i = 0; i < peerCount; i++) {
        if (memcmp(&peers[i].addr, &addr, sizeof(ble_addr_t)) == 0) {
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
        peers[peerCount].nodeNum = 0;
        peerCount++;
        LOG_DEBUG("BLE mesh new peer (%u total)", peerCount);
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
