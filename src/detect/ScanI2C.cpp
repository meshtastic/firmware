//
// Created by noster on 3/7/23.
//

#include "ScanI2C.h"


const ScanI2C::DeviceAddress ScanI2C::ADDRESS_NONE = 0;
const ScanI2C::FoundDevice ScanI2C::DEVICE_NONE =
        ScanI2C::FoundDevice(ScanI2C::DeviceType::NONE, ADDRESS_NONE);

ScanI2C::ScanI2C() = default;

void ScanI2C::scanDevices()
{
}

void ScanI2C::setSuppressScreen()
{
    shouldSuppressScreen = true;
}

ScanI2C::FoundDevice ScanI2C::firstScreen() const
{
    // Allow to override the scanner results for screen
    if (shouldSuppressScreen)
        return DEVICE_NONE;

    ScanI2C::DeviceType types[] = {SCREEN_SSD1306, SCREEN_SH1106, SCREEN_ST7567, SCREEN_UNKNOWN};
    return firstOfOrNONE(4, types);
}

ScanI2C::FoundDevice ScanI2C::firstRTC() const
{
    ScanI2C::DeviceType types[] = {RTC_RV3028, RTC_PCF8563};
    return firstOfOrNONE(2, types);
}

ScanI2C::FoundDevice ScanI2C::firstKeyboard() const
{
    ScanI2C::DeviceType types[] = {CARDKB, RAK14004};
    return firstOfOrNONE(2, types);
}

ScanI2C::FoundDevice ScanI2C::find(ScanI2C::DeviceType) const
{
    return DEVICE_NONE;
}

bool ScanI2C::exists(ScanI2C::DeviceType) const
{
    return false;
}

ScanI2C::FoundDevice ScanI2C::firstOfOrNONE(size_t count, ScanI2C::DeviceType * types) const
{
    return DEVICE_NONE;
}
