#include "JapanTxHook.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include <algorithm>

JapanTxHook *japanTxHook = nullptr;

void initJapanTxHook()
{
    bool shouldBeActive = JapanTxHook::isJapanRegion();
    if (shouldBeActive && !japanTxHook)
        japanTxHook = new JapanTxHook();
    else if (!shouldBeActive && japanTxHook) {
        delete japanTxHook;
        japanTxHook = nullptr;
    }
}

JapanTxHook::JapanTxHook() : RadioTxHook() {}

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

uint32_t JapanTxHook::getMaxTxDurationMs()
{
    return isJapanRegion() ? MAX_TX_DURATION_MS : UINT32_MAX;
}

uint32_t JapanTxHook::getMaxTxDurationMs(meshtastic_Config_LoRaConfig_RegionCode region)
{
    return (region == meshtastic_Config_LoRaConfig_RegionCode_JP) ? MAX_TX_DURATION_MS : UINT32_MAX;
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
        return false;
    bool hasValidSample = false;
    const uint32_t start = Time::getMillis();
    while (!Throttle::hasElapsed(start, CARRIER_SENSE_TIME_MS)) {
        int16_t rssi = iface->getCurrentRSSI();
        if (isValidRssi(rssi)) {
            hasValidSample = true;
            rssi -= RSSI_FEM_LNA_OFFSET;
            if (rssi >= CARRIER_SENSE_THRESHOLD_DBM) {
                LOG_DEBUG("JP LBT: carrier sensed during 5ms window (RSSI %d dBm >= %d dBm)", rssi, CARRIER_SENSE_THRESHOLD_DBM);
                return false;
            }
        }
        delayMicroseconds(250);
    }
    if (!hasValidSample) {
        LOG_WARN("JP LBT: no valid RSSI sample obtained during 5ms window");
        return false;
    }
    return true;
}

RadioTxHook::PreTxAction JapanTxHook::beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    if (!isJapanRegion() || !p)
        return PRETX_SEND;

    const uint32_t now = Time::getMillis();
    if (pauseArmed) {
        const uint32_t pauseUntil = lastTxEndTime + getTxPauseDurationMs();
        if (!Throttle::deadlinePassedAt(now, pauseUntil)) {
            LOG_DEBUG("JP LBT: deferring packet 0x%08x for mandatory 50ms pause (remaining %ums)", p->id, pauseUntil - now);
            return PRETX_DEFER;
        }
        pauseArmed = false;
    }

    if (backoffArmed) {
        if (!Throttle::deadlinePassedAt(now, backoffUntilMs)) {
            LOG_DEBUG("JP LBT: deferring packet 0x%08x during LBT backoff (remaining %ums)", p->id, backoffUntilMs - now);
            return PRETX_DEFER;
        }
        backoffArmed = false;
        backoffPacket = nullptr;
    }

    if (!performCarrierSense(iface)) {
        busyCount++;
        const uint32_t backoffMs = computeBackoffMs(busyCount);
        backoffUntilMs = Time::getMillis() + backoffMs;
        backoffArmed = true;
        backoffPacket = p;
        LOG_DEBUG("JP LBT: channel busy (attempt %u), backing off %ums for packet 0x%08x", busyCount, backoffMs, p->id);
        return PRETX_DEFER;
    }

    busyCount = 0;
    backoffArmed = false;
    backoffPacket = nullptr;
    LOG_DEBUG("JP LBT: carrier sense clear (5ms window), transmit permitted for packet 0x%08x", p->id);
    return PRETX_SEND;
}

uint32_t JapanTxHook::getDeferDelayMs(uint32_t now) const
{
    if (!isJapanRegion())
        return 0;

    uint32_t remaining = 0;
    if (pauseArmed) {
        const uint32_t pauseUntil = lastTxEndTime + getTxPauseDurationMs();
        if (!Throttle::deadlinePassedAt(now, pauseUntil))
            remaining = pauseUntil - now;
    }
    if (backoffArmed && !Throttle::deadlinePassedAt(now, backoffUntilMs))
        remaining = std::max(remaining, backoffUntilMs - now);
    return remaining;
}

void JapanTxHook::postTransmit(RadioInterface *iface, const meshtastic_MeshPacket *p)
{
    (void)iface;
    (void)p;
    if (!isJapanRegion())
        return;
    busyCount = 0;
    backoffArmed = false;
    backoffPacket = nullptr;
    lastTxEndTime = Time::getMillis();
    pauseArmed = true;
}

void JapanTxHook::packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p)
{
    (void)iface;
    // Compare pointer identity only so cancelling a packet behind txQueue.front() leaves active backoff intact.
    if (p && p == backoffPacket) {
        busyCount = 0;
        backoffArmed = false;
        backoffPacket = nullptr;
    }
}
