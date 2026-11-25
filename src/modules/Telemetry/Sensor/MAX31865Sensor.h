#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(MAX31865_CS) && __has_include(<Adafruit_MAX31865.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_MAX31865.h>

// SPI RTD converter (e.g. RAK12022 PT100 module). Build-time settings:
//   MAX31865_CS        chip select pin (required)
//   MAX31865_SPI       SPI bus, default SPI
//   MAX31865_WIRES     2, 3 or 4, must match the probe wiring / board jumpers (default 4)
//   MAX31865_RNOMINAL  RTD resistance at 0 C in ohms, default 100 (PT100)
//   MAX31865_RREF      reference resistor in ohms, default 400
#ifndef MAX31865_SPI
#define MAX31865_SPI SPI
#endif
#ifndef MAX31865_WIRES
#define MAX31865_WIRES 4
#endif
#ifndef MAX31865_RNOMINAL
#define MAX31865_RNOMINAL 100.0f
#endif
#ifndef MAX31865_RREF
#define MAX31865_RREF 400.0f
#endif

class MAX31865Sensor : public TelemetrySensor
{
  private:
    Adafruit_MAX31865 max31865;
    bool probe();

  public:
    MAX31865Sensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
