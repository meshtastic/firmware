#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "MFRC522_lib/MFRC522_I2C.h"
#include <Adafruit_I2CDevice.h>

class MFRC522Sensor : virtual public TelemetrySensor
{
private:
  // MFRC522_I2C mfrc522(0x28, RST_PIN_MFRC522);	// Create MFRC522 instance.
  MFRC522_I2C mfrc522;
  // Adafruit_I2CDevice *i2c_dev_MFRC522_I2C = NULL;

protected:
  virtual void setup() override;

public:
  MFRC522Sensor();
  virtual int32_t runOnce() override;
  virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};