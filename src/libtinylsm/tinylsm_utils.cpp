#include "tinylsm_utils.h"
#include "RTC.h"
#include "configuration.h"
#include <ctime>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// CRC32 Implementation
// ============================================================================

uint32_t CRC32::table[256];
bool CRC32::table_initialized = false;

void CRC32::init_table()
{
    if (table_initialized) {
        return;
    }

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }

    table_initialized = true;
}

uint32_t CRC32::compute(const uint8_t *data, size_t length)
{
    return compute(data, length, 0xFFFFFFFF);
}

uint32_t CRC32::compute(const uint8_t *data, size_t length, uint32_t initial)
{
    init_table();

    uint32_t crc = initial;
    for (size_t i = 0; i < length; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// Time Utilities
// ============================================================================

uint32_t get_epoch_time()
{
    // Use Meshtastic's existing time function
    return getTime(); // From RTC.h
}

} // namespace tinylsm
} // namespace meshtastic
