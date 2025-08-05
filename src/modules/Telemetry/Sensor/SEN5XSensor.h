#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "Wire.h"
#include "RTC.h"

// Warm up times for SEN5X from the datasheet
#ifndef SEN5X_WARMUP_MS_1
#define SEN5X_WARMUP_MS_1 15000
#endif

#ifndef SEN5X_WARMUP_MS_2
#define SEN5X_WARMUP_MS_2 30000
#endif

#ifndef SEN5X_I2C_CLOCK_SPEED
#define SEN5X_I2C_CLOCK_SPEED 100000
#endif

#define ONE_WEEK_IN_SECONDS 604800

struct _SEN5XMeasurements {
    uint16_t pM1p0;
    uint16_t pM2p5;
    uint16_t pM4p0;
    uint16_t pM10p0;
    uint32_t pN0p5;
    uint32_t pN1p0;
    uint32_t pN2p5;
    uint32_t pN4p0;
    uint32_t pN10p0;
    float tSize;
    float humidity;
    float temperature;
    float vocIndex;
    float noxIndex;
};

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
    // TODO - Remove
    bool continousMode = false;
    bool forcedContinousMode = false;

    bool sendCommand(uint16_t wichCommand);
    bool sendCommand(uint16_t wichCommand, uint8_t* buffer, uint8_t byteNumber=0);
    uint8_t readBuffer(uint8_t* buffer, uint8_t byteNumber); // Return number of bytes received
    uint8_t sen5xCRC(uint8_t* buffer);
    bool I2Cdetect(TwoWire *_Wire, uint8_t address);
    bool restoreClock(uint32_t);
    bool startCleaning();
    uint8_t getMeasurements();
    bool readRawValues();
    bool readPnValues(bool cumulative);
    bool readValues();

    uint32_t measureStarted = 0;
    _SEN5XMeasurements sen5xmeasurement;

  protected:
    // Store status of the sensor in this file
    const char *sen5XStateFileName = "/prefs/sen5X.dat";
    bool loadState();
    bool saveState();

    // Cleaning State
    // Last cleaning status
    uint32_t lastCleaning = 0;
    bool lastCleaningValid = false;

    // TODO - VOC State
    // # define SEN5X_VOC_STATE_BUFFER_SIZE 12
    // uint8_t VOCstate[SEN5X_VOC_STATE_BUFFER_SIZE];
    // struct VOCstateStruct { uint8_t state[SEN5X_VOC_STATE_BUFFER_SIZE]; uint32_t time; bool valid=true; };
    // void loadVOCState();
    // void updateVOCState();

    virtual void setup() override;

  public:

    SEN5XSensor();
    bool isActive();
    uint32_t wakeUp();
    bool idle();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;

    // Sensirion recommends taking a reading after 15 seconds, if the Particle number reading is over 100#/cm3 the reading is OK, but if it is lower wait until 30 seconds and take it again.
    // https://sensirion.com/resource/application_note/low_power_mode/sen5x
    #define SEN5X_PN4P0_CONC_THD 100
    // This value represents the time needed for pending data
    int32_t pendingForReady();
};



#endif