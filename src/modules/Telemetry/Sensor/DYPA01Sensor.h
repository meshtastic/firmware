#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#pragma once

#include "TelemetrySensor.h"
#if defined(HAS_DYP_A01)
#include "DYPA01Protocol.h"
#endif

class DYPA01Sensor : public TelemetrySensor
{
  public:
    DYPA01Sensor();
    bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    int32_t runOnce() override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;

#if defined(HAS_DYP_A01)
  private:
    enum class Phase : uint8_t { Idle, Boot, PostTrigger, Read, Gap, Retry };

    void openUart();
    void enablePeripheralPower();
    void flushInput();
    int32_t armTrigger();
    int32_t idleWait();
    bool burstIsDue();
    bool takeSampleIfReady();
    int32_t finishBurst();
    int32_t runControlled();
    int32_t runAuto();
    uint32_t telemetryIntervalMs() const;

    DypA01Parser parser;
    Phase phase = Phase::Idle;
    float samples[DYP_A01_BURST_SAMPLES] = {};
    float lastDistanceMm = 0;
    uint32_t bootStartedMs = 0;
    uint32_t triggerSentMs = 0;
    uint32_t readWindowStartMs = 0;
    uint32_t retryStartedMs = 0;
    uint32_t lastBurstDoneMs = 0;
    uint32_t lastValidFrameMs = 0;
    uint32_t lastRejectLogMs = 0;
    uint32_t lastNoMetricsLogMs = 0;
    uint8_t validSamples = 0;
    uint8_t triggersThisBurst = 0;
    uint8_t attempt = 1;
    bool hasValidReading = false;
    bool burstCompleted = false;
    bool capturedThisTrigger = false;
    bool peripheralPowerOn = false;
#endif // HAS_DYP_A01
};

#endif
