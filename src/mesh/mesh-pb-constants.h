#pragma once
#include <vector>

#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/localonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

// this file defines constants which come from mesh.options

// Tricky macro to let you find the sizeof a type member
#define member_size(type, member) sizeof(((type *)0)->member)

/// max number of packets which can be waiting for delivery to android - note, this value comes from mesh.options protobuf
// FIXME - max_count is actually 32 but we save/load this as one long string of preencoded MeshPacket bytes - not a big array in
// RAM #define MAX_RX_TOPHONE (member_size(DeviceState, receive_queue) / member_size(DeviceState, receive_queue[0]))
#ifndef MAX_RX_TOPHONE
#if defined(ARCH_ESP32) && !(defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3))
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

/// Tighten this when the slim header shrinks; loosen only with deliberate
/// awareness of MAX_NUM_NODES impact per platform.
static_assert(sizeof(meshtastic_NodeInfoLite) <= 130, "NodeInfoLite size increased. Reconsider impact on MAX_NUM_NODES.");

// Compile satellite NodeDBs out on STM32WL (and the status DB also follows
// MESHTASTIC_EXCLUDE_STATUS).
#ifndef MESHTASTIC_EXCLUDE_POSITIONDB
#if defined(ARCH_STM32WL)
#define MESHTASTIC_EXCLUDE_POSITIONDB 1
#else
#define MESHTASTIC_EXCLUDE_POSITIONDB 0
#endif // STM32WL
#endif // MESHTASTIC_EXCLUDE_POSITIONDB

#ifndef MESHTASTIC_EXCLUDE_TELEMETRYDB
#if defined(ARCH_STM32WL)
#define MESHTASTIC_EXCLUDE_TELEMETRYDB 1
#else
#define MESHTASTIC_EXCLUDE_TELEMETRYDB 0
#endif // STM32WL
#endif // MESHTASTIC_EXCLUDE_TELEMETRYDB

#ifndef MESHTASTIC_EXCLUDE_ENVIRONMENTDB
#if defined(ARCH_STM32WL)
#define MESHTASTIC_EXCLUDE_ENVIRONMENTDB 1
#else
#define MESHTASTIC_EXCLUDE_ENVIRONMENTDB 0
#endif // STM32WL
#endif // MESHTASTIC_EXCLUDE_ENVIRONMENTDB

#ifndef MESHTASTIC_EXCLUDE_STATUSDB
#if defined(ARCH_STM32WL) || defined(MESHTASTIC_EXCLUDE_STATUS)
#define MESHTASTIC_EXCLUDE_STATUSDB 1
#else
#define MESHTASTIC_EXCLUDE_STATUSDB 0
#endif // STM32WL
#endif // MESHTASTIC_EXCLUDE_STATUSDB

/// Max nodes in the hot store (full NodeInfoLite). Evicted nodes' identities
/// live in the warm tier (WARM_NODE_COUNT). nRF52840 caps at 120 to keep
/// nodes.proto inside the stock 28 KB LittleFS; flash-rich platforms (ESP32-S3,
/// portduino) keep their larger hot store and lean on warm only for the tail.
#ifndef MAX_NUM_NODES
#if defined(ARCH_STM32WL)
#define MAX_NUM_NODES 10
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "Esp.h"
static inline int get_max_num_nodes()
{
    uint32_t flash_size = ESP.getFlashChipSize() / (1024 * 1024); // Convert Bytes to MB
    if (flash_size >= 15) {
        return 250;
    } else if (flash_size >= 7) {
        return 200;
    } else {
        return 100;
    }
}
#define MAX_NUM_NODES get_max_num_nodes()
#elif defined(ARCH_PORTDUINO)
#define MAX_NUM_NODES 250 // native host: no flash/RAM constraint; match the ESP32-S3 top tier
#else
#define MAX_NUM_NODES 120 // nRF52840 and generic ESP32 (inc. ESP32C3 etc.)
#endif                    // platform
#endif                    // MAX_NUM_NODES

/// Per-map cap (position/telemetry/environment/status): only the freshest
/// MAX_SATELLITE_NODES nodes keep satellite payloads, the rest just the
/// NodeInfoLite header. RAM-bound (the maps are internal-SRAM, not PSRAM), so
/// flash-rich hosts get a cap >= their hot store (satellites for every node, as
/// before the cap existed) while constrained parts stay at 40.
#ifndef MAX_SATELLITE_NODES
#if (defined(CONFIG_IDF_TARGET_ESP32S3) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
#define MAX_SATELLITE_NODES 250
#else
#define MAX_SATELLITE_NODES 40 // nRF52840, generic ESP32, and ESP32-S3 without PSRAM
#endif                         // platform
#endif                         // MAX_SATELLITE_NODES

/// Warm tier: 40 B {num, last_heard, public_key} records kept for evicted nodes
/// so DMs to/from them keep decrypting. 0 disables it; size is per-platform
/// below, persisted to /prefs/warm.dat (or the nRF52840 raw-flash ring).
#ifndef WARM_NODE_COUNT
#if defined(ARCH_STM32WL)
#define WARM_NODE_COUNT 0
#elif defined(NRF52840_XXAA)
// Keyed on the NRF52840_XXAA build flag, not ARCH_NRF52: the latter (from
// architecture.h via configuration.h) isn't defined this early in every include
// chain. Backed by the raw-flash ring below LittleFS — see WarmNodeStore.h.
#define WARM_NODE_COUNT 200
#elif (defined(CONFIG_IDF_TARGET_ESP32S3) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
#define WARM_NODE_COUNT 2000 // PSRAM-equipped ESP32-S3 / native host; warm.dat ~80 KB
#else
#define WARM_NODE_COUNT 320 // Generic ESP32, ESP32-S3 without PSRAM, ESP32C3 etc.
#endif                      // platform
#endif                      // WARM_NODE_COUNT

/// Max number of channels allowed
#define MAX_NUM_CHANNELS (member_size(meshtastic_ChannelFile, channels) / member_size(meshtastic_ChannelFile, channels[0]))

// Traffic Management module configuration
// Enabled by default; STM32WL is excluded due to RAM constraints (MAX_NUM_NODES=10).
// Disable per-variant by defining HAS_TRAFFIC_MANAGEMENT=0 in variant.h
#ifdef ARCH_STM32WL
#define HAS_TRAFFIC_MANAGEMENT 0
#endif
#ifndef HAS_TRAFFIC_MANAGEMENT
#define HAS_TRAFFIC_MANAGEMENT 1
#endif

// HopScalingModule - variable hop module: dynamically adjusts broadcast hop_limit based on mesh density
// Enable per-variant by defining HAS_VARIABLE_HOPS=1 in variant.h
#ifdef ARCH_STM32WL
#define HAS_VARIABLE_HOPS 0
#endif

#ifndef HAS_VARIABLE_HOPS
#define HAS_VARIABLE_HOPS 1
#endif

// Cache size for traffic management (number of nodes to track)
// Can be overridden per-variant by defining before this header is included.
#ifndef TRAFFIC_MANAGEMENT_CACHE_SIZE
#if !HAS_TRAFFIC_MANAGEMENT
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 0
#elif (defined(CONFIG_IDF_TARGET_ESP32S3) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 2048 // PSRAM-equipped ESP32-S3 / native host
#else
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 1000 // Generic ESP32, ESP32-S3 without PSRAM
#endif
#endif // TRAFFIC_MANAGEMENT_CACHE_SIZE

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
