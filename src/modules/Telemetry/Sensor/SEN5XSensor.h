#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "Wire.h"
// #include <SensirionI2CSen5x.h>

#ifndef SEN5X_WARMUP_MS
// from the SEN5X datasheet
#define SEN5X_WARMUP_MS 30000
#endif

class SEN5XSensor : public TelemetrySensor
{
  private:
    TwoWire * bus;
    uint8_t address;

    bool getVersion();
    float firmwareVer = -1;
    float hardwareVer = -1;
    float protocolVer = -1;
    bool findModel();

    // Commands
    #define SEN5X_RESET                        0xD304
    #define SEN5X_GET_PRODUCT_NAME             0xD014
    #define SEN5X_GET_FIRMWARE_VERSION         0xD100
    #define SEN5X_START_MEASUREMENT            0x0021
    #define SEN5X_START_MEASUREMENT_RHT_GAS    0x0037
    #define SEN5X_STOP_MEASUREMENT             0x0104
    #define SEN5X_READ_DATA_READY              0x0202
    #define SEN5X_START_FAN_CLEANING           0x5607
    #define SEN5X_RW_VOCS_STATE                0x6181

    #define SEN5X_READ_VALUES                  0x03C4
    #define SEN5X_READ_RAW_VALUES              0x03D2
    #define SEN5X_READ_PM_VALUES               0x0413

    enum SEN5Xmodel { SEN5X_UNKNOWN = 0, SEN50 = 0b001, SEN54 = 0b010, SEN55 = 0b100 };
    SEN5Xmodel model = SEN5X_UNKNOWN;

    enum SEN5XState { SEN5X_OFF, SEN5X_IDLE, SEN5X_MEASUREMENT, SEN5X_MEASUREMENT_2, SEN5X_CLEANING, SEN5X_NOT_DETECTED };
    SEN5XState state = SEN5X_OFF;

    bool sendCommand(uint16_t wichCommand);
    bool sendCommand(uint16_t wichCommand, uint8_t* buffer, uint8_t byteNumber=0);
    uint8_t readBuffer(uint8_t* buffer, uint8_t byteNumber); // Return number of bytes received
    uint8_t CRC(uint8_t* buffer);
    bool I2Cdetect(TwoWire *_Wire, uint8_t address);

  protected:
    virtual void setup() override;

  public:

// #ifdef SEN5X_ENABLE_PIN
    // void sleep();
    // uint32_t wakeUp();
// #endif

    SEN5XSensor();
    bool isActive();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif