#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RTC.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd4x.h>

// Max speed 400kHz
#define SCD4X_I2C_CLOCK_SPEED 100000
#define SCD4X_WARMUP_MS 5000

class SCD4XSensor : public TelemetrySensor
{
  private:
    SensirionI2cScd4x scd4x;
    TwoWire *_bus{};
    uint8_t _address{};

    bool performFRC(uint32_t targetCO2);
    bool setASCBaseline(uint32_t targetCO2);
    bool getASC(uint16_t &ascEnabled);
    bool setASC(bool ascEnabled);
    bool setTemperature(float tempReference);
    bool getAltitude(uint16_t &altitude);
    bool setAltitude(uint32_t altitude);
    bool getAmbientPressure(uint32_t &ambientPressure);
    bool setAmbientPressure(uint32_t ambientPressure);
    bool factoryReset();
    bool setPowerMode(bool _lowPower);
    bool startMeasurement();
    bool stopMeasurement();

    uint16_t ascActive = 1;
    // low power measurement mode (on sensirion side). Disables sleep mode
    // Improvement and testing needed for timings
    bool lowPower = true;
    uint32_t co2MeasureStarted = 0;

  public:
    SCD4XSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

    enum SCD4XState { SCD4X_OFF, SCD4X_IDLE, SCD4X_MEASUREMENT };
    SCD4XState state = SCD4X_OFF;
    SCD4xSensorVariant sensorVariant{};

    virtual bool isActive() override;

    virtual void sleep() override;      // Stops measurement (measurement -> idle)
    virtual uint32_t wakeUp() override; // Starts measurement (idle -> measurement)
    bool powerDown();                   // Powers down sensor (idle -> power-off)
    bool powerUp();                     // Powers the sensor (power-off -> idle)
    virtual bool canSleep() override;
    virtual int32_t wakeUpTimeMs() override;
    virtual int32_t pendingForReadyMs() override;
    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif