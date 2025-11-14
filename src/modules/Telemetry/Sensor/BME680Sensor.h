#include "configuration.h"

#include "BME680Generic.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(BME680_HEADER)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

#if BME680_BSEC2_SUPPORTED == 1
#include <bme68xLibrary.h>
#include <bsec2.h>
#else
#include <Adafruit_BME680.h>
#include <memory>
#endif // BME680_BSEC2_SUPPORTED

#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000) // That's 6 hours worth of millis()

#if BME680_BSEC2_SUPPORTED == 1
const uint8_t bsec_config[] = {
#include "config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.txt"
};
#endif // BME680_BSEC2_SUPPORTED
class BME680Sensor : public TelemetrySensor
{
  private:
#if BME680_BSEC2_SUPPORTED == 1
    Bsec2 bme680;
#else
    using BME680Ptr = std::unique_ptr<Adafruit_BME680>;

    static BME680Ptr makeBME680(TwoWire *bus) { return std::make_unique<Adafruit_BME680>(bus); }

    BME680Ptr bme680;
#endif // BME680_BSEC2_SUPPORTED

  protected:
#if BME680_BSEC2_SUPPORTED == 1
    const char *bsecConfigFileName = "/prefs/bsec.dat";
    uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
    uint8_t accuracy = 0;
    uint16_t stateUpdateCounter = 0;
    bsecSensor sensorList[9] = {BSEC_OUTPUT_IAQ,
                                BSEC_OUTPUT_RAW_TEMPERATURE,
                                BSEC_OUTPUT_RAW_PRESSURE,
                                BSEC_OUTPUT_RAW_HUMIDITY,
                                BSEC_OUTPUT_RAW_GAS,
                                BSEC_OUTPUT_STABILIZATION_STATUS,
                                BSEC_OUTPUT_RUN_IN_STATUS,
                                BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
                                BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY};
    void loadState();
    void updateState();
    void checkStatus(const char *functionName);
#endif // BME680_BSEC2_SUPPORTED

  public:
    BME680Sensor();
#if BME680_BSEC2_SUPPORTED == 1
    virtual int32_t runOnce() override;
#endif // BME680_BSEC2_SUPPORTED
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif