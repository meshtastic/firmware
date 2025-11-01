#pragma once
#include <vector>

#include "memGet.h"
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/localonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

// this file defines constants which come from mesh.options

// Tricky macro to let you find the sizeof a type member
#define member_size(type, member) sizeof(((type *)0)->member)

// Minimum PSRAM the firmware expects before enabling the "expanded" queues that
// rely on off-chip RAM instead of internal DRAM. Currently set to 2MB to
// accommodate Heltec WiFi LoRa 32 V4 boards (and others)
static constexpr size_t PSRAM_LARGE_THRESHOLD_BYTES = 2 * 1024 * 1024;

// Default RX queue size for phone delivery when PSRAM is available
// This is an arbitrary default bump from default, boards can override
// this in board.h
static constexpr int RX_TOPHONE_WITH_PSRAM_DEFAULT = 100;

inline bool has_psram(size_t minimumBytes = PSRAM_LARGE_THRESHOLD_BYTES)
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    return memGet.getPsramSize() >= minimumBytes;
#else
    (void)minimumBytes;
    return false;
#endif
}

// Runtime cap used to keep the BLE message queue from overflowing low-memory
// S3 variants if PSRAM is smaller than expected or temporarily unavailable.
inline int get_rx_tophone_limit()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#if defined(BOARD_MAX_RX_TOPHONE)
    return BOARD_MAX_RX_TOPHONE;
#elif defined(BOARD_HAS_PSRAM)
    return RX_TOPHONE_WITH_PSRAM_DEFAULT;
#else
    return 32;
#endif
#elif defined(ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    return 8;
#else
    return 32;
#endif
}

/// max number of packets which can be waiting for delivery to android - note, this value comes from mesh.options protobuf
// FIXME - max_count is actually 32 but we save/load this as one long string of preencoded MeshPacket bytes - not a big array in
// RAM #define MAX_RX_TOPHONE (member_size(DeviceState, receive_queue) / member_size(DeviceState, receive_queue[0]))
#ifndef MAX_RX_TOPHONE
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#if defined(BOARD_MAX_RX_TOPHONE)
#define MAX_RX_TOPHONE BOARD_MAX_RX_TOPHONE
#elif defined(BOARD_HAS_PSRAM)
#define MAX_RX_TOPHONE RX_TOPHONE_WITH_PSRAM_DEFAULT
#else
#define MAX_RX_TOPHONE 32
#endif
#elif defined(ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
#define MAX_RX_TOPHONE 8
#else
#define MAX_RX_TOPHONE 32
#endif
#endif

/// max number of QueueStatus packets which can be waiting for delivery to phone
#ifndef MAX_RX_QUEUESTATUS_TOPHONE
#define MAX_RX_QUEUESTATUS_TOPHONE 2
#endif

/// max number of MqttClientProxyMessage packets which can be waiting for delivery to phone
#ifndef MAX_RX_MQTTPROXY_TOPHONE
#define MAX_RX_MQTTPROXY_TOPHONE 8
#endif

/// max number of ClientNotification packets which can be waiting for delivery to phone
#ifndef MAX_RX_NOTIFICATION_TOPHONE
#define MAX_RX_NOTIFICATION_TOPHONE 2
#endif

/// Verify baseline assumption of node size. If it increases, we need to reevaluate
/// the impact of its memory footprint, notably on MAX_NUM_NODES.
static_assert(sizeof(meshtastic_NodeInfoLite) <= 200, "NodeInfoLite size increased. Reconsider impact on MAX_NUM_NODES.");

/// max number of nodes allowed in the nodeDB
/// Note: With LSM storage, this is just the RAM cache size.
/// Total capacity is much larger (stored on flash via LSM).
#ifndef MAX_NUM_NODES
#if defined(ARCH_STM32WL)
#define MAX_NUM_NODES 50 // Increased from 10 (LSM provides flash storage)
#elif defined(ARCH_NRF52)
#define MAX_NUM_NODES 200 // Increased from 80 (LSM can handle 3000+ on flash)
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#if defined(BOARD_MAX_NUM_NODES)
#define MAX_NUM_NODES BOARD_MAX_NUM_NODES
#elif defined(BOARD_HAS_PSRAM)
#define MAX_NUM_NODES 3000 // Unchanged (PSRAM allows large cache)
#else
#define MAX_NUM_NODES 500 // Increased from 100-250 (LSM provides flash storage)
#endif
#else
// Other ESP32 platforms (ESP32, ESP32-C3, etc.)
#define MAX_NUM_NODES 500 // Increased from 100 (LSM provides flash storage)
#endif
#endif

/// Max number of channels allowed
#define MAX_NUM_CHANNELS (member_size(meshtastic_ChannelFile, channels) / member_size(meshtastic_ChannelFile, channels[0]))

/// helper function for encoding a record as a protobuf, any failures to encode are fatal and we will panic
/// returns the encoded packet size
size_t pb_encode_to_bytes(uint8_t *destbuf, size_t destbufsize, const pb_msgdesc_t *fields, const void *src_struct);

/// helper function for decoding a record as a protobuf, we will return false if the decoding failed
bool pb_decode_from_bytes(const uint8_t *srcbuf, size_t srcbufsize, const pb_msgdesc_t *fields, void *dest_struct);

/// Read from an Arduino File
bool readcb(pb_istream_t *stream, uint8_t *buf, size_t count);

/// Write to an arduino file
bool writecb(pb_ostream_t *stream, const uint8_t *buf, size_t count);

/** is_in_repeated is a macro/function that returns true if a specified word appears in a repeated protobuf array.
 * It relies on the following naming conventions from nanopb:
 *
 * pb_size_t ignore_incoming_count;
 * uint32_t ignore_incoming[3];
 */
bool is_in_helper(uint32_t n, const uint32_t *array, pb_size_t count);

#define is_in_repeated(name, n) is_in_helper(n, name, name##_count)
