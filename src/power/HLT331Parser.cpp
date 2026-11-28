#include "HLT331Parser.h"
#include <string.h>

uint8_t HLT331Parser::crc8(const uint8_t *data, size_t len, uint8_t crc)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8C : crc >> 1;
    }
    return crc;
}

void HLT331Parser::decode(const uint8_t d[DATA_LENGTH], HLT331Status &out)
{
    out.charging = d[0] & 0x01;
    out.batteryPercent = (d[0] >> 1) > 100 ? 100 : d[0] >> 1;
    out.c1State = d[1] & 0x03;
    out.c2State = (d[1] >> 2) & 0x03;
    out.ntcOverTemp = d[1] & 0x10;
    out.wirelessActive = d[1] & 0x20;
    out.wirelessForeignObject = d[1] & 0x40;
    out.wirelessFull = d[1] & 0x80;
    out.wirelessProtocol = d[2] & 0x0F;
    out.fastChargeMode = d[2] >> 4;
    out.ntc1TempC = (int8_t)d[3];
    out.batteryMv = d[4] | (d[5] << 8);
    out.busMv = d[6] | (d[7] << 8);
    out.busMa = d[8] | (d[9] << 8);
    out.wirelessMw = (uint32_t)(d[10] | (d[11] << 8)) * 10;
    out.ntc2TempC = (int8_t)d[12];
}

bool HLT331Parser::feed(uint8_t byte)
{
    switch (state) {
    case WAIT_HEADER_0:
        if (byte == HEADER_0)
            state = WAIT_HEADER_1;
        return false;
    case WAIT_HEADER_1:
        // A5 A5 5A: the second A5 may be the real start of a frame.
        state = byte == HEADER_1 ? WAIT_LENGTH : (byte == HEADER_0 ? WAIT_HEADER_1 : WAIT_HEADER_0);
        return false;
    case WAIT_LENGTH:
        if (byte == DATA_LENGTH) {
            dataIndex = 0;
            state = READ_DATA;
        } else {
            // Noise ending in A5 5A swallows the next frame's A5 here: keep it as a header start.
            badLengthCount++;
            state = byte == HEADER_0 ? WAIT_HEADER_1 : WAIT_HEADER_0;
        }
        return false;
    case READ_DATA:
        data[dataIndex++] = byte;
        if (dataIndex == DATA_LENGTH)
            state = READ_CRC;
        return false;
    case READ_CRC: {
        state = WAIT_HEADER_0;
        const uint8_t length = DATA_LENGTH;
        if (crc8(data, DATA_LENGTH, crc8(&length, 1)) != byte) {
            badCrcCount++;
            return false;
        }
        memcpy(lastData, data, DATA_LENGTH);
        decode(data, lastStatus);
        goodFrameCount++;
        return true;
    }
    }
    return false;
}
