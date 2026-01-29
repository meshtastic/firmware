#include "cas.h"

// Calculate the checksum for a CAS packet
void CASChecksum(uint8_t *message, size_t length)
{
    uint32_t cksum = ((uint32_t)message[5] << 24); // Message ID
    cksum += ((uint32_t)message[4]) << 16;         // Class
    cksum += message[2];                           // Payload Len

    // Iterate over the payload as a series of uint32_t's and
    // accumulate the cksum
    for (size_t i = 0; i < (length - 10) / 4; i++) {
        uint32_t pl = 0;
        memcpy(&pl, (message + 6) + (i * sizeof(uint32_t)), sizeof(uint32_t)); // avoid pointer dereference
        cksum += pl;
    }

    // Place the checksum values in the message
    message[length - 4] = (cksum & 0xFF);
    message[length - 3] = (cksum & (0xFF << 8)) >> 8;
    message[length - 2] = (cksum & (0xFF << 16)) >> 16;
    message[length - 1] = (cksum & (0xFF << 24)) >> 24;
}

// Function to create a CAS packet for editing in memory
uint8_t makeCASPacket(uint8_t *out, uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg)
{
    // General CAS structure
    //        | H1   | H2   | payload_len | cls  | msg  | Payload       ...   | Checksum                  |
    // Size:  | 1    | 1    | 2           | 1    | 1    | payload_len         | 4                         |
    // Pos:   | 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7      ...   | 6 + payload_len ...       |
    //        |------|------|-------------|------|------|------|--------------|---------------------------|
    //        | 0xBA | 0xCE | 0xXX | 0xXX | 0xXX | 0xXX | 0xXX | 0xXX   ...   | 0xXX | 0xXX | 0xXX | 0xXX |

    // Construct the CAS packet
    out[0] = 0xBA;         // header 1 (0xBA)
    out[1] = 0xCE;         // header 2 (0xCE)
    out[2] = payload_size; // length 1
    out[3] = 0;            // length 2
    out[4] = class_id;     // class
    out[5] = msg_id;       // id

    out[6 + payload_size] = 0x00; // Checksum
    out[7 + payload_size] = 0x00;
    out[8 + payload_size] = 0x00;
    out[9 + payload_size] = 0x00;

    for (int i = 0; i < payload_size; i++) {
        out[6 + i] = pgm_read_byte(&msg[i]);
    }
    CASChecksum(out, (payload_size + 10));

#if defined(GPS_DEBUG) && defined(DEBUG_PORT)
    LOG_DEBUG("CAS packet: ");
    DEBUG_PORT.hexDump(MESHTASTIC_LOG_LEVEL_DEBUG, out, payload_size + 10);
#endif
    return (payload_size + 10);
}
