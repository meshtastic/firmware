#include "JapanTxHook.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include <algorithm>

JapanTxHook *japanTxHook = nullptr;

void initJapanTxHook()
{
    if (!japanTxHook)
        japanTxHook = new JapanTxHook();
}

uint32_t getTxPauseDurationMs()
{
    return JapanTxHook::getTxPauseDurationMs();
}

JapanTxHook::JapanTxHook() : RadioTxHook()
{
    if (!japanTxHook)
        japanTxHook = this;
}

JapanTxHook::~JapanTxHook()
{
    if (japanTxHook == this)
        japanTxHook = nullptr;
}

bool JapanTxHook::isJapanRegion()
{
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_JP)
        return true;
    if (myRegion && myRegion->code == meshtastic_Config_LoRaConfig_RegionCode_JP)
        return true;
    return false;
}

uint32_t JapanTxHook::getTxPauseDurationMs()
{
    return isJapanRegion() ? INTER_TX_PAUSE_MS : 0;
}

uint32_t JapanTxHook::getTxPauseDurationMs(meshtastic_Config_LoRaConfig_RegionCode region)
{
    return (region == meshtastic_Config_LoRaConfig_RegionCode_JP) ? INTER_TX_PAUSE_MS : 0;
}

uint32_t JapanTxHook::computeBackoffMs(uint32_t count)
{
    if (count == 0)
        return 0;
    uint32_t shift = (count > 8) ? 8 : count;
    uint32_t maxBackoff = std::min((uint32_t)(BACKOFF_BASE_MS * (1u << shift)), BACKOFF_MAX_MS);
    uint32_t minBackoff = maxBackoff / 2;
    return (uint32_t)random(minBackoff, maxBackoff + 1);
}

bool JapanTxHook::performCarrierSense(RadioInterface *iface)
{
    if (!iface)
        return true;
    const uint32_t start = Time::getMillis();
    while (!Throttle::hasElapsed(start, CARRIER_SENSE_TIME_MS)) {
        int16_t rssi = iface->getCurrentRSSI();
        if (isValidRssi(rssi) && rssi >= CARRIER_SENSE_THRESHOLD_DBM) {
            LOG_DEBUG("JP LBT: carrier sensed during 5ms window (RSSI %d dBm >= %d dBm)", rssi, CARRIER_SENSE_THRESHOLD_DBM);
            return false;
        }
        delay(1);
#ifdef PIO_UNIT_TESTING
        if (Time::useTestClock.load(std::memory_order_relaxed))
            Time::advanceTestMillis(1);
#endif
    }
    int16_t finalRssi = iface->getCurrentRSSI();
    if (isValidRssi(finalRssi) && finalRssi >= CARRIER_SENSE_THRESHOLD_DBM) {
        LOG_DEBUG("JP LBT: carrier sensed at end of 5ms window (RSSI %d dBm >= %d dBm)", finalRssi, CARRIER_SENSE_THRESHOLD_DBM);
        return false;
    }
    return true;
}

RadioTxHook::PreTxAction JapanTxHook::beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    if (!isJapanRegion() || !p)
        return PRETX_SEND;

    const uint32_t pauseMs = getTxPauseDurationMs();
    if (lastTxEndTime != 0 && !Throttle::hasElapsed(lastTxEndTime, pauseMs)) {
        uint32_t deadline = lastTxEndTime + pauseMs;
        if (deadline == 0)
            deadline = 1;
        if (!p->tx_after || !Throttle::deadlinePassedAt(p->tx_after, deadline))
            p->tx_after = deadline;
        const uint32_t now = Time::getMillis();
        LOG_DEBUG("JP LBT: deferring packet 0x%08x for mandatory 50ms pause (remaining %ums)", p->id,
                  deadline > now ? deadline - now : 0);
        return PRETX_DEFER;
    }

    if (!performCarrierSense(iface)) {
        busyCount++;
        const uint32_t backoffMs = computeBackoffMs(busyCount);
        uint32_t deadline = Time::getMillis() + backoffMs;
        if (deadline == 0)
            deadline = 1;
        if (!p->tx_after || !Throttle::deadlinePassedAt(p->tx_after, deadline))
            p->tx_after = deadline;
        LOG_DEBUG("JP LBT: channel busy (attempt %u), backing off %ums for packet 0x%08x", busyCount, backoffMs, p->id);
        return PRETX_DEFER;
    }

    busyCount = 0;
    LOG_DEBUG("JP LBT: carrier sense clear (5ms window), transmit permitted for packet 0x%08x", p->id);
    return PRETX_SEND;
}

void JapanTxHook::postTransmit(RadioInterface *iface, const meshtastic_MeshPacket *p)
{
    (void)iface;
    (void)p;
    if (!isJapanRegion())
        return;
    resetBusyCount();
    lastTxEndTime = Time::getMillis();
    if (lastTxEndTime == 0)
        lastTxEndTime = 1;
}

void JapanTxHook::packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p)
{
    (void)iface;
    (void)p;
    resetBusyCount();
}
