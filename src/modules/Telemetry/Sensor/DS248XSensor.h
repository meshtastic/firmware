#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_DS248x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_DS248x.h>

#ifndef DS248X_I2C_CLOCK_SPEED
#define DS248X_I2C_CLOCK_SPEED 400000
#endif

#define DS18B20_CMD_SKIP_ROM 0xCC
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

#define DS18B20_FAMILY_CODE 0x28
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_MATCH_ROM 0x55
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

#define DS248X_CMD_CHANNEL_SELECT 0xC3
#define DS248X_REG_CHANNEL 0xD2
#define DS248X_CH0 0xF0

typedef enum { DS248X_UNKNOWN = 0, DS248X_DS2484, DS248X_DS2482_800 } ds248x_variant_t;

struct _DS248XData {
    uint8_t rom[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float temperature;
};

struct _DS2482800Data {
    _DS248XData ds248xData[8];
};

class DS248XSensor : public TelemetrySensor
{
  private:
    Adafruit_DS248x ds248x;
    TwoWire *_bus{};
    uint8_t _address{};
    ds248x_variant_t _variant = DS248X_UNKNOWN;
    _DS248XData ds248xData{};
    _DS2482800Data ds2482800Data{};
    void printROM(uint8_t *rom);
    bool isValidROM(uint8_t *rom);
    float readTemperatureROM(uint8_t *rom);
    bool readTemperatureChannel(uint8_t channel);

  public:
    DS248XSensor();
    ds248x_variant_t detectVariant();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif