#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <bsec2.h>

#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000) // That's 6 hours worth of millis()

#include "bme680_iaq_33v_3s_4d/bsec_iaq.h"

class BME680Sensor : public TelemetrySensor
{
  private:
    Bsec2 bme680;

  protected:
    virtual void setup() override;
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
    void checkStatus(String functionName);

  public:
    BME680Sensor();
    int32_t runTrigger();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};