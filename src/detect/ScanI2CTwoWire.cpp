#include "ScanI2CTwoWire.h"

#include "concurrency/LockGuard.h"
#include "configuration.h"
#if defined(ARCH_PORTDUINO)
#include "linux/LinuxHardwareI2C.h"
#endif
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
#include "main.h" // atecc
#endif

// AXP192 and AXP2101 have the same device address, we just need to identify it in Power.cpp
#ifndef XPOWERS_AXP192_AXP2101_ADDRESS
#define XPOWERS_AXP192_AXP2101_ADDRESS 0x34
#endif

bool in_array(uint8_t *array, int size, uint8_t lookfor)
{
    int i;
    for (i = 0; i < size; i++)
        if (lookfor == array[i])
            return true;
    return false;
}

ScanI2C::FoundDevice ScanI2CTwoWire::find(ScanI2C::DeviceType type) const
{
    concurrency::LockGuard guard((concurrency::Lock *)&lock);

    return exists(type) ? ScanI2C::FoundDevice(type, deviceAddresses.at(type)) : DEVICE_NONE;
}

bool ScanI2CTwoWire::exists(ScanI2C::DeviceType type) const
{
    return deviceAddresses.find(type) != deviceAddresses.end();
}

ScanI2C::FoundDevice ScanI2CTwoWire::firstOfOrNONE(size_t count, DeviceType types[]) const
{
    concurrency::LockGuard guard((concurrency::Lock *)&lock);

    for (size_t k = 0; k < count; k++) {
        ScanI2C::DeviceType current = types[k];

        if (exists(current)) {
            return ScanI2C::FoundDevice(current, deviceAddresses.at(current));
        }
    }

    return DEVICE_NONE;
}

ScanI2C::DeviceType ScanI2CTwoWire::probeOLED(ScanI2C::DeviceAddress addr) const
{
    TwoWire *i2cBus = fetchI2CBus(addr);

    uint8_t r = 0;
    uint8_t r_prev = 0;
    uint8_t c = 0;
    ScanI2C::DeviceType o_probe = ScanI2C::DeviceType::SCREEN_UNKNOWN;
    do {
        r_prev = r;
        i2cBus->beginTransmission(addr.address);
        i2cBus->write((uint8_t)0x00);
        i2cBus->endTransmission();
        i2cBus->requestFrom((int)addr.address, 1);
        if (i2cBus->available()) {
            r = i2cBus->read();
        }
        r &= 0x0f;

        if (r == 0x08 || r == 0x00) {
            LOG_INFO("sh1106 display found\n");
            o_probe = SCREEN_SH1106; // SH1106
        } else if (r == 0x03 || r == 0x04 || r == 0x06 || r == 0x07) {
            LOG_INFO("ssd1306 display found\n");
            o_probe = SCREEN_SSD1306; // SSD1306
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

uint16_t ScanI2CTwoWire::getRegisterValue(const ScanI2CTwoWire::RegisterLocation &registerLocation,
                                          ScanI2CTwoWire::ResponseWidth responseWidth) const
{
    uint16_t value = 0x00;
    TwoWire *i2cBus = fetchI2CBus(registerLocation.i2cAddress);

    i2cBus->beginTransmission(registerLocation.i2cAddress.address);
    i2cBus->write(registerLocation.registerAddress);
    i2cBus->endTransmission();
    delay(20);
    i2cBus->requestFrom(registerLocation.i2cAddress.address, responseWidth);
    LOG_DEBUG("Wire.available() = %d\n", i2cBus->available());
    if (i2cBus->available() == 2) {
        // Read MSB, then LSB
        value = (uint16_t)i2cBus->read() << 8;
        value |= i2cBus->read();
    } else if (i2cBus->available()) {
        value = i2cBus->read();
    }
    return value;
}

#define SCAN_SIMPLE_CASE(ADDR, T, ...)                                                                                           \
    case ADDR:                                                                                                                   \
        LOG_INFO(__VA_ARGS__);                                                                                                   \
        type = T;                                                                                                                \
        break;

void ScanI2CTwoWire::scanPort(I2CPort port, uint8_t *address, uint8_t asize)
{
    concurrency::LockGuard guard((concurrency::Lock *)&lock);

    LOG_DEBUG("Scanning for I2C devices on port %d\n", port);

    uint8_t err;

    DeviceAddress addr(port, 0x00);

    uint16_t registerValue = 0x00;
    ScanI2C::DeviceType type;
    TwoWire *i2cBus;
#ifdef RV3028_RTC
    Melopero_RV3028 rtc;
#endif

#ifdef I2C_SDA1
    if (port == I2CPort::WIRE1) {
        i2cBus = &Wire1;
    } else {
#endif
        i2cBus = &Wire;
#ifdef I2C_SDA1
    }
#endif

    for (addr.address = 1; addr.address < 127; addr.address++) {
        if (asize != 0) {
            if (!in_array(address, asize, addr.address))
                continue;
            LOG_DEBUG("Scanning address 0x%x\n", addr.address);
        }
        i2cBus->beginTransmission(addr.address);
#ifdef ARCH_PORTDUINO
        if (i2cBus->read() != -1)
            err = 0;
        else
            err = 2;
#else
        err = i2cBus->endTransmission();
#endif
        type = NONE;
        if (err == 0) {
            LOG_DEBUG("I2C device found at address 0x%x\n", addr.address);

            switch (addr.address) {
            case SSD1306_ADDRESS:
                type = probeOLED(addr);
                break;

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
            case ATECC608B_ADDR:
#ifdef RP2040_SLOW_CLOCK
                if (atecc.begin(addr.address, Wire, Serial2) == true)
#else
                if (atecc.begin(addr.address) == true)
#endif

                {
                    LOG_INFO("ATECC608B initialized\n");
                } else {
                    LOG_WARN("ATECC608B initialization failed\n");
                }
                printATECCInfo();
                break;
#endif

#ifdef RV3028_RTC
            case RV3028_RTC:
                // foundDevices[addr] = RTC_RV3028;
                type = RTC_RV3028;
                LOG_INFO("RV3028 RTC found\n");
                rtc.initI2C(*i2cBus);
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

                SCAN_SIMPLE_CASE(TDECK_KB_ADDR, TDECKKB, "T-Deck keyboard found\n");
                SCAN_SIMPLE_CASE(BBQ10_KB_ADDR, BBQ10KB, "BB Q10 keyboard found\n");
                SCAN_SIMPLE_CASE(ST7567_ADDRESS, SCREEN_ST7567, "st7567 display found\n");
#ifdef HAS_NCP5623
                SCAN_SIMPLE_CASE(NCP5623_ADDR, NCP5623, "NCP5623 RGB LED found\n");
#endif
#ifdef HAS_PMU
                SCAN_SIMPLE_CASE(XPOWERS_AXP192_AXP2101_ADDRESS, PMU_AXP192_AXP2101, "axp192/axp2101 PMU found\n")
#endif
            case BME_ADDR:
            case BME_ADDR_ALTERNATE:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xD0), 1); // GET_ID
                switch (registerValue) {
                case 0x61:
                    LOG_INFO("BME-680 sensor found at address 0x%x\n", (uint8_t)addr.address);
                    type = BME_680;
                    break;
                case 0x60:
                    LOG_INFO("BME-280 sensor found at address 0x%x\n", (uint8_t)addr.address);
                    type = BME_280;
                    break;
                case 0x55:
                    LOG_INFO("BMP-085 or BMP-180 sensor found at address 0x%x\n", (uint8_t)addr.address);
                    type = BMP_085;
                    break;
                default:
                    LOG_INFO("BMP-280 sensor found at address 0x%x\n", (uint8_t)addr.address);
                    type = BMP_280;
                }
                break;
#ifndef HAS_NCP5623
            case AHT10_ADDR:
                LOG_INFO("AHT10 sensor found at address 0x%x\n", (uint8_t)addr.address);
                type = AHT10;
                break;
#endif
            case INA_ADDR:
            case INA_ADDR_ALTERNATE:
            case INA_ADDR_WAVESHARE_UPS:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xFE), 2);
                LOG_DEBUG("Register MFG_UID: 0x%x\n", registerValue);
                if (registerValue == 0x5449) {
                    LOG_INFO("INA260 sensor found at address 0x%x\n", (uint8_t)addr.address);
                    type = INA260;
                } else { // Assume INA219 if INA260 ID is not found
                    LOG_INFO("INA219 sensor found at address 0x%x\n", (uint8_t)addr.address);
                    type = INA219;
                }
                break;
            case INA3221_ADDR:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xFE), 2);
                LOG_DEBUG("Register MFG_UID: 0x%x\n", registerValue);
                if (registerValue == 0x5449) {
                    LOG_INFO("INA3221 sensor found at address 0x%x\n", (uint8_t)addr.address);
                    type = INA3221;
                } else {
                    LOG_INFO("DFRobot Lark weather station found at address 0x%x\n", (uint8_t)addr.address);
                    type = DFROBOT_LARK;
                }
                break;
            case MCP9808_ADDR:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x07), 2);
                if (registerValue == 0x0400) {
                    type = MCP9808;
                    LOG_INFO("MCP9808 sensor found\n");
                } else {
                    type = LIS3DH;
                    LOG_INFO("LIS3DH accelerometer found\n");
                }

                break;

            case SHT31_4x_ADDR:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x89), 2);
                if (registerValue == 0x11a2 || registerValue == 0x11da) {
                    type = SHT4X;
                    LOG_INFO("SHT4X sensor found\n");
                } else if (getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x7E), 2) == 0x5449) {
                    type = OPT3001;
                    LOG_INFO("OPT3001 light sensor found\n");
                } else {
                    type = SHT31;
                    LOG_INFO("SHT31 sensor found\n");
                }

                break;

                SCAN_SIMPLE_CASE(SHTC3_ADDR, SHTC3, "SHTC3 sensor found\n")
                SCAN_SIMPLE_CASE(RCWL9620_ADDR, RCWL9620, "RCWL9620 sensor found\n")

            case LPS22HB_ADDR_ALT:
                SCAN_SIMPLE_CASE(LPS22HB_ADDR, LPS22HB, "LPS22HB sensor found\n")

                SCAN_SIMPLE_CASE(QMC6310_ADDR, QMC6310, "QMC6310 Highrate 3-Axis magnetic sensor found\n")

            case QMI8658_ADDR:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x0A), 1); // get ID
                if (registerValue == 0xC0) {
                    type = BQ24295;
                    LOG_INFO("BQ24295 PMU found\n");
                    break;
                }
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x0F), 1); // get ID
                if (registerValue == 0x6A) {
                    type = LSM6DS3;
                    LOG_INFO("LSM6DS3 accelerometer found at address 0x%x\n", (uint8_t)addr.address);
                } else {
                    type = QMI8658;
                    LOG_INFO("QMI8658 Highrate 6-Axis inertial measurement sensor found\n");
                }
                break;

                SCAN_SIMPLE_CASE(QMC5883L_ADDR, QMC5883L, "QMC5883L Highrate 3-Axis magnetic sensor found\n")

                SCAN_SIMPLE_CASE(PMSA0031_ADDR, PMSA0031, "PMSA0031 air quality sensor found\n")
                SCAN_SIMPLE_CASE(MPU6050_ADDR, MPU6050, "MPU6050 accelerometer found\n");
                SCAN_SIMPLE_CASE(BMX160_ADDR, BMX160, "BMX160 accelerometer found\n");
                SCAN_SIMPLE_CASE(BMA423_ADDR, BMA423, "BMA423 accelerometer found\n");
                SCAN_SIMPLE_CASE(LSM6DS3_ADDR, LSM6DS3, "LSM6DS3 accelerometer found at address 0x%x\n", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(TCA9555_ADDR, TCA9555, "TCA9555 I2C expander found\n");
                SCAN_SIMPLE_CASE(VEML7700_ADDR, VEML7700, "VEML7700 light sensor found\n");
                SCAN_SIMPLE_CASE(TSL25911_ADDR, TSL2591, "TSL2591 light sensor found\n");
                SCAN_SIMPLE_CASE(OPT3001_ADDR, OPT3001, "OPT3001 light sensor found\n");
                SCAN_SIMPLE_CASE(MLX90632_ADDR, MLX90632, "MLX90632 IR temp sensor found\n");
                SCAN_SIMPLE_CASE(NAU7802_ADDR, NAU7802, "NAU7802 based scale found\n");

            default:
                LOG_INFO("Device found at address 0x%x was not able to be enumerated\n", addr.address);
            }
        } else if (err == 4) {
            LOG_ERROR("Unknown error at address 0x%x\n", addr.address);
        }

        // Check if a type was found for the enumerated device - save, if so
        if (type != NONE) {
            deviceAddresses[type] = addr;
            foundDevices[addr] = type;
        }
    }
}

void ScanI2CTwoWire::scanPort(I2CPort port)
{
    scanPort(port, nullptr, 0);
}

TwoWire *ScanI2CTwoWire::fetchI2CBus(ScanI2C::DeviceAddress address) const
{
    if (address.port == ScanI2C::I2CPort::WIRE) {
        return &Wire;
    } else {
#ifdef I2C_SDA1
        return &Wire1;
#else
        return &Wire;
#endif
    }
}

size_t ScanI2CTwoWire::countDevices() const
{
    return foundDevices.size();
}
