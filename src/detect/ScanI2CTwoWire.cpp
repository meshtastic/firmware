#include "ScanI2CTwoWire.h"

#if !MESHTASTIC_EXCLUDE_I2C

#include "concurrency/LockGuard.h"
#if defined(ARCH_PORTDUINO)
#include "linux/LinuxHardwareI2C.h"
#endif
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
#include "meshUtils.h" // vformat
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
            logFoundDevice("SH1106", (uint8_t)addr.address);
            o_probe = SCREEN_SH1106; // SH1106
        } else if (r == 0x03 || r == 0x04 || r == 0x06 || r == 0x07) {
            logFoundDevice("SSD1306", (uint8_t)addr.address);
            o_probe = SCREEN_SSD1306; // SSD1306
        }
        c++;
    } while ((r != r_prev) && (c < 4));
    LOG_DEBUG("0x%x subtype probed in %i tries ", r, c);

    return o_probe;
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
        logFoundDevice(__VA_ARGS__);                                                                                             \
        type = T;                                                                                                                \
        break;

void ScanI2CTwoWire::scanPort(I2CPort port, uint8_t *address, uint8_t asize)
{
    concurrency::LockGuard guard((concurrency::Lock *)&lock);

    LOG_DEBUG("Scan for I2C devices on port %d", port);

    uint8_t err;

    DeviceAddress addr(port, 0x00);

    uint16_t registerValue = 0x00;
    ScanI2C::DeviceType type;
    TwoWire *i2cBus;
#ifdef RV3028_RTC
    Melopero_RV3028 rtc;
#endif

#if WIRE_INTERFACES_COUNT == 2
    if (port == I2CPort::WIRE1) {
        i2cBus = &Wire1;
    } else {
#endif
        i2cBus = &Wire;
#if WIRE_INTERFACES_COUNT == 2
    }
#endif

    // We only need to scan 112 addresses, the rest is reserved for special purposes
    // 0x00 General Call
    // 0x01 CBUS addresses
    // 0x02 Reserved for different bus formats
    // 0x03 Reserved for future purposes
    // 0x04-0x07 High Speed Master Code
    // 0x78-0x7B 10-bit slave addressing
    // 0x7C-0x7F Reserved for future purposes

    for (addr.address = 8; addr.address < 120; addr.address++) {
        if (asize != 0) {
            if (!in_array(address, asize, (uint8_t)addr.address))
                continue;
            LOG_DEBUG("Scan address 0x%x", (uint8_t)addr.address);
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
            switch (addr.address) {
            case SSD1306_ADDRESS:
                type = probeOLED(addr);
                break;

#ifdef RV3028_RTC
            case RV3028_RTC:
                // foundDevices[addr] = RTC_RV3028;
                type = RTC_RV3028;
                logFoundDevice("RV3028", (uint8_t)addr.address);
                rtc.initI2C(*i2cBus);
                rtc.writeToRegister(0x35, 0x07); // no Clkout
                rtc.writeToRegister(0x37, 0xB4);
                break;
#endif

#ifdef PCF8563_RTC
                SCAN_SIMPLE_CASE(PCF8563_RTC, RTC_PCF8563, "PCF8563", (uint8_t)addr.address)
#endif

            case CARDKB_ADDR:
                // Do we have the RAK14006 instead?
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x04), 1);
                if (registerValue == 0x02) {
                    // KEYPAD_VERSION
                    logFoundDevice("RAK14004", (uint8_t)addr.address);
                    type = RAK14004;
                } else {
                    logFoundDevice("M5 cardKB", (uint8_t)addr.address);
                    type = CARDKB;
                }
                break;

                SCAN_SIMPLE_CASE(TDECK_KB_ADDR, TDECKKB, "T-Deck keyboard", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(BBQ10_KB_ADDR, BBQ10KB, "BB Q10", (uint8_t)addr.address);

                SCAN_SIMPLE_CASE(ST7567_ADDRESS, SCREEN_ST7567, "ST7567", (uint8_t)addr.address);
#ifdef HAS_NCP5623
                SCAN_SIMPLE_CASE(NCP5623_ADDR, NCP5623, "NCP5623", (uint8_t)addr.address);
#endif
#ifdef HAS_PMU
                SCAN_SIMPLE_CASE(XPOWERS_AXP192_AXP2101_ADDRESS, PMU_AXP192_AXP2101, "AXP192/AXP2101", (uint8_t)addr.address)
#endif
            case BME_ADDR:
            case BME_ADDR_ALTERNATE:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xD0), 1); // GET_ID
                switch (registerValue) {
                case 0x61:
                    logFoundDevice("BME680", (uint8_t)addr.address);
                    type = BME_680;
                    break;
                case 0x60:
                    logFoundDevice("BME280", (uint8_t)addr.address);
                    type = BME_280;
                    break;
                case 0x55:
                    logFoundDevice("BMP085/BMP180", (uint8_t)addr.address);
                    type = BMP_085;
                    break;
                default:
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x00), 1); // GET_ID
                    switch (registerValue) {
                    case 0x50: // BMP-388 should be 0x50
                        logFoundDevice("BMP-388", (uint8_t)addr.address);
                        type = BMP_3XX;
                        break;
                    case 0x58: // BMP-280 should be 0x58
                    default:
                        logFoundDevice("BMP-280", (uint8_t)addr.address);
                        type = BMP_280;
                        break;
                    }
                    break;
                }
                break;
#ifndef HAS_NCP5623
            case AHT10_ADDR:
                logFoundDevice("AHT10", (uint8_t)addr.address);
                type = AHT10;
                break;
#endif
            case INA_ADDR:
            case INA_ADDR_ALTERNATE:
            case INA_ADDR_WAVESHARE_UPS:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xFE), 2);
                LOG_DEBUG("Register MFG_UID: 0x%x", registerValue);
                if (registerValue == 0x5449) {
                    logFoundDevice("INA260", (uint8_t)addr.address);
                    type = INA260;
                } else { // Assume INA219 if INA260 ID is not found
                    logFoundDevice("INA219", (uint8_t)addr.address);
                    type = INA219;
                }
                break;
            case INA3221_ADDR:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xFE), 2);
                LOG_DEBUG("Register MFG_UID FE: 0x%x", registerValue);
                if (registerValue == 0x5449) {
                    logFoundDevice("INA3221", (uint8_t)addr.address);
                    type = INA3221;
                } else {
                    /* check the first 2 bytes of the 6 byte response register
                    LARK FW 1.0 should return:
                    RESPONSE_STATUS STATUS_SUCCESS (0x53)
                    RESPONSE_CMD CMD_GET_VERSION (0x05)
                    RESPONSE_LEN_L 0x02
                    RESPONSE_LEN_H 0x00
                    RESPONSE_PAYLOAD 0x01
                    RESPONSE_PAYLOAD+1 0x00
                    */
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x05), 2);
                    LOG_DEBUG("Register MFG_UID 05: 0x%x", registerValue);
                    if (registerValue == 0x5305) {
                        logFoundDevice("DFRobot Lark", (uint8_t)addr.address);
                        type = DFROBOT_LARK;
                    }
                    // else: probably a RAK12500/UBLOX GPS on I2C
                }
                break;
            case MCP9808_ADDR:
                // We need to check for STK8BAXX first, since register 0x07 is new data flag for the z-axis and can produce some
                // weird result. and register 0x00 doesn't seems to be colliding with MCP9808 and LIS3DH chips.
                {
#ifdef HAS_STK8XXX
                    // Check register 0x00 for 0x8700 response to ID STK8BA53 chip.
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x00), 2);
                    if (registerValue == 0x8700) {
                        type = STK8BAXX;
                        logFoundDevice("STK8BAXX", (uint8_t)addr.address);
                        break;
                    }
#endif

                    // Check register 0x07 for 0x0400 response to ID MCP9808 chip.
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x07), 2);
                    if (registerValue == 0x0400) {
                        type = MCP9808;
                        logFoundDevice("MCP9808", (uint8_t)addr.address);
                        break;
                    }

                    // Check register 0x0F for 0x3300 response to ID LIS3DH chip.
                    registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x0F), 2);
                    if (registerValue == 0x3300 || registerValue == 0x3333) { // RAK4631 WisBlock has LIS3DH register at 0x3333
                        type = LIS3DH;
                        logFoundDevice("LIS3DH", (uint8_t)addr.address);
                    }
                    break;
                }
            case SHT31_4x_ADDR:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x89), 2);
                if (registerValue == 0x11a2 || registerValue == 0x11da || registerValue == 0xe9c) {
                    type = SHT4X;
                    logFoundDevice("SHT4X", (uint8_t)addr.address);
                } else if (getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x7E), 2) == 0x5449) {
                    type = OPT3001;
                    logFoundDevice("OPT3001", (uint8_t)addr.address);
                } else {
                    type = SHT31;
                    logFoundDevice("SHT31", (uint8_t)addr.address);
                }

                break;

                SCAN_SIMPLE_CASE(SHTC3_ADDR, SHTC3, "SHTC3", (uint8_t)addr.address)
            case RCWL9620_ADDR:
                // get MAX30102 PARTID
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0xFF), 1);
                if (registerValue == 0x15) {
                    type = MAX30102;
                    logFoundDevice("MAX30102", (uint8_t)addr.address);
                    break;
                } else {
                    type = RCWL9620;
                    logFoundDevice("RCWL9620", (uint8_t)addr.address);
                }
                break;

            case LPS22HB_ADDR_ALT:
                SCAN_SIMPLE_CASE(LPS22HB_ADDR, LPS22HB, "LPS22HB", (uint8_t)addr.address)
                SCAN_SIMPLE_CASE(QMC6310_ADDR, QMC6310, "QMC6310", (uint8_t)addr.address)

            case QMI8658_ADDR:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x0A), 1); // get ID
                if (registerValue == 0xC0) {
                    type = BQ24295;
                    logFoundDevice("BQ24295", (uint8_t)addr.address);
                    break;
                }
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x0F), 1); // get ID
                if (registerValue == 0x6A) {
                    type = LSM6DS3;
                    logFoundDevice("LSM6DS3", (uint8_t)addr.address);
                } else {
                    type = QMI8658;
                    logFoundDevice("QMI8658", (uint8_t)addr.address);
                }
                break;

                SCAN_SIMPLE_CASE(QMC5883L_ADDR, QMC5883L, "QMC5883L", (uint8_t)addr.address)
                SCAN_SIMPLE_CASE(HMC5883L_ADDR, HMC5883L, "HMC5883L", (uint8_t)addr.address)
#ifdef HAS_QMA6100P
                SCAN_SIMPLE_CASE(QMA6100P_ADDR, QMA6100P, "QMA6100P", (uint8_t)addr.address)
#else
                SCAN_SIMPLE_CASE(PMSA0031_ADDR, PMSA0031, "PMSA0031", (uint8_t)addr.address)
#endif
            case BMA423_ADDR: // this can also be LIS3DH_ADDR_ALT
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x0F), 2);
                if (registerValue == 0x3300 || registerValue == 0x3333) { // RAK4631 WisBlock has LIS3DH register at 0x3333
                    type = LIS3DH;
                    logFoundDevice("LIS3DH", (uint8_t)addr.address);
                } else {
                    type = BMA423;
                    logFoundDevice("BMA423", (uint8_t)addr.address);
                }
                break;

                SCAN_SIMPLE_CASE(LSM6DS3_ADDR, LSM6DS3, "LSM6DS3", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(TCA9535_ADDR, TCA9535, "TCA9535", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(TCA9555_ADDR, TCA9555, "TCA9555", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(VEML7700_ADDR, VEML7700, "VEML7700", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(TSL25911_ADDR, TSL2591, "TSL2591", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(OPT3001_ADDR, OPT3001, "OPT3001", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(MLX90632_ADDR, MLX90632, "MLX90632", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(NAU7802_ADDR, NAU7802, "NAU7802", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(FT6336U_ADDR, FT6336U, "FT6336U", (uint8_t)addr.address);
                SCAN_SIMPLE_CASE(MAX1704X_ADDR, MAX17048, "MAX17048", (uint8_t)addr.address);
#ifdef HAS_TPS65233
                SCAN_SIMPLE_CASE(TPS65233_ADDR, TPS65233, "TPS65233", (uint8_t)addr.address);
#endif

            case MLX90614_ADDR_DEF:
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x0e), 1);
                if (registerValue == 0x5a) {
                    type = MLX90614;
                    logFoundDevice("MLX90614", (uint8_t)addr.address);
                } else {
                    type = MPR121KB;
                    logFoundDevice("MPR121KB", (uint8_t)addr.address);
                }
                break;

            case ICM20948_ADDR:     // same as BMX160_ADDR
            case ICM20948_ADDR_ALT: // same as MPU6050_ADDR
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x00), 1);
                if (registerValue == 0xEA) {
                    type = ICM20948;
                    logFoundDevice("ICM20948", (uint8_t)addr.address);
                    break;
                } else if (addr.address == BMX160_ADDR) {
                    type = BMX160;
                    logFoundDevice("BMX160", (uint8_t)addr.address);
                    break;
                } else {
                    type = MPU6050;
                    logFoundDevice("MPU6050", (uint8_t)addr.address);
                    break;
                }
                break;

            case CGRADSENS_ADDR:
                // Register 0x00 of the RadSens sensor contains is product identifier 0x7D
                registerValue = getRegisterValue(ScanI2CTwoWire::RegisterLocation(addr, 0x00), 1);
                if (registerValue == 0x7D) {
                    type = CGRADSENS;
                    logFoundDevice("ClimateGuard RadSens", (uint8_t)addr.address);
                    break;
                }
                break;

            default:
                LOG_INFO("Device found at address 0x%x was not able to be enumerated", (uint8_t)addr.address);
            }
        } else if (err == 4) {
            LOG_ERROR("Unknown error at address 0x%x", (uint8_t)addr.address);
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
#if WIRE_INTERFACES_COUNT == 2
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

void ScanI2CTwoWire::logFoundDevice(const char *device, uint8_t address)
{
    LOG_INFO("%s found at address 0x%x", device, address);
}
#endif