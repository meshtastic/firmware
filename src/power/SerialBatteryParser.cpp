#include "SerialBatteryParser.h"

bool SerialBatteryParser::feed(uint8_t byte)
{
    if (!inFrame) {
        if (byte == START) {
            inFrame = true;
            payloadIndex = 0;
        }
        return false;
    }

    if (payloadIndex < PAYLOAD_LENGTH) {
        payload[payloadIndex++] = byte;
        return false;
    }

    inFrame = false;
    if (byte != END) {
        badFrameCount++;
        // The byte that broke this frame may start the next one.
        if (byte == START) {
            inFrame = true;
            payloadIndex = 0;
        }
        return false;
    }

    lastStatus.batteryPercent = payload[0] > 100 ? 100 : payload[0];
    // payload[1..3] are volts, centivolts and 0.1 mV steps of half the pack voltage.
    const uint32_t halfTenthsOfMv = payload[1] * 10000UL + payload[2] * 100UL + payload[3];
    lastStatus.batteryMv = (uint16_t)(halfTenthsOfMv * 2 / 10);
    goodFrameCount++;
    return true;
}
