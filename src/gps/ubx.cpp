#include "ubx.h"

void UBXChecksum(uint8_t *message, size_t length)
{
    uint8_t CK_A = 0, CK_B = 0;
    for (size_t i = 2; i < length - 2; i++) {
        CK_A = (CK_A + message[i]) & 0xFF;
        CK_B = (CK_B + CK_A) & 0xFF;
    }
    message[length - 2] = CK_A;
    message[length - 1] = CK_B;
}

uint8_t makeUBXPacket(uint8_t *out, uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg)
{
    // UBX header
    out[0] = 0xB5;
    out[1] = 0x62;
    out[2] = class_id;
    out[3] = msg_id;
    out[4] = payload_size; // length LSB
    out[5] = 0x00;         // length MSB

    // Payload
    for (int i = 0; i < payload_size; i++) {
        out[6 + i] = pgm_read_byte(&msg[i]);
    }

    // Reserve space for checksum and compute it
    out[6 + payload_size] = 0x00;
    out[7 + payload_size] = 0x00;
    UBXChecksum(out, payload_size + 8);

    return payload_size + 8;
}
