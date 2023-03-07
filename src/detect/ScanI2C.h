#pragma once

#include <stdint.h>
#include <stddef.h>

class ScanI2C {
public:
     typedef enum DeviceType
    {
        NONE,
        SCREEN_SSD1306,
        SCREEN_SH1106,
        SCREEN_UNKNOWN, // has the same address as the two above but does not respond to the same commands
        SCREEN_ST7567,
        ATECC608B,
        RTC_RV3028,
        RTC_PCF8563,
        CARDKB,
        RAK14004,
        PMU_AXP192_AXP2101,
        BME_680,
        BME_280,
        BMP_280,
        INA260,
        INA219,
        MCP9808,
        SHT31,
        SHTC3,
        LPS22HB,
        QMC6310,
        QMI8658,
        QMC5883L,
        PMSA0031,
    } DeviceType;

    typedef uint8_t DeviceAddress;
    typedef uint8_t RegisterAddress;

    typedef struct FoundDevice
    {
        DeviceType type;
        DeviceAddress address;

        FoundDevice(DeviceType type, DeviceAddress address) : type(type), address(address) {}
    } FoundDevice;

    static const FoundDevice DEVICE_NONE;
    static const DeviceAddress ADDRESS_NONE;

    ScanI2C();

    virtual void scanDevices();

    /*
     * A bit of a hack, this tells the scanner not to tell later systems there is a screen to avoid enabling it.
     */
    void setSuppressScreen();

    FoundDevice firstScreen() const;

    FoundDevice firstRTC() const;

    FoundDevice firstKeyboard() const;

    virtual FoundDevice find(DeviceType) const;

    virtual bool exists(DeviceType) const;

protected:

    virtual FoundDevice firstOfOrNONE(size_t, DeviceType[]) const;

private:

    bool shouldSuppressScreen = false;
};
