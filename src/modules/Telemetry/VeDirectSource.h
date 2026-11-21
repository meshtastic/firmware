#pragma once

#include "configuration.h"

// Decoded VE.Direct readings go out through PowerTelemetryModule, so decoding is built only where that module is
#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_POWER_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#define HAS_VEDIRECT_TELEMETRY 1
#else
#define HAS_VEDIRECT_TELEMETRY 0
#endif

#if HAS_VEDIRECT_TELEMETRY

#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <cstdint>

/**
 * Decoder for the Victron VE.Direct text protocol. SerialModule feeds it the UART bytes in VE_DIRECT mode and
 * PowerTelemetryModule publishes the result as PowerMetrics: ch1 = battery, ch2 = solar panel, ch3 = load output.
 */
class VeDirectSource
{
  public:
    enum class FeedResult : uint8_t { PENDING, BLOCK_VALID, BLOCK_CORRUPT };

    // No valid block for this long means the link is down and the readings are withheld
    static constexpr uint32_t STALE_AFTER_MS = 30 * 1000;

    /// Consume one received byte
    FeedResult feed(uint8_t c);

    /// Set by SerialModule once it is decoding; makes this the node's source of power metrics
    void setActive() { active = true; }
    bool isActive() const { return active; }

    /// True while the last valid block is younger than STALE_AFTER_MS. Latches false and drops the readings,
    /// so a long-dead link cannot read as fresh again when the millisecond clock wraps.
    bool isFresh();

    /// Fill m->variant.power_metrics from the latest readings; false when there are none to report
    bool getMetrics(meshtastic_Telemetry *m);

  private:
    // Not IDLE / HEX: Arduino cores define those names as macros
    enum class ParseState : uint8_t { WAIT_LINE, LABEL, VALUE, CHECKSUM, HEX_FRAME };

    struct Readings {
        float battVoltage;  // V
        float battCurrent;  // mA, negative = discharge
        float panelVoltage; // V
        float panelPower;   // W
        float loadCurrent;  // mA
        bool hasBattVoltage;
        bool hasBattCurrent;
        bool hasPanelVoltage;
        bool hasPanelPower;
        bool hasLoadCurrent;
    };

    void stageField();
    void commitBlock();

    ParseState parseState = ParseState::WAIT_LINE;
    uint8_t blockSum = 0;
    char label[9] = "";
    char value[34] = "";
    uint8_t labelLen = 0;
    uint8_t valueLen = 0;
    bool cleanBlockStart = false; // false while joining mid-block: that block's checksum failure is not an error
    Readings staged = {};
    Readings published = {};
    uint32_t lastValidBlockMsec = 0; // 0 = no valid block yet, or the link expired
    bool active = false;
};

extern VeDirectSource veDirectSource;

#endif
