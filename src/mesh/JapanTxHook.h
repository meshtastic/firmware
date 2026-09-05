#pragma once

#include "MeshRadio.h"
#include "MeshTypes.h"
#include "RadioTxHook.h"
#include "configuration.h"

class RadioInterface;

/**
 * RadioTxHook implementing ARIB STD-T108 compliance for Japan (JP) region.
 * Enforces:
 *  1. Continuous RSSI carrier sensing for >= 5 ms at -80 dBm threshold before transmission,
 *     with exponential backoff (500 ms - 4000 ms) when busy.
 *  2. Minimum 50 ms inter-transmission pause between consecutive transmissions.
 *  3. Complete isolation for non-JP regions (zero delay, zero RSSI gating).
 */
class JapanTxHook : public RadioTxHook
{
  public:
    static constexpr int16_t CARRIER_SENSE_THRESHOLD_DBM = -80;
    static constexpr uint32_t CARRIER_SENSE_TIME_MS = 5;
    static constexpr uint32_t INTER_TX_PAUSE_MS = 50;
    static constexpr uint32_t BACKOFF_BASE_MS = 500;
    static constexpr uint32_t BACKOFF_MAX_MS = 4000;
    static constexpr int16_t RSSI_UNAVAILABLE = 0;
    // Lower bound accommodates SX126x (-141 dBm), SX127x (-164 dBm HF offset), and LR11x0, above SPI errors (< -500).
    static constexpr int16_t RSSI_VALID_MIN = -192;
    static constexpr int16_t RSSI_MIN_VALID_DBM = RSSI_VALID_MIN;
    static constexpr int16_t RSSI_INVALID_DRIVER_ERROR = -706;

    JapanTxHook();
    virtual ~JapanTxHook();

    PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p) override;
    void postTransmit(RadioInterface *iface, const meshtastic_MeshPacket *p) override;
    void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p) override;

    static uint32_t getTxPauseDurationMs();
    static uint32_t getTxPauseDurationMs(meshtastic_Config_LoRaConfig_RegionCode region);
    static bool isJapanRegion();
    static uint32_t computeBackoffMs(uint32_t count);

    // Valid RSSI must be strictly negative (< 0 rejects 0 fallback and positive saturation) and >= -192 dBm.
    static bool isValidRssi(int16_t rssi) { return rssi < 0 && rssi >= RSSI_VALID_MIN; }

    bool performCarrierSense(RadioInterface *iface);

    uint32_t getLastTxEndTime() const { return lastTxEndTime; }
    void setLastTxEndTime(uint32_t t) { lastTxEndTime = t; }
    uint32_t getBusyCount() const { return busyCount; }
    void resetBusyCount() { busyCount = 0; }
    void reset()
    {
        lastTxEndTime = 0;
        busyCount = 0;
    }

  private:
    uint32_t lastTxEndTime = 0;
    uint32_t busyCount = 0;
};

extern JapanTxHook *japanTxHook;
void initJapanTxHook();
uint32_t getTxPauseDurationMs();
