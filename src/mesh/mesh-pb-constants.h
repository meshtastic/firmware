#pragma once
#include <vector>

#include "memory/MemClass.h"
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/localonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

// this file defines constants which come from mesh.options
//
// RAM-shaped cache tiers key off MESHTASTIC_MEM_CLASS (memory/MemClass.h) so
// unclassified chips fail safe-small; class-deviant branches say why inline.

// Tricky macro to let you find the sizeof a type member
#define member_size(type, member) sizeof(((type *)0)->member)

/// max number of packets which can be waiting for delivery to android - note, this value comes from mesh.options protobuf
// FIXME - max_count is actually 32 but we save/load this as one long string of preencoded MeshPacket bytes - not a big array in
// RAM #define MAX_RX_TOPHONE (member_size(DeviceState, receive_queue) / member_size(DeviceState, receive_queue[0]))
#ifndef MAX_RX_TOPHONE
#if defined(ARCH_ESP32) && !(defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3))
#define MAX_RX_TOPHONE 8
#elif defined(NRF52840_XXAA)
// Each slot is a ~340 B MeshPacket in the static pool (Router.cpp MAX_PACKETS_STATIC), so 32 slots
// cost ~11 KB of .bss on the RAM-tightest platform (2.8.0 field reports: 99% heap). 16 still doubles
// the 8 classic ESP32 has shipped with for years; drops start when a stalled phone/serial client has
// 16 packets queued.
#define MAX_RX_TOPHONE 16
#elif MESHTASTIC_MEM_CLASS >= MEM_CLASS_MEDIUM || defined(ARCH_RP2040) || defined(CONFIG_IDF_TARGET_ESP32C3) ||                  \
    defined(ARCH_STM32WL)
// RP2040/RP2350, ESP32-C3 and STM32WL keep their historical 32 (no field pressure to cut them;
// STM32WL's pool is dynamic, so the constant only bounds in-flight packets there).
#define MAX_RX_TOPHONE 32
#else
#define MAX_RX_TOPHONE 16 // unclassified small parts: fail safe-small
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

/// Packet-history capacity: 2x the hot store so dedup/relayer state survives a
/// full mesh, floored at 100. Shared by PacketHistory's constructor clamp and
/// the boot-cache budget assert below so the two cannot drift.
#ifndef PACKETHISTORY_MAX
#define PACKETHISTORY_MAX (MAX_NUM_NODES * 2 > 100 ? (uint32_t)(MAX_NUM_NODES * 2) : (uint32_t)100)
#endif

/// Per-map cap (position/telemetry/environment/status): only the freshest
/// MAX_SATELLITE_NODES nodes keep satellite payloads, the rest just the
/// NodeInfoLite header. RAM-bound (the maps are internal-SRAM, not PSRAM), so
/// flash-rich hosts get a cap >= their hot store (satellites for every node, as
/// before the cap existed) while constrained parts stay at 40.
#ifndef MAX_SATELLITE_NODES
#if MESHTASTIC_MEM_CLASS >= MEM_CLASS_LARGE
#define MAX_SATELLITE_NODES 250
#else
#define MAX_SATELLITE_NODES 40 // nRF52840, generic ESP32, and ESP32-S3 without PSRAM
#endif                         // platform
#endif                         // MAX_SATELLITE_NODES

/// Warm tier: 40 B {num, last_heard, public_key} records kept for evicted nodes
/// so DMs to/from them keep decrypting. 0 disables it; size is per-platform
/// below, persisted to /prefs/warm.dat (or the nRF52840 raw-flash ring).
#ifndef WARM_NODE_COUNT
#if MESHTASTIC_MEM_CLASS <= MEM_CLASS_TINY
#define WARM_NODE_COUNT 0
#elif defined(NRF52840_XXAA)
// Keyed on the NRF52840_XXAA build flag, not ARCH_NRF52: the latter (from
// architecture.h via configuration.h) isn't defined this early in every include
// chain. Backed by the raw-flash ring below LittleFS - see WarmNodeStore.h.
// 100 (was 200): the RAM cache is 40 B/entry calloc'd from the ~115 KB heap
// arena, which 2.8.0 field reports showed at 99% use; 120 hot + 100 warm
// identities still covers meshes well past the hot cap.
#define WARM_NODE_COUNT 100
#elif defined(ARCH_RP2040)
// Class-deviant on purpose: bounded so the warm.dat write fits the 8s watchdog (#10746),
// not by RAM (RP2040 264 KB / RP2350 520 KB could hold more).
#define WARM_NODE_COUNT 150
#elif MESHTASTIC_MEM_CLASS >= MEM_CLASS_LARGE
#define WARM_NODE_COUNT 2000 // PSRAM-equipped ESP32-S3 / native host; warm cache in PSRAM (~80 KB)
#elif MESHTASTIC_MEM_CLASS == MEM_CLASS_MEDIUM
#define WARM_NODE_COUNT 150 // 512 KB+ SRAM, no PSRAM (S3/C6/P4): ~6 KB heap (#10705)
#else
// MEM_CLASS_SMALL: classic ESP32/S2/C3 (~4 KB heap, #10705) and anything unclassified -
// fail small so a new RAM-constrained part can't boot-allocate 12.8 KB (2.8.0 lesson).
#define WARM_NODE_COUNT 100
#endif // platform
#endif // WARM_NODE_COUNT

/// Max number of channels allowed
#define MAX_NUM_CHANNELS (member_size(meshtastic_ChannelFile, channels) / member_size(meshtastic_ChannelFile, channels[0]))

// Traffic Management module configuration
// Enabled by default; TINY parts (STM32WL) are excluded due to RAM constraints (MAX_NUM_NODES=10).
// Disable per-variant by defining HAS_TRAFFIC_MANAGEMENT=0 in variant.h
#if MESHTASTIC_MEM_CLASS <= MEM_CLASS_TINY
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
#elif defined(NRF52840_XXAA)
// Class-deviant on purpose (SMALL would be 400): the 256 KB part shares its ~115 KB heap arena with
// SoftDevice and the FreeRTOS task stacks; 2.8.0 field reports hit 99% heap use with the old
// 1000-entry (~10 KB) cache. 250 entries (2.5 KB) still tracks >2x the 120-node hot store; LRU
// victim recycling absorbs busier meshes.
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 250
#elif MESHTASTIC_MEM_CLASS >= MEM_CLASS_LARGE
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 2048 // PSRAM-equipped ESP32-S3 / native host
#elif MESHTASTIC_MEM_CLASS == MEM_CLASS_MEDIUM
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 500 // 512 KB+ SRAM, no PSRAM (S3/C6/P4): ~5 KB heap (#10705)
#else
// MEM_CLASS_SMALL: classic ESP32/S2/C3 (~4 KB heap, #10705), RP2040/RP2350, and anything
// unclassified - fail small rather than defaulting large (2.8.0 lesson).
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 400
#endif
#endif // TRAFFIC_MANAGEMENT_CACHE_SIZE

// Enforce the per-class boot-cache budget (memory/MemClass.h) over the three big
// boot-allocated mesh caches. Entry sizes are pinned by static_asserts at their
// definitions: TMM UnifiedCacheEntry = 10 B (TrafficManagementModule.h),
// WarmNodeEntry = 40 B (WarmNodeStore.h), PacketHistory::PacketRecord = 20 B
// (PacketHistory.h; its optional hash index is excluded here). Skipped where
// MAX_NUM_NODES is not a compile-time constant: ESP32-S3 (runtime, from flash
// size) and portduino (runtime, from portduino_config.MaxNodes via variant.h).
// If this fires on a new feature: shrink the cache for this class, or raise the
// class budget in MemClass.h as a visible, reviewable decision.
#if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(ARCH_PORTDUINO)
static_assert((uint32_t)TRAFFIC_MANAGEMENT_CACHE_SIZE * 10u + (uint32_t)WARM_NODE_COUNT * 40u + 20u * PACKETHISTORY_MAX <=
                  MESHTASTIC_BOOT_CACHE_BUDGET,
              "Boot-allocated mesh caches exceed this memory class's budget - shrink a tier or consciously raise "
              "MESHTASTIC_BOOT_CACHE_BUDGET in memory/MemClass.h");
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
