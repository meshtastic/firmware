#include "ScanI2C.h"

const ScanI2C::DeviceAddress ScanI2C::ADDRESS_NONE = ScanI2C::DeviceAddress();
const ScanI2C::FoundDevice ScanI2C::DEVICE_NONE = ScanI2C::FoundDevice(ScanI2C::DeviceType::NONE, ADDRESS_NONE);

ScanI2C::ScanI2C() = default;

void ScanI2C::scanPort(ScanI2C::I2CPort port) {}
void ScanI2C::scanPort(ScanI2C::I2CPort port, uint8_t *address, uint8_t asize) {}

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
    ScanI2C::DeviceType types[] = {CARDKB, TDECKKB, BBQ10KB, RAK14004};
    return firstOfOrNONE(4, types);
}

ScanI2C::FoundDevice ScanI2C::firstAccelerometer() const
{
    ScanI2C::DeviceType types[] = {MPU6050, LIS3DH, BMA423, LSM6DS3, BMX160};
    return firstOfOrNONE(5, types);
}

ScanI2C::FoundDevice ScanI2C::find(ScanI2C::DeviceType) const
{
    return DEVICE_NONE;
}

bool ScanI2C::exists(ScanI2C::DeviceType) const
{
    return false;
}

ScanI2C::FoundDevice ScanI2C::firstOfOrNONE(size_t count, ScanI2C::DeviceType *types) const
{
    return DEVICE_NONE;
}

size_t ScanI2C::countDevices() const
{
    return 0;
}

ScanI2C::DeviceAddress::DeviceAddress(ScanI2C::I2CPort port, uint8_t address) : port(port), address(address) {}

ScanI2C::DeviceAddress::DeviceAddress() : DeviceAddress(I2CPort::NO_I2C, 0) {}

bool ScanI2C::DeviceAddress::operator<(const ScanI2C::DeviceAddress &other) const
{
    return
        // If this one has no port and other has a port
        (port == NO_I2C && other.port != NO_I2C)
        // if both have a port and this one's address is lower
        || (port != NO_I2C && other.port != NO_I2C && (address < other.address));
}

ScanI2C::FoundDevice::FoundDevice(ScanI2C::DeviceType type, ScanI2C::DeviceAddress address) : type(type), address(address) {}