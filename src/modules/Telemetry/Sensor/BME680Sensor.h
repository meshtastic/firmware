#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BME680.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BME680IaqEstimator.h"
#include "TelemetrySensor.h"

#include <Adafruit_BME680.h>
#include <memory>

class BME680Sensor : public TelemetrySensor
{
  private:
    using BME680Ptr = std::unique_ptr<Adafruit_BME680>;

    static BME680Ptr makeBME680(TwoWire *bus) { return BME680Ptr(new Adafruit_BME680(bus)); }

    BME680Ptr bme680;
    BME680IaqEstimator iaqEstimator;

    static constexpr uint32_t SAMPLE_INTERVAL_MS = 60 * 1000;
    // getMetrics() publishes the cached async sample only while it is this
    // fresh; a failed refresh past this age drops the BME680 fields from the
    // packet rather than freezing the last reading on the wire
    static constexpr uint32_t SAMPLE_FRESH_MS = 2 * 60 * 1000;
    // A heater-unstable cycle reports gas_resistance 0; carry the previous IAQ
    // through such blips, but not forever
    static constexpr uint32_t IAQ_CARRY_MS = 10 * 60 * 1000;
    static constexpr uint32_t STATE_SAVE_PERIOD_MS = 6 * 60 * 60 * 1000;
    static constexpr uint32_t STATE_SAVE_PERIOD_SECS = STATE_SAVE_PERIOD_MS / 1000;

    static constexpr const char *stateFileName = "/prefs/bme680.dat";
    static constexpr const char *legacyBsecStateFileName = "/prefs/bsec.dat"; // left behind by pre-open-IAQ firmware

    // Async sampling state (driven from runOnce)
    bool readingInFlight = false;
    uint32_t readingDoneAtMs = 0;

    // Cached last sample
    bool haveSample = false;
    uint32_t lastSampleMs = 0;
    float lastTemperature = 0;
    float lastHumidity = 0;
    float lastPressureHPa = 0;
    float lastGasOhms = 0;
    uint16_t lastIaq = 0;
    bool lastIaqValid = false;
    uint32_t lastIaqMs = 0;

    // Persistence bookkeeping: burn-in progress is saved whenever it advances
    // (bounded to ~33 writes lifetime), steady-state saves are RTC-gated so a
    // deep-sleeping node doesn't rewrite flash on every wake
    uint32_t lastPersistedSampleCount = UINT32_MAX;
    uint32_t lastPersistedWarmup = UINT32_MAX;
    uint32_t lastSaveEpochSecs = 0;
    uint32_t lastStateSaveMs = 0;

    void captureSample();
    void loadState();
    void maybeSaveState();
    void saveState();

  public:
    BME680Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
