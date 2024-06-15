#include "MeshModule.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>

class NAU7802Sensor : public TelemetrySensor
{
  private:
    NAU7802 nau7802;

  protected:
    virtual void setup() override;
    const char *nau7802ConfigFileName = "/prefs/nau7802.dat";
    bool saveCalibrationData();
    bool loadCalibrationData();

  public:
    NAU7802Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    void tare();
    void calibrate(float weight);
    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif