#include "configuration.h"

#include "BME680Generic.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(BME680_HEADER)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

#if defined(ARCH_PORTDUINO)
#include <Adafruit_BME680.h>
#include <memory>
#else // defined(ARCH_PORTDUINO)
#include <bme68xLibrary.h>
#include <bsec2.h>
#endif // defined(ARCH_PORTDUINO)

#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000) // That's 6 hours worth of millis()

#if !defined(ARCH_PORTDUINO)
const uint8_t bsec_config[] = {
#include "config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.txt"
};
#endif // !defined(ARCH_PORTDUINO)

class BME680Sensor : public TelemetrySensor
{
  private:
#if defined(ARCH_PORTDUINO)
    using BME680Ptr = std::unique_ptr<Adafruit_BME680>;

    static BME680Ptr makeBME680(TwoWire *bus) { return std::make_unique<Adafruit_BME680>(bus); }

    BME680Ptr bme680;
#else
    Bsec2 bme680;
#endif // defined(ARCH_PORTDUINO)

  protected:
#if !defined(ARCH_PORTDUINO)
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
#endif // !defined(ARCH_PORTDUINO)

  public:
    BME680Sensor();
#if !defined(ARCH_PORTDUINO)
    virtual int32_t runOnce() override;
#endif // !defined(ARCH_PORTDUINO)
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif