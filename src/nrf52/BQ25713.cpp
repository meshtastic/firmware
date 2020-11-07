#include "BQ25713.h"
#include "configuration.h"

#include <Wire.h>

#ifdef BQ25703A_ADDR

const uint8_t BQ25713::devAddr = BQ25703A_ADDR;

bool BQ25713::setup()
{
    DEBUG_MSG("Init BQ25713\n");

    //	if(!writeReg(0x34,0x9034)) return false;
    //
    //	if(!writeReg(0x34,0x8034)) return false;

    if (!writeReg(0x00, 0x0F0A))
        return false; // Config Charge Option 0

    if (!writeReg(0x02, 0x0224))
        return false; // Config Charge Current

    if (!writeReg(0x04, 0x1070))
        return false; // Config Charge Voltage

    if (!writeReg(0x06, 0x099C))
        return false; // Config OTG Voltage

    if (!writeReg(0x08, 0x5000))
        return false; // Config OTG Current

    //	if(!writeReg(0x0A,0x0100)) return false;//Config Input Voltage

    if (!writeReg(0x0C, 0x1800))
        return false; // Config Minimum System Voltage

    if (!writeReg(0x0E, 0x4900))
        return false; // Config Input Current

    if (!writeReg(0x30, 0xE210))
        return false; // Config Charge Option 1

    if (!writeReg(0x32, 0x32BF))
        return false; // Config Charge Option 2

    if (!writeReg(0x34, 0x0834))
        return false; // Config Charge Option 3

    if (!writeReg(0x36, 0x4A65))
        return false; // Config Prochot Option 0

    if (!writeReg(0x38, 0x81FF))
        return false; // Config Prochot Option 1

    if (!writeReg(0x3A, 0xA0FF))
        return false; // Config ADC Option

    return true;
}

uint16_t BQ25713::readReg(uint8_t reg)
{
    Wire.beginTransmission(devAddr);
    Wire.write(reg);
    byte err = Wire.endTransmission();
    if (!err) {
        int readLen = 2;
        Wire.requestFrom(devAddr, (int)(readLen + 1));
        if (Wire.available() >= readLen) {
            uint8_t lsb = Wire.read(), msb = Wire.read();

            return (((uint16_t)msb) << 8) + lsb;
        } else
            return 0;
    } else {
        return 0;
    }
}

bool BQ25713::writeReg(uint8_t reg, uint16_t v)
{
    Wire.beginTransmission(devAddr);
    Wire.write(reg);
    Wire.write(v & 0xff);
    Wire.write((v >> 8) & 0xff);
    byte err = Wire.endTransmission(); // 0 for success

    if (!err) {
        // Do a test readback for early debugging
        uint16_t found = readReg(reg);
        if (found != v) {
            DEBUG_MSG("Readback reg=0x%0x test failed, expected 0x%0x, found 0x%0x!\n", reg, v, found);
            return true; // claim success - FIXME
        }
    }
    return !err;
}

#endif