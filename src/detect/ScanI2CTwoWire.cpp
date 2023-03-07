#include "ScanI2CTwoWire.h"

#include "../concurrency/LockGuard.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
#include "../main.h" // atecc
#endif

// AXP192 and AXP2101 have the same device address, we just need to identify it in Power.cpp
#ifndef XPOWERS_AXP192_AXP2101_ADDRESS
#define XPOWERS_AXP192_AXP2101_ADDRESS 0x34
#endif

ScanI2CTwoWire::ScanI2CTwoWire(TwoWire &wire) : i2cBus(wire) {}

ScanI2C::FoundDevice ScanI2CTwoWire::find(ScanI2C::DeviceType type) const
{
    concurrency::LockGuard guard((concurrency::Lock *) &lock);

    return exists(type) ?
           ScanI2C::FoundDevice(type, deviceAddresses.at(type)) : DEVICE_NONE;
}

bool ScanI2CTwoWire::exists(ScanI2C::DeviceType type) const
{
    return deviceAddresses.find(type) != deviceAddresses.end();
}

ScanI2C::FoundDevice ScanI2CTwoWire::firstOfOrNONE(size_t count, DeviceType types[]) const
{
    concurrency::LockGuard guard((concurrency::Lock *) &lock);

    for (size_t k = 0; k < count; k++ )
    {
        ScanI2C::DeviceType current = types[k];

        if (exists(current)) {
            return ScanI2C::FoundDevice(current, deviceAddresses.at(current));
        }
    }

    return DEVICE_NONE;
}

uint8_t ScanI2CTwoWire::probeOLED(ScanI2C::DeviceAddress addr) const
{
    uint8_t r = 0;
    uint8_t r_prev = 0;
    uint8_t c = 0;
    uint8_t o_probe = 0;
    do {
        r_prev = r;
        i2cBus.beginTransmission(addr);
        i2cBus.write(0x00);
        i2cBus.endTransmission();
        i2cBus.requestFrom((int)addr, 1);
        if (i2cBus.available()) {
            r = i2cBus.read();
        }
        r &= 0x0f;

        if (r == 0x08 || r == 0x00) {
            o_probe = 2; // SH1106
        } else if (r == 0x03 || r == 0x04 || r == 0x06 || r == 0x07) {
            o_probe = 1; // SSD1306
        }
        c++;
    } while ((r != r_prev) && (c < 4));
    LOG_DEBUG("0x%x subtype probed in %i tries \n", r, c);
    return o_probe;
}
void ScanI2CTwoWire::printATECCInfo() const
{
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
    atecc.readConfigZone(false);

    LOG_DEBUG("ATECC608B Serial Number: ");
    for (int i = 0; i < 9; i++) {
        LOG_DEBUG("%02x", atecc.serialNumber[i]);
    }

    LOG_DEBUG(", Rev Number: ");
    for (int i = 0; i < 4; i++) {
        LOG_DEBUG("%02x", atecc.revisionNumber[i]);
    }
    LOG_DEBUG("\n");

    LOG_DEBUG("ATECC608B Config %s", atecc.configLockStatus ? "Locked" : "Unlocked");
    LOG_DEBUG(", Data %s", atecc.dataOTPLockStatus ? "Locked" : "Unlocked");
    LOG_DEBUG(", Slot 0 %s\n", atecc.slot0LockStatus ? "Locked" : "Unlocked");

    if (atecc.configLockStatus && atecc.dataOTPLockStatus && atecc.slot0LockStatus) {
        if (atecc.generatePublicKey() == false) {
            LOG_DEBUG("ATECC608B Error generating public key\n");
        } else {
            LOG_DEBUG("ATECC608B Public Key: ");
            for (int i = 0; i < 64; i++) {
                LOG_DEBUG("%02x", atecc.publicKey64Bytes[i]);
            }
            LOG_DEBUG("\n");
        }
    }
#endif
}

uint16_t ScanI2CTwoWire::getRegisterValue(
        const ScanI2CTwoWire::RegisterLocation & registerLocation,
        ScanI2CTwoWire::ResponseWidth responseWidth
) const
{
    uint16_t value = 0x00;
    i2cBus.beginTransmission(registerLocation.i2cAddress);
    i2cBus.write(registerLocation.registerAddress);
    i2cBus.endTransmission();
    delay(20);
    i2cBus.requestFrom(registerLocation.i2cAddress, responseWidth);
    LOG_DEBUG("Wire.available() = %d\n", i2cBus.available());
    if (i2cBus.available() == 2) {
        // Read MSB, then LSB
        value = (uint16_t)i2cBus.read() << 8;
        value |= i2cBus.read();
    } else if (i2cBus.available()) {
        value = i2cBus.read();
    }
    return value;
}

#define SCAN_SIMPLE_CASE(ADDR, T, ...)      \
                case ADDR:                  \
                    LOG_INFO(__VA_ARGS__);  \
                    type = T;               \
                    break;


void ScanI2CTwoWire::scanDevices()
{
    LOG_INFO("Scanning for i2c devices....");
    concurrency::LockGuard guard((concurrency::Lock *) &lock);

    byte err;
    DeviceAddress addr;
    uint16_t registerValue = 0x00;
    ScanI2C::DeviceType type;

    for (addr = 1; addr < 127; addr++) {
        i2cBus.beginTransmission(addr);
        err = i2cBus.endTransmission();
        type = NONE;
        if (err == 0) {
            LOG_DEBUG("I2C device found at address 0x%x\n", addr);

            switch (addr)
            {
                case SSD1306_ADDRESS:
                    switch (probeOLED(addr)) {
                        case 1:
                            LOG_INFO("ssd1306 display found\n");
                            type = SCREEN_SSD1306;
                            break;
                        case 2:
                            LOG_INFO("sh1106 display found\n");
                            type = SCREEN_SH1106;
                            break;
                        default:
                            LOG_INFO("unknown display found\n");
                            type = SCREEN_UNKNOWN;
                    }
                    break;

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
                case ATECC608B_ADDR:
                    type = ATECC608B;
                    if (atecc.begin(addr) == true) {
                        LOG_INFO("ATECC608B initialized\n");
                    } else {
                        LOG_WARN("ATECC608B initialization failed\n");
                    }
                    printATECCInfo();
                    break;
#endif

#ifdef RV3028_RTC
                case RV3028_RTC:
                    //foundDevices[addr] = RTC_RV3028;
                    type = RTC_RV3028;
                    LOG_INFO("RV3028 RTC found\n");
                    Melopero_RV3028 rtc;
                    rtc.initI2C();
                    rtc.writeToRegister(0x35, 0x07); // no Clkout
                    rtc.writeToRegister(0x37, 0xB4);
                    break;
#endif

#ifdef PCF8563_RTC
                SCAN_SIMPLE_CASE(PCF8563_RTC, RTC_PCF8563, "PCF8563 RTC found\n")
#endif

                case CARDKB_ADDR:
                    // Do we have the RAK14006 instead?
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x04), 1);
                    if (registerValue == 0x02) {
                        // KEYPAD_VERSION
                        LOG_INFO("RAK14004 found\n");
                        type = RAK14004;
                    } else {
                        LOG_INFO("m5 cardKB found\n");
                        type = CARDKB;
                    }
                    break;

                SCAN_SIMPLE_CASE(ST7567_ADDRESS, SCREEN_ST7567, "st7567 display found\n")

 #ifdef HAS_PMU
                SCAN_SIMPLE_CASE(XPOWERS_AXP192_AXP2101_ADDRESS, PMU_AXP192_AXP2101, "axp192/axp2101 PMU found\n")
 #endif
                case BME_ADDR:
                case BME_ADDR_ALTERNATE:
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xD0), 1); // GET_ID
                    switch (registerValue)
                    {
                        case 0x61:
                            LOG_INFO("BME-680 sensor found at address 0x%x\n", (uint8_t)addr);
                            type = BME_680;
                            break;
                        case 0x60:
                            LOG_INFO("BME-280 sensor found at address 0x%x\n", (uint8_t)addr);
                            type = BME_280;
                            break;
                        default:
                            LOG_INFO("BMP-280 sensor found at address 0x%x\n", (uint8_t)addr);
                            type = BMP_280;
                    }
                    break;

                case INA_ADDR:
                case INA_ADDR_ALTERNATE:
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xFE), 2);
                    LOG_DEBUG("Register MFG_UID: 0x%x\n", registerValue);
                    if (registerValue == 0x5449) {
                        LOG_INFO("INA260 sensor found at address 0x%x\n", (uint8_t)addr);
                        type = INA260;
                    } else { // Assume INA219 if INA260 ID is not found
                        LOG_INFO("INA219 sensor found at address 0x%x\n", (uint8_t)addr);
                        type = INA219;
                    }
                    break;

                SCAN_SIMPLE_CASE(MCP9808_ADDR, MCP9808, "MCP9808 sensor found\n")

                SCAN_SIMPLE_CASE(SHT31_ADDR, SHT31, "SHT31 sensor found\n")
                SCAN_SIMPLE_CASE(SHTC3_ADDR, SHTC3, "SHTC3 sensor found\n")

                case LPS22HB_ADDR_ALT:
                SCAN_SIMPLE_CASE(LPS22HB_ADDR, LPS22HB,"LPS22HB sensor found\n")

                SCAN_SIMPLE_CASE(QMC6310_ADDR, QMC6310, "QMC6310 Highrate 3-Axis magnetic sensor found\n")
                SCAN_SIMPLE_CASE(QMI8658_ADDR, QMI8658, "QMI8658 Highrate 6-Axis inertial measurement sensor found\n")
                SCAN_SIMPLE_CASE(QMC5883L_ADDR, QMC5883L, "QMC5883L Highrate 3-Axis magnetic sensor found\n")

                SCAN_SIMPLE_CASE(PMSA0031_ADDR, PMSA0031, "PMSA0031 air quality sensor found\n")

                default:
                    LOG_INFO("Device found at address 0x%x was not able to be enumerated\n", addr);
            }

        } else if (err == 4) {
            LOG_ERROR("Unknow error at address 0x%x\n", addr);
        }

        // Check if a type was found for the enumerated device - save, if so
        if (type != NONE)
        {
            deviceAddresses[type] = addr;
            foundDevices[addr] = type;
        }
    }

    if (foundDevices.size() == 0)
        LOG_INFO("No I2C devices found\n");
    else
        LOG_INFO("%i I2C devices found\n", foundDevices.size());
}