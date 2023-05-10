#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <bsec2.h>

#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000) // That's 6 hours worth of millis()

#include "config/Default_H2S_NonH2S/Default_H2S_NonH2S.h"

class BME680Sensor : virtual public TelemetrySensor
{
  private:
    Bsec2 bme680;

  protected:
    virtual void setup() override;
    const char *bsecConfigFileName = "/prefs/bsec.dat";
    uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
    uint8_t accuracy = 0;
    uint16_t stateUpdateCounter = 0;
    bsec_virtual_sensor_t sensorList[13] = {BSEC_OUTPUT_IAQ,
                                            BSEC_OUTPUT_STATIC_IAQ,
                                            BSEC_OUTPUT_CO2_EQUIVALENT,
                                            BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
                                            BSEC_OUTPUT_RAW_TEMPERATURE,
                                            BSEC_OUTPUT_RAW_PRESSURE,
                                            BSEC_OUTPUT_RAW_HUMIDITY,
                                            BSEC_OUTPUT_RAW_GAS,
                                            BSEC_OUTPUT_STABILIZATION_STATUS,
                                            BSEC_OUTPUT_RUN_IN_STATUS,
                                            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
                                            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
                                            BSEC_OUTPUT_GAS_PERCENTAGE};
    void loadState();
    void updateState();

  public:
    BME680Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};