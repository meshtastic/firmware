#pragma once

#include <stddef.h>
#include <stdint.h>

class ScanI2C
{
  public:
    typedef enum DeviceType {
        NONE,
        SCREEN_SSD1306,
        SCREEN_SH1106,
        SCREEN_UNKNOWN, // has the same address as the two above but does not respond to the same commands
        SCREEN_ST7567,
        RTC_RV3028,
        RTC_PCF8563,
        CARDKB,
        TDECKKB,
        BBQ10KB,
        RAK14004,
        PMU_AXP192_AXP2101,
        BME_680,
        BME_280,
        BMP_280,
        BMP_085,
        BMP_3XX,
        INA260,
        INA219,
        INA3221,
        MAX17048,
        MCP9808,
        SHT31,
        SHT4X,
        SHTC3,
        LPS22HB,
        QMC6310,
        QMI8658,
        QMC5883L,
        HMC5883L,
        PMSA0031,
        QMA6100P,
        MPU6050,
        LIS3DH,
        BMA423,
        BQ24295,
        LSM6DS3,
        TCA9535,
        TCA9555,
        VEML7700,
        RCWL9620,
        NCP5623,
        TSL2591,
        OPT3001,
        MLX90632,
        MLX90614,
        AHT10,
        BMX160,
        DFROBOT_LARK,
        NAU7802,
        FT6336U,
        STK8BAXX,
        ICM20948,
        MAX30102,
        TPS65233,
        MPR121KB,
        CGRADSENS
    } DeviceType;

    // typedef uint8_t DeviceAddress;
    typedef enum I2CPort {
        NO_I2C,
        WIRE,
        WIRE1,
    } I2CPort;

    typedef struct DeviceAddress {
        // set default values for ADDRESS_NONE
        I2CPort port = I2CPort::NO_I2C;
        uint8_t address = 0;

        explicit DeviceAddress(I2CPort port, uint8_t address);
        DeviceAddress();

        bool operator<(const DeviceAddress &other) const;
    } DeviceAddress;

    static const DeviceAddress ADDRESS_NONE;

    typedef uint8_t RegisterAddress;

    typedef struct FoundDevice {
        DeviceType type;
        DeviceAddress address;

        explicit FoundDevice(DeviceType = DeviceType::NONE, DeviceAddress = ADDRESS_NONE);
    } FoundDevice;

    static const FoundDevice DEVICE_NONE;

  public:
    ScanI2C();

    virtual void scanPort(ScanI2C::I2CPort);
    virtual void scanPort(ScanI2C::I2CPort, uint8_t *, uint8_t);

    /*
     * A bit of a hack, this tells the scanner not to tell later systems there is a screen to avoid enabling it.
     */
    void setSuppressScreen();

    FoundDevice firstScreen() const;

    FoundDevice firstRTC() const;

    FoundDevice firstKeyboard() const;

    FoundDevice firstAccelerometer() const;

    virtual FoundDevice find(DeviceType) const;

    virtual bool exists(DeviceType) const;

    virtual size_t countDevices() const;

  protected:
    virtual FoundDevice firstOfOrNONE(size_t, DeviceType[]) const;

  private:
    bool shouldSuppressScreen = false;
};