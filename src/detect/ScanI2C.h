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
        ATECC608B,
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
        INA260,
        INA219,
        INA3221,
        MCP9808,
        SHT31,
        SHTC3,
        LPS22HB,
        QMC6310,
        QMI8658,
        QMC5883L,
        PMSA0031,
        MPU6050,
        LIS3DH,
        BMA423,
#ifdef HAS_NCP5623
        NCP5623,
#endif
    } DeviceType;

    // typedef uint8_t DeviceAddress;
    typedef enum I2CPort {
        NO_I2C,
        WIRE,
        WIRE1,
    } I2CPort;

    typedef struct DeviceAddress {
        I2CPort port;
        uint8_t address;

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