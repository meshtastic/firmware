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

/// Verify baseline assumption of node size. If it increases, we need to reevaluate
/// the impact of its memory footprint, notably on NodeDB target caps.
static_assert(sizeof(meshtastic_NodeInfoLite) <= 200, "NodeInfoLite size increased. Reconsider NodeDB target caps.");

static constexpr size_t NODEDB_STM32WL_TARGET_CAP = 10;
static constexpr size_t NODEDB_NRF52_TARGET_CAP = 300;
static constexpr size_t NODEDB_ESP32_NO_PSRAM_TARGET_CAP = 500;
static constexpr size_t NODEDB_ESP32S3_PSRAM_TARGET_CAP = 3000;

#if defined(BOARD_HAS_PSRAM)
static constexpr size_t NODEDB_PSRAM_HEADROOM_BYTES = 512 * 1024;
static constexpr size_t NODEDB_HEAP_HEADROOM_BYTES = 96 * 1024;
static constexpr size_t NODEDB_ESTIMATED_DRAM_BYTES_PER_NODE = 32;
#endif

// `NODEDB_TARGET_CAP` is the build's upper bound. `MAX_NUM_NODES` remains as the
// compatibility macro consumed by the rest of the codebase and by Portduino overrides.
#if defined(MAX_NUM_NODES) && !defined(NODEDB_TARGET_CAP)
#define NODEDB_TARGET_CAP MAX_NUM_NODES
#endif

/// max number of nodes allowed in the nodeDB
#ifndef NODEDB_TARGET_CAP
#if defined(ARCH_STM32WL)
#define NODEDB_TARGET_CAP NODEDB_STM32WL_TARGET_CAP
#elif defined(ARCH_NRF52)
#define NODEDB_TARGET_CAP NODEDB_NRF52_TARGET_CAP
#elif defined(CONFIG_IDF_TARGET_ESP32S3) && defined(BOARD_HAS_PSRAM)
#define NODEDB_TARGET_CAP NODEDB_ESP32S3_PSRAM_TARGET_CAP
#elif defined(ARCH_ESP32)
#define NODEDB_TARGET_CAP NODEDB_ESP32_NO_PSRAM_TARGET_CAP
#else
#define NODEDB_TARGET_CAP 100
#endif
#endif

#ifndef MAX_NUM_NODES
#define MAX_NUM_NODES NODEDB_TARGET_CAP
#endif

/// Max number of channels allowed
#define MAX_NUM_CHANNELS (member_size(meshtastic_ChannelFile, channels) / member_size(meshtastic_ChannelFile, channels[0]))

// Traffic Management module configuration
// Enable per-variant by defining HAS_TRAFFIC_MANAGEMENT=1 in variant.h
#ifndef HAS_TRAFFIC_MANAGEMENT
#define HAS_TRAFFIC_MANAGEMENT 0
#endif

// Cache size for traffic management (number of nodes to track)
// Can be overridden per-variant based on available memory
#ifndef TRAFFIC_MANAGEMENT_CACHE_SIZE
#if HAS_TRAFFIC_MANAGEMENT
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 1000
#else
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 0
#endif
#endif

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
