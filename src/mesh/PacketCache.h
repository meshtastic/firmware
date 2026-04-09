#pragma once
#include "RadioInterface.h"

#define PACKET_HASH(a, b) ((((a ^ b) >> 16) ^ (a ^ b)) & 0xFFFF) // 16 bit fold of packet (from, id) tuple
typedef uint16_t PacketHash;

#define PACKET_CACHE_BUCKETS 64                                    // Number of hash table buckets
#define PACKET_CACHE_BUCKET(h) (((h >> 12) ^ (h >> 6) ^ h) & 0x3F) // Fold hash down to 6-bit bucket index

typedef struct PacketCacheEntry {
    PacketCacheEntry *next;
    PacketHeader header;
    uint16_t payload_len = 0;
    union {
        uint16_t bitfield;
        struct {
            uint8_t encrypted : 1;    // Payload is encrypted
            uint8_t has_metadata : 1; // Payload includes PacketCacheMetadata
            uint8_t : 6;              // Reserved for future use
            uint8_t : 8;              // Reserved for future use
        };
    };
} PacketCacheEntry;

typedef struct PacketCacheMetadata {
    PacketCacheMetadata() : _bitfield(0), reply_id(0), _bitfield2(0) {}
    union {
        uint32_t _bitfield;
        struct {
            uint16_t portnum : 9;       // meshtastic_MeshPacket::decoded::portnum
            uint16_t want_response : 1; // meshtastic_MeshPacket::decoded::want_response
            uint16_t emoji : 1;         // meshtastic_MeshPacket::decoded::emoji
            uint16_t bitfield : 5;      // meshtastic_MeshPacket::decoded::bitfield (truncated)
            uint8_t rx_rssi : 8;        // meshtastic_MeshPacket::rx_rssi (map via actual RSSI + 200)
            uint8_t rx_snr : 8;         // meshtastic_MeshPacket::rx_snr (map via (p->rx_snr + 30.0f) / 0.25f)
        };
    };
    union {
        uint32_t reply_id;   // meshtastic_MeshPacket::decoded.reply_id
        uint32_t request_id; // meshtastic_MeshPacket::decoded.request_id
    };
    uint32_t rx_time = 0;            // meshtastic_MeshPacket::rx_time
    uint8_t transport_mechanism = 0; // meshtastic_MeshPacket::transport_mechanism
    struct {
        uint8_t _bitfield2;
        union {
            uint8_t priority : 7; // meshtastic_MeshPacket::priority
            uint8_t reserved : 1; // Reserved for future use
        };
    };
} PacketCacheMetadata;

class PacketCache
{
  public:
    PacketCacheEntry *cache(const meshtastic_MeshPacket *p, bool preserveMetadata);
    static void dump(void *dest, const PacketCacheEntry **entries, size_t num_entries);
    size_t dumpSize(const PacketCacheEntry **entries, size_t num_entries);
    PacketCacheEntry *find(NodeNum from, PacketId id);
    PacketCacheEntry *find(PacketHash h);
    bool load(void *src, PacketCacheEntry **entries, size_t num_entries);
    size_t getNumEntries() { return num_entries; }
    size_t getSize() { return size; }
    void rehydrate(const PacketCacheEntry *e, meshtastic_MeshPacket *p);
    void release(PacketCacheEntry *e);

  private:
    PacketCacheEntry *buckets[PACKET_CACHE_BUCKETS]{};
    size_t num_entries = 0;
    size_t size = 0;
    void insert(PacketCacheEntry *e);
    void remove(PacketCacheEntry *e);
};

extern PacketCache packetCache;