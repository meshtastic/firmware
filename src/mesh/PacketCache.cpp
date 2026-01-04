#include "PacketCache.h"
#include "Router.h"

PacketCache packetCache{};

/**
 * Allocate a new cache entry and copy the packet header and payload into it
 */
PacketCacheEntry *PacketCache::cache(const meshtastic_MeshPacket *p, bool preserveMetadata)
{
    size_t payload_size =
        (p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) ? p->encrypted.size : p->decoded.payload.size;
    PacketCacheEntry *e = (PacketCacheEntry *)malloc(sizeof(PacketCacheEntry) + payload_size +
                                                     (preserveMetadata ? sizeof(PacketCacheMetadata) : 0));
    if (!e) {
        LOG_ERROR("Unable to allocate memory for packet cache entry");
        return NULL;
    }

    *e = {};
    e->header.from = p->from;
    e->header.to = p->to;
    e->header.id = p->id;
    e->header.channel = p->channel;
    e->header.next_hop = p->next_hop;
    e->header.relay_node = p->relay_node;
    e->header.flags = (p->hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) | (p->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0) |
                      (p->via_mqtt ? PACKET_FLAGS_VIA_MQTT_MASK : 0) |
                      ((p->hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);

    PacketCacheMetadata m{};
    if (preserveMetadata) {
        e->has_metadata = true;
        m.rx_rssi = (uint8_t)(p->rx_rssi + 200);
        m.rx_snr = (uint8_t)((p->rx_snr + 30.0f) / 0.25f);
        m.rx_time = p->rx_time;
        m.transport_mechanism = p->transport_mechanism;
        m.priority = p->priority;
    }
    if (p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        e->encrypted = true;
        e->payload_len = p->encrypted.size;
        memcpy(((unsigned char *)e) + sizeof(PacketCacheEntry), p->encrypted.bytes, p->encrypted.size);
    } else if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        e->encrypted = false;
        if (preserveMetadata) {
            m.portnum = p->decoded.portnum;
            m.want_response = p->decoded.want_response;
            m.emoji = p->decoded.emoji;
            m.bitfield = p->decoded.bitfield;
            if (p->decoded.reply_id)
                m.reply_id = p->decoded.reply_id;
            else if (p->decoded.request_id)
                m.request_id = p->decoded.request_id;
        }
        e->payload_len = p->decoded.payload.size;
        memcpy(((unsigned char *)e) + sizeof(PacketCacheEntry), p->decoded.payload.bytes, p->decoded.payload.size);
    } else {
        LOG_ERROR("Unable to cache packet with unknown payload type %d", p->which_payload_variant);
        free(e);
        return NULL;
    }
    if (preserveMetadata)
        memcpy(((unsigned char *)e) + sizeof(PacketCacheEntry) + e->payload_len, &m, sizeof(m));

    size += sizeof(PacketCacheEntry) + e->payload_len + (e->has_metadata ? sizeof(PacketCacheMetadata) : 0);
    insert(e);
    return e;
};

/**
 * Dump a list of packets into the provided buffer
 */
void PacketCache::dump(void *dest, const PacketCacheEntry **entries, size_t num_entries)
{
    unsigned char *pos = (unsigned char *)dest;
    for (size_t i = 0; i < num_entries; i++) {
        size_t entry_len =
            sizeof(PacketCacheEntry) + entries[i]->payload_len + (entries[i]->has_metadata ? sizeof(PacketCacheMetadata) : 0);
        memcpy(pos, entries[i], entry_len);
        pos += entry_len;
    }
}

/**
 * Calculate the length of buffer needed to dump the specified entries
 */
size_t PacketCache::dumpSize(const PacketCacheEntry **entries, size_t num_entries)
{
    size_t total_size = 0;
    for (size_t i = 0; i < num_entries; i++) {
        total_size += sizeof(PacketCacheEntry) + entries[i]->payload_len;
        if (entries[i]->has_metadata)
            total_size += sizeof(PacketCacheMetadata);
    }
    return total_size;
}

/**
 * Find a packet in the cache
 */
PacketCacheEntry *PacketCache::find(NodeNum from, PacketId id)
{
    uint16_t h = PACKET_HASH(from, id);
    PacketCacheEntry *e = buckets[PACKET_CACHE_BUCKET(h)];
    while (e) {
        if (e->header.from == from && e->header.id == id)
            return e;
        e = e->next;
    }
    return NULL;
}

/**
 * Find a packet in the cache by its hash
 */
PacketCacheEntry *PacketCache::find(PacketHash h)
{
    PacketCacheEntry *e = buckets[PACKET_CACHE_BUCKET(h)];
    while (e) {
        if (PACKET_HASH(e->header.from, e->header.id) == h)
            return e;
        e = e->next;
    }
    return NULL;
}

/**
 * Load a list of packets from the provided buffer
 */
bool PacketCache::load(void *src, PacketCacheEntry **entries, size_t num_entries)
{
    memset(entries, 0, sizeof(PacketCacheEntry *) * num_entries);
    unsigned char *pos = (unsigned char *)src;
    for (size_t i = 0; i < num_entries; i++) {
        PacketCacheEntry e{};
        memcpy(&e, pos, sizeof(PacketCacheEntry));
        size_t entry_len = sizeof(PacketCacheEntry) + e.payload_len + (e.has_metadata ? sizeof(PacketCacheMetadata) : 0);
        entries[i] = (PacketCacheEntry *)malloc(entry_len);
        size += entry_len;
        if (!entries[i]) {
            LOG_ERROR("Unable to allocate memory for packet cache entry");
            for (size_t j = 0; j < i; j++) {
                size -= sizeof(PacketCacheEntry) + entries[j]->payload_len +
                        (entries[j]->has_metadata ? sizeof(PacketCacheMetadata) : 0);
                free(entries[j]);
                entries[j] = NULL;
            }
            return false;
        }
        memcpy(entries[i], pos, entry_len);
        pos += entry_len;
    }
    for (size_t i = 0; i < num_entries; i++)
        insert(entries[i]);
    return true;
}

/**
 * Copy the cached packet into the provided MeshPacket structure
 */
void PacketCache::rehydrate(const PacketCacheEntry *e, meshtastic_MeshPacket *p)
{
    if (!e || !p)
        return;

    *p = {};
    p->from = e->header.from;
    p->to = e->header.to;
    p->id = e->header.id;
    p->channel = e->header.channel;
    p->next_hop = e->header.next_hop;
    p->relay_node = e->header.relay_node;
    p->hop_limit = e->header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    p->want_ack = !!(e->header.flags & PACKET_FLAGS_WANT_ACK_MASK);
    p->via_mqtt = !!(e->header.flags & PACKET_FLAGS_VIA_MQTT_MASK);
    p->hop_start = (e->header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
    p->which_payload_variant = e->encrypted ? meshtastic_MeshPacket_encrypted_tag : meshtastic_MeshPacket_decoded_tag;

    unsigned char *payload = ((unsigned char *)e) + sizeof(PacketCacheEntry);
    PacketCacheMetadata m{};
    if (e->has_metadata) {
        memcpy(&m, (payload + e->payload_len), sizeof(m));
        p->rx_rssi = ((int)m.rx_rssi) - 200;
        p->rx_snr = ((float)m.rx_snr * 0.25f) - 30.0f;
        p->rx_time = m.rx_time;
        p->transport_mechanism = (meshtastic_MeshPacket_TransportMechanism)m.transport_mechanism;
        p->priority = (meshtastic_MeshPacket_Priority)m.priority;
    }
    if (e->encrypted) {
        memcpy(p->encrypted.bytes, payload, e->payload_len);
        p->encrypted.size = e->payload_len;
    } else {
        memcpy(p->decoded.payload.bytes, payload, e->payload_len);
        p->decoded.payload.size = e->payload_len;
        if (e->has_metadata) {
            // Decrypted-only metadata
            p->decoded.portnum = (meshtastic_PortNum)m.portnum;
            p->decoded.want_response = m.want_response;
            p->decoded.emoji = m.emoji;
            p->decoded.bitfield = m.bitfield;
            if (m.reply_id)
                p->decoded.reply_id = m.reply_id;
            else if (m.request_id)
                p->decoded.request_id = m.request_id;
        }
    }
}

/**
 * Release a cache entry
 */
void PacketCache::release(PacketCacheEntry *e)
{
    if (!e)
        return;
    remove(e);
    size -= sizeof(PacketCacheEntry) + e->payload_len + (e->has_metadata ? sizeof(PacketCacheMetadata) : 0);
    free(e);
}

/**
 * Insert a new entry into the hash table
 */
void PacketCache::insert(PacketCacheEntry *e)
{
    assert(e);
    PacketHash h = PACKET_HASH(e->header.from, e->header.id);
    PacketCacheEntry **target = &buckets[PACKET_CACHE_BUCKET(h)];
    e->next = *target;
    *target = e;
    num_entries++;
}

/**
 * Remove an entry from the hash table
 */
void PacketCache::remove(PacketCacheEntry *e)
{
    assert(e);
    PacketHash h = PACKET_HASH(e->header.from, e->header.id);
    PacketCacheEntry **target = &buckets[PACKET_CACHE_BUCKET(h)];
    while (*target) {
        if (*target == e) {
            *target = e->next;
            e->next = NULL;
            num_entries--;
            break;
        } else {
            target = &(*target)->next;
        }
    }
}