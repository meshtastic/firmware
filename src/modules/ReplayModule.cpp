#include "ReplayModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include "memGet.h"

/**
 * TODO:
 * - Which tunables should be configurable?
 * - What should the tunable default values be?
 * - Scale replay rate based on modem settings
 * - Prioritise replay of packets requested by routers
 * - Lots of testing (and likely a bunch of bugfixes)
 *   - WARN  | 23:30:46 4214 [Router] Replay: Advertisement sequence went backwards from server=0x056191db seq=36, last_seq=48
 *   - Back off repeated replay requests?
 * - Implement a periodic stats packet that includes:
 *   - Number of adverts sent
 *   - Number of replays sent
 *   - Number of replay requests received
 *   - Number of replays requested
 *   - Number of adverts received
 *   - For each server we are tracking:
 *     - Age of last advert
 *     - Number of adverts received
 *     - Number of packets requested from this server
 *     - Number of packets requested by this server
 *     - router flag
 */

ReplayModule *replayModule = NULL;

/**
 * Copy a packet into the replay buffer
 */
ReplayEntry *ReplayBuffer::adopt(meshtastic_MeshPacket *p)
{
    if (p->is_replay_cached || search(p))
        return NULL; // Already cached

    // Free the tail entry before overwriting it
    if (next && (next & REPLAY_BUFFER_MASK) == (last & REPLAY_BUFFER_MASK)) {
        ReplayEntry *oldEntry = &entries[(last++) & REPLAY_BUFFER_MASK];
        if (oldEntry->p) {
            meshtastic_MeshPacket *oldp = oldEntry->p;
            oldEntry->p = NULL;
            oldp->is_replay_cached = false;
            packets.release(oldp);
            num_cached--;
        }
    }

    // Ensure we don't use too much memory
    unsigned int cacheHeapPct = getNumCached() * sizeof(meshtastic_MeshPacket) * 100 / memGet.getHeapSize();
    unsigned int freeHeapPct = (memGet.getFreeHeap() * 100) / memGet.getHeapSize();
    if (cacheHeapPct >= REPLAY_HEAP_THRESHOLD_PCT && freeHeapPct < REPLAY_HEAP_FREE_MIN_PCT) {
        unsigned int wantPct = REPLAY_HEAP_FREE_TARGET_PCT - freeHeapPct;
        if (wantPct > cacheHeapPct - REPLAY_HEAP_RESERVE_PCT)
            wantPct = cacheHeapPct - REPLAY_HEAP_RESERVE_PCT;
        unsigned int reduceBy = (wantPct * memGet.getHeapSize()) / (sizeof(meshtastic_MeshPacket) * 100);
        LOG_DEBUG("Replay: Pruning %u packets from the replay cache to reduce memory pressure", reduceBy);
        prune(getNumCached() - reduceBy);
    } else if (num_cached >= REPLAY_BUFFER_CACHE_MAX) {
        prune(REPLAY_BUFFER_CACHE_MAX - 1);
    }

    // Add the new entry
    meshtastic_MeshPacket *newPacket = packets.allocCopy(*p);
    newPacket->is_replay_cached = true;
    ReplayEntry *newEntry = &entries[next++ & REPLAY_BUFFER_MASK];
    *newEntry = {};
    newEntry->hash = REPLAY_HASH(p->from, p->id);
    newEntry->p = newPacket;
    num_cached++;
    LOG_DEBUG("Replay: packets=%u, cached=%u, cache=%lu, heap=%lu, heap_free=%lu, last=%lu, next=%lu", getLength(),
              getNumCached(), num_cached * sizeof(meshtastic_MeshPacket), memGet.getHeapSize(), memGet.getFreeHeap(), last, next);

    return newEntry;
}

/**
 * Search for a packet in the replay buffer by its hash
 */
ReplayEntry *ReplayBuffer::search(ReplayHash hash)
{
    if (next == last)
        return NULL; // The buffer is empty
    for (unsigned int i = next + REPLAY_BUFFER_SIZE - 1; i >= last + REPLAY_BUFFER_SIZE; i--) {
        unsigned int idx = i & REPLAY_BUFFER_MASK;
        if (entries[idx].hash == hash)
            return &entries[idx];
    }
    return NULL;
}

/**
 * Search for a packet in the replay buffer by its (from,id) tuple
 */
ReplayEntry *ReplayBuffer::search(NodeNum from, uint32_t id)
{
    if (next == last)
        return NULL; // The buffer is empty
    for (unsigned int i = next + REPLAY_BUFFER_SIZE - 1; i >= last + REPLAY_BUFFER_SIZE; i--) {
        unsigned int idx = i & REPLAY_BUFFER_MASK;
        if (!entries[idx].p)
            continue; // This entry does not have an associated cached packet
        if (entries[idx].p->id == id && entries[idx].p->from == from)
            return &entries[idx];
    }
    return NULL;
}

/**
 * Search for a packet in the replay buffer by its pointer
 * If strict, only an exact pointer match will do. Otherwise matches on (from,id) tuple.
 */
ReplayEntry *ReplayBuffer::search(meshtastic_MeshPacket *p, bool strict)
{
    if (!p)
        return NULL; // Invalid search pointer
    if (next == last)
        return NULL; // The buffer is empty
    if (!strict)
        return search(p->from, p->id);
    for (unsigned int i = next + REPLAY_BUFFER_SIZE - 1; i >= last + REPLAY_BUFFER_SIZE; i--) {
        unsigned int idx = i & REPLAY_BUFFER_MASK;
        if (entries[idx].p == p)
            return &entries[idx];
    }
    return NULL;
}

/**
 * Prune the replay buffer to contain no more than 'keep' cached packets
 * Hashes are still retained.
 */
void ReplayBuffer::prune(unsigned int keep)
{
    if (getLength() <= keep)
        return; // Nothing to do
    unsigned int priority[meshtastic_MeshPacket_Priority_MAX + 1] = {};
    // Count the number of packets at each priority level
    for (unsigned int i = last; i < next; i++) {
        unsigned int idx = i & REPLAY_BUFFER_MASK;
        if (entries[idx].p) {
            priority[entries[idx].p->priority]++;
        }
    }
    if (num_cached <= keep)
        return; // Nothing to do
    size_t threshold = 0;
    // Find the lowest priority threshold that will release enough packets
    for (unsigned int prunable = 0; threshold <= meshtastic_MeshPacket_Priority_MAX;) {
        prunable += priority[threshold];
        if (num_cached - prunable <= keep)
            break;
        threshold++;
    }
    // Release all packets at or below the priority threshold until we have pruned enough
    for (unsigned int i = last; i < next && num_cached > keep; i++) {
        unsigned int idx = i & REPLAY_BUFFER_MASK;
        if (entries[idx].p && entries[idx].p->priority <= threshold) {
            meshtastic_MeshPacket *p = entries[idx].p;
            entries[idx].p = NULL;
            p->is_replay_cached = false;
            packets.release(p);
            num_cached--;
        }
    }
}

/**
 * Truncate the replay buffer to contain no more than the most recent 'keep' entries
 * Both metadata and cached packets are released
 */
void ReplayBuffer::truncate(unsigned int keep)
{
    while (getLength() > keep) {
        ReplayEntry *oldEntry = &entries[last++ & REPLAY_BUFFER_MASK];
        if (oldEntry->p) {
            meshtastic_MeshPacket *oldp = oldEntry->p;
            oldEntry->p = NULL;
            oldp->is_replay_cached = false;
            packets.release(oldp);
            num_cached--;
        }
    }
}

/**
 * Add a packet to the replay buffer
 */
void ReplayModule::adopt(meshtastic_MeshPacket *p)
{
    if (p->decoded.portnum == meshtastic_PortNum_REPLAY_APP)
        return; // Don't cache replay packets

    ReplayEntry *entry = buffer.adopt(p);
    if (!entry)
        return; // Already cached

    LOG_DEBUG("Replay: Adopting packet from=0x%08x id=0x%08x priority=%u packets=%u cached=%u cache_bytes=%u", p->from, p->id,
              p->priority, buffer.getLength(), buffer.getNumCached(), buffer.getNumCached() * sizeof(meshtastic_MeshPacket));
    unsigned int idx = buffer.getHeadCursor() & REPLAY_BUFFER_MASK;
    want_replay.reset(idx);
    dirty.set(idx);
    if (p->priority >= REPLAY_CHUTIL_PRIORITY)
        dirty_prio.set(idx);
    packets_since_advert++;
    notify(REPLAY_NOTIFY_ADOPT, true);
}

/**
 * Check if we have seen this packet before
 */
bool ReplayModule::isKnown(ReplayHash hash)
{
    for (ReplayCursor i = memory_next + REPLAY_REMEMBER_SIZE; i >= memory_next; i--) {
        if (memory[i & REPLAY_REMEMBER_MASK] == hash) {
            return true;
        }
    }
    return false;
}

/**
 * Get client metadata entry
 */
ReplayClientInfo *ReplayModule::client(NodeNum id)
{
    ReplayClientInfo *target = clients;
    for (unsigned int i = 0; i < REPLAY_CLIENT_SIZE; i++) {
        if (clients[i].id == id) {
            return &clients[i];
            unsigned int bucket = clients[i].bucket + ((millis() - clients[i].last_request_millis) / REPLAY_CLIENT_RATE_MS);
            if (bucket >= REPLAY_CLIENT_BURST && clients[i].last_request_millis < target->last_request_millis)
                target = &clients[i];
        }
        if (target->id != id) {
            *target = {};
            target->id = id;
        }
    }
    return target;
}

/**
 * Send an advertisement of available packets
 */
void ReplayModule::advertise(bool aggregate, unsigned int from_sequence, ReplayMap aggregate_mask)
{
    LOG_INFO("Replay: Triggered advertisement: dirty=%u, dirty_prio=%u, packets_since_advert=%u, seq=%u", dirty.count(),
             dirty_prio.count(), packets_since_advert, (next_sequence + 1) & REPLAY_SEQUENCE_MASK);
    if (last_advert_cursor < buffer.getTailCursor())
        last_advert_cursor = buffer.getTailCursor(); // Clamp the advertisement cursor to the start of the buffer
    if (last_advert_cursor >= buffer.getHeadCursor() && !aggregate)
        return; // No new packets since last advertisement & not responding to an aggregate request
    ReplayWire wire = {};
    wire.header.type = REPLAY_ADVERT_TYPE_AVAILABLE;
    wire.header.priority = (airTime->channelUtilizationPercent() >= REPLAY_CHUTIL_THRESHOLD_PCT);
    wire.header.boot = !last_advert_millis;
    wire.header.router = IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER,
                                   meshtastic_Config_DeviceConfig_Role_ROUTER_LATE);
    wire.header.aggregate = aggregate;

    uint8_t throttled_clients[REPLAY_CLIENT_SIZE] = {};
    uint8_t throttled_count = 0;
    for (unsigned int i = 0; i < REPLAY_CLIENT_SIZE; i++) {
        ReplayClientInfo *client = &clients[i];
        if (!(client->bucket + ((millis() - client->last_request_millis) / REPLAY_CLIENT_RATE_MS))) {
            LOG_DEBUG("Replay: Telling client 0x%08x not to make requests", client->id);
            throttled_clients[throttled_count++] = client->id & 0x000F;
        }
        if (throttled_count >= REPLAY_CLIENT_THROTTLE_ADVERT_MAX)
            break;
    }
    if (throttled_count)
        wire.header.throttle = 1;

    last_advert_millis = millis();
    std::bitset<REPLAY_BUFFER_SIZE> b = {};
    ReplayMap aggregate_mask_local = 0;
    if (aggregate) {
        LOG_INFO("Replay: Sending aggregate advertisement from_sequence=%u mask=0x%04x", from_sequence, aggregate_mask);
        wire.header.sequence = from_sequence & REPLAY_SEQUENCE_MASK;
        b.reset();
        for (unsigned int bit = 0; bit < 15; bit++) {
            if (!(aggregate_mask & (1 << bit)))
                continue;
            ReplayAdvertisement *record = &advertisements[(from_sequence + bit) & REPLAY_SEQUENCE_MASK];
            if (b.count() + record->dirty.count() > REPLAY_ADVERT_MAX_PACKETS) {
                LOG_DEBUG("Replay: Requested aggregate exceeds max packets per advert");
                break; // Avoid exceeding the maximum number of packets in a single advert
            }
            for (unsigned int i = record->tail; i <= record->head && i < buffer.getTailCursor(); i++) {
                record->dirty.reset(i & REPLAY_BUFFER_MASK); // Clear expired packets
            }
            b |= record->dirty;
            aggregate_mask_local |= (1 << bit);
        }
        for (unsigned int i = 0; i < REPLAY_BUFFER_SIZE; i++) {
            if (b.test(i)) {
                if (!buffer.get(i)->p)
                    b.reset(i); // Don't advertise pruned entries
                else if (wire.header.priority && buffer.get(i)->p->priority < REPLAY_CHUTIL_PRIORITY)
                    b.reset(i); // Don't advertise non-priority entries
            }
        }
    } else {
        wire.header.sequence = next_sequence & REPLAY_SEQUENCE_MASK;
        b = wire.header.priority ? dirty_prio : dirty;
        ReplayAdvertisement *record = &advertisements[wire.header.sequence];
        record->sequence = next_sequence;
        record->head = buffer.getHeadCursor();
        record->tail = buffer.getTailCursor();
        record->dirty = dirty;
    }

    uint16_t ranges = 0;
    unsigned int packets = 0;
    bool again = false;
    for (unsigned int i = 0; i < REPLAY_BUFFER_SIZE; i++) {
        const unsigned int bit = i / 16;
        if (b.test(i) && buffer.get(i)->p) {
            ranges |= (1 << bit);
            packets++;
            if (packets >= REPLAY_ADVERT_MAX_PACKETS) {
                again = true;
                break;
            }
        }
    }
    if (!ranges)
        return; // No cached dirty packets at current priority level

    const unsigned int payload_max =
        __builtin_popcount(ranges) * 2 + packets + 1 /*ranges*/ + 1 /*header*/ + (aggregate ? 1 : 0) /*aggregate mask*/;
    uint16_t payload[payload_max] = {wire.header.bitfield, ranges};
    off_t payload_cursor = 2;
    for (unsigned int bit = 0; bit < 16; bit++) {
        if (!(ranges & (1 << bit)))
            continue;
        ReplayMap *packet_map = &payload[payload_cursor++];
        ReplayMap *priority_map = &payload[payload_cursor++];
        for (unsigned int j = 0; j < 16; j++) {
            unsigned int idx = bit * 16 + j;
            ReplayEntry *entry = buffer.get(idx);
            if (!b.test(idx) || !entry->p)
                continue;
            LOG_DEBUG("Advertising packet hash=0x%04x from=0x%08x id=0x%08x", entry->hash, entry->p->from, entry->p->id);
            payload[payload_cursor++] = entry->hash;
            *packet_map |= (1 << j);
            if (entry->p->priority >= REPLAY_CHUTIL_PRIORITY)
                *priority_map |= (1 << j);
            dirty.reset(idx);
            dirty_prio.reset(idx);
        }
    }
    if (aggregate)
        payload[payload_cursor++] = aggregate_mask_local;
    else
        next_sequence++;

    if (throttled_count) {
        uint8_t *payload_throttled = (uint8_t *)&payload[payload_cursor];
        for (unsigned int i = 0; i < throttled_count; i++) {
            payload_throttled[i] = throttled_clients[i];
        }
        payload_cursor += throttled_count / 2 + (throttled_count & 0x01);
    }

    LOG_INFO("Replay: Advertising %u of %u/%u cached packets (chutil=%4.2f%%)", packets, buffer.getNumCached(),
             buffer.getLength(), airTime->channelUtilizationPercent());
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    p->priority = meshtastic_MeshPacket_Priority_REPLAY;
    p->hop_limit = 0;
    p->decoded.payload.size = payload_cursor * sizeof(uint16_t);
    memcpy(p->decoded.payload.bytes, payload, p->decoded.payload.size);

    last_advert_cursor = buffer.getHeadCursor();
    if (!aggregate)
        packets_since_advert -= packets;
    service->sendToMesh(p);

    if (again) {
        advertise();
    }
}

/**
 * Send an advertisement of expired packets (i.e. which packets have been pruned from the cache)
 */
void ReplayModule::advertiseExpired()
{
    ReplayWire wire = {};
    wire.header.type = REPLAY_ADVERT_TYPE_EXPIRED;
    wire.header.priority = (airTime->channelUtilizationPercent() >= REPLAY_CHUTIL_THRESHOLD_PCT);
    wire.header.boot = !last_advert_millis;
    wire.header.router = IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER,
                                   meshtastic_Config_DeviceConfig_Role_ROUTER_LATE);

    uint16_t payload[18] = {wire.header.bitfield};
    ReplayMap *map = &payload[1];
    ReplayMap *range = &payload[2];

    for (unsigned int i = 0; i < REPLAY_BUFFER_SIZE; i++) {
        const unsigned int bit = i / 16;
        if (!(i & 0x04) && *range)
            range++;
        if (!buffer.get(i)->p)
            *range |= (1 << (i & 0x04));
    }

    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    p->priority = meshtastic_MeshPacket_Priority_REPLAY;
    p->hop_limit = 0;
    p->decoded.payload.size = sizeof(uint16_t) * (1 /*header*/ + 1 /*map*/ + __builtin_popcount(*map) /*ranges*/);
    memcpy(p->decoded.payload.bytes, &payload, p->decoded.payload.size);

    service->sendToMesh(p);
    want_replay_expired = false;
}

/**
 * Send the next pending packet for which a replay has been requested, priority packets first
 */
void ReplayModule::replay()
{
    LOG_WARN("Replay: Triggered replay: from=%u, want_replay=%u, want_replay_prio=%u, want_replay_expired=%u", replay_from,
             want_replay.count(), want_replay_prio, want_replay_expired);

    if (!replay_from)
        return; // No replay in progress

    if (want_replay_expired && last_expired_millis + REPLAY_EXPIRED_SPACING_SECS * 1000 < millis()) {
        advertiseExpired();
        return;
    }

    if (!want_replay.any()) {
        LOG_DEBUG("Replay: There is nothing left to replay");
        replay_from = 0; // All done
        return;
    }

    ReplayEntry *to_send = NULL;
    ReplayCursor to_send_idx = 0;
    for (ReplayCursor i = replay_from + REPLAY_BUFFER_SIZE; !to_send && i >= buffer.getTailCursor() + REPLAY_BUFFER_SIZE; i--) {
        // Replay priority packets first
        ReplayCursor idx = i & REPLAY_BUFFER_MASK;
        if (want_replay.test(idx)) {
            ReplayEntry *entry = buffer.get(idx);
            if (!entry->p)
                want_replay_expired = true;
            else if (want_replay_prio && !(entry->p->priority >= REPLAY_CHUTIL_PRIORITY)) {
                if (entry->last_replay_millis > last_advert_millis)
                    continue; // Already replayed this packet since last advert
                to_send = entry;
                to_send_idx = idx;
            }
        }
    }
    if (!to_send && airTime->channelUtilizationPercent() < REPLAY_CHUTIL_THRESHOLD_PCT) {
        // No more priority packets to send, so now send non-priority packets if chutil allows
        want_replay_prio = false;
        for (ReplayCursor i = replay_from + REPLAY_BUFFER_SIZE; !to_send && i >= buffer.getTailCursor() + REPLAY_BUFFER_SIZE;
             i--) {
            ReplayCursor idx = i & REPLAY_BUFFER_MASK;
            if (want_replay.test(idx) && buffer.get(idx)->p) {
                ReplayEntry *entry = buffer.get(idx);
                if (entry->last_replay_millis > last_advert_millis)
                    continue; // Already replayed this packet since last advert
                to_send = entry;
                to_send_idx = idx;
            }
        }
    }

    if (to_send) {
        LOG_INFO("Replay: Replaying packet hash=0x%04x from=0x%08x id=0x%08x count=%u", to_send->hash, to_send->p->from,
                 to_send->p->id, to_send->replay_count + 1);
        // router->rawSend(to_send->p);
        if (!queuePush(to_send_idx)) {
            LOG_WARN("Replay: Unable to queue replay packet hash=0x%04x from=0x%08x id=0x%08x: queue full", to_send->hash,
                     to_send->p->from, to_send->p->id);
            return;
        } else {
            to_send->last_replay_millis = millis();
            to_send->replay_count++;
            want_replay.reset(to_send_idx);
        }
    } else {
        LOG_DEBUG("Triggered replay, but there is nothing to send");
        replay_from = 0; // All done
    }
}

/**
 * Request replay of missing packets from a server
 */
void ReplayModule::requestReplay(ReplayServerInfo *server)
{
    std::bitset<REPLAY_BUFFER_SIZE> request = server->missing & server->available;
    if (server->flag_priority)
        request &= server->priority;
    if (!request.any())
        return; // Nothing to request
    if (server->last_advert_millis + REPLAY_SERVER_STALE_SECS * 1000 < millis()) {
        LOG_DEBUG("Replay: Cancelling requests for missing packets from stale server=0x%08x", server->id);
        invalidateServer(server);
        return;
    }
    unsigned long request_millis = millis() + REPLAY_REQUEST_TIMEOUT_SECS * 1000;
    ReplayRequestInfo *requests[REPLAY_BUFFER_SIZE] = {};
    for (int i = 0; i < REPLAY_BUFFER_SIZE; i++) {
        if (request.test(i)) {
            if (isKnown(server->packets[i])) {
                server->missing.reset(i);
                request.reset(i);
                continue;
            }
            ReplayRequestInfo *r = requestInfo(server->packets[i]);
            if (!r) {
                LOG_WARN("Replay: Not requesting missing packet 0x%04x from server 0x%08x: too many outstanding requests",
                         server->packets[i], server->id);
                request.reset(i);
            } else if (r->timeout_millis >= millis()) {
                LOG_DEBUG("Replay: Not requesting missing packet 0x%04x from server 0x%08x: already requested this recently",
                          server->packets[i], server->id);
                request.reset(i);
            } else {
                requests[i] = r;
                r->timeout_millis = request_millis;
            }
        }
    }
    if (!request.any())
        return; // Nothing to request
    int requested = request.count();
    if (requested > REPLAY_REQUEST_MAX_PACKETS) {
        // Limit the number of requested packets to avoid overloading the server
        for (int i = 0; i < REPLAY_BUFFER_SIZE && requested > REPLAY_REQUEST_MAX_PACKETS; i++) {
            if (request.test(i) && !server->priority.test(i)) {
                // Skip non-priority packets first
                request.reset(i);
                requested--;
                requests[i]->timeout_millis = 0;
            }
        }
        for (int i = 0; i < REPLAY_BUFFER_SIZE && requested > REPLAY_REQUEST_MAX_PACKETS; i++) {
            if (request.test(i)) {
                request.reset(i);
                requested--;
                requests[i]->timeout_millis = 0;
            }
        }
    }

    ReplayWire wire = {};
    wire.header.type = REPLAY_REQUEST_TYPE_PACKETS;
    wire.header.priority = airTime->channelUtilizationPercent() >= REPLAY_CHUTIL_THRESHOLD_PCT;
    wire.header.router = IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER,
                                   meshtastic_Config_DeviceConfig_Role_ROUTER_LATE);
    wire.header.sequence = server->last_sequence; // Echo the server's last sequence number for tracking & future-proofing

    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = server->id;
    p->priority = meshtastic_MeshPacket_Priority_REPLAY;
    p->hop_limit = 0;
    uint16_t *payload = (uint16_t *)p->decoded.payload.bytes;
    *payload++ = wire.header.bitfield;
    ReplayMap *map = payload++;
    for (unsigned int i = 0; i < 16; i++) {
        for (unsigned int j = 0; j < 16; j++) {
            unsigned int idx = i * 16 + j;
            if (request.test(idx)) {
                LOG_DEBUG("Replay: Requesting replay of packet hash=0x%04x via=0x%08x", server->packets[idx], server->id);
                *map |= (1 << i);
                *payload |= (1 << j);
                server->replays_requested++;
            }
        }
        if (*map & (1 << i))
            payload++;
    }
    p->decoded.payload.size = (payload - (uint16_t *)p->decoded.payload.bytes) * sizeof(uint16_t);

    LOG_INFO("Replay: Requesting %u missing packets server=0x%08x prio=%u ranges=%u size=%u", request.count(), server->id,
             wire.header.priority, (uint16_t)*map, p->decoded.payload.size);
    service->sendToMesh(p);
}

/**
 * Request that a server send an aggregate advertisement covering specific prior adverts that have been missed
 */
void ReplayModule::requestMissingAdvertisements(ReplayServerInfo *server)
{
    if (!server->missing_sequence)
        return; // Nothing to request
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = server->id;
    p->priority = meshtastic_MeshPacket_Priority_REPLAY;
    p->hop_limit = 0;
    uint16_t *payload = (uint16_t *)p->decoded.payload.bytes;

    ReplayWire wire = {};
    wire.header.type = REPLAY_REQUEST_TYPE_ADVERTISEMENT;
    wire.header.priority = airTime->channelUtilizationPercent() >= REPLAY_CHUTIL_THRESHOLD_PCT;
    wire.header.router = IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER,
                                   meshtastic_Config_DeviceConfig_Role_ROUTER_LATE);
    wire.header.sequence = server->last_sequence;
    *payload++ = wire.header.bitfield;
    *payload++ = server->missing_sequence & 0xFFFF;
    p->decoded.payload.size = (unsigned char *)payload - p->decoded.payload.bytes;

    LOG_INFO("Replay: Requesting missing advertisements from server=0x%08x sequence=%lu missing=%u", server->id,
             server->last_sequence, server->missing_sequence);
    service->sendToMesh(p);
}

/**
 * Handle all incoming replay protocol packets
 */
ProcessMessage ReplayModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.decoded.payload.size < sizeof(uint16_t))
        return ProcessMessage::STOP; // Not enough data for even the header
    if (mp.hop_limit != mp.hop_start) {
        LOG_DEBUG("Replay: Ignoring indirect packet from=0x%08x hop_limit=%u hop_start=%u", mp.from, mp.hop_limit, mp.hop_start);
        return ProcessMessage::STOP; // Replay packets must be from a direct neighbor
    }
    if (isFromUs(&mp))
        return ProcessMessage::STOP; // Ignore our own packets

    else if (isToUs(&mp))
        handleRequest(&mp);
    else
        handleAdvertisement(&mp);

    return ProcessMessage::STOP;
}

/**
 * Handle a replay request packet
 */
void ReplayModule::handleRequest(const meshtastic_MeshPacket *p)
{
    uint16_t *payload = (uint16_t *)p->decoded.payload.bytes;
    int payload_words = p->decoded.payload.size / sizeof(uint16_t);
    ReplayWire *wire = (ReplayWire *)payload;
    LOG_INFO("Replay: Received request from=0x%08x size=%u type=%u", p->from, p->decoded.payload.size, wire->header.type);

    ReplayClientInfo *client = this->client(p->from);
    client->bucket += (millis() - client->last_request_millis) / REPLAY_CLIENT_RATE_MS;
    if (client->bucket > REPLAY_CLIENT_BURST)
        client->bucket = REPLAY_CLIENT_BURST;
    client->last_request_millis = millis();

    switch (wire->header.type) {
    case REPLAY_REQUEST_TYPE_ADVERTISEMENT: {
        if (payload_words < 2) {
            LOG_WARN("Replay: Advertisement request payload too small");
            break;
        }
        uint32_t missing = payload[1];
        if (!missing) {
            LOG_WARN("Replay: Advertisement request for zero missing advertisements");
            break;
        }
        LOG_INFO("Replay: Advertisement request from=0x%08x seq=%u missing=%u", p->from, wire->header.sequence, missing);
        advertise(true, wire->header.sequence, missing);
    } break;
    case REPLAY_REQUEST_TYPE_PACKETS: {
        if (payload_words < 3 || payload_words < 1 /*header*/ + 1 /*map*/ + __builtin_popcount(payload[1]) /*ranges*/) {
            LOG_WARN("Replay: Packet request payload too small");
            break;
        }
        ReplayMap map = payload[1];
        ReplayMap *range = &payload[2];
        unsigned int requested = 0;
        for (unsigned int i = 0; i < 16; i++) {
            if (!(map & (1 << i)))
                continue;
            for (unsigned int j = 0; j < 16; j++) {
                if (*range & (1 << j)) {
                    if (!client->bucket)
                        break;
                    ReplayCursor idx = (i * 16 + j) & REPLAY_BUFFER_MASK;
                    ReplayEntry *entry = buffer.get(idx);
                    if (router->findInTxQueue(entry->p->from, entry->p->id))
                        continue; // Don't replay packets that are already in our TX queue
                    if (!wire->header.priority || (entry->p && entry->p->priority >= REPLAY_CHUTIL_PRIORITY)) {
                        want_replay.set(idx);
                        requested++;
                        client->bucket--;
                        LOG_INFO("Replay: Request for %s packet hash=0x%04x client=0x%08x", entry->p ? "cached" : "expired",
                                 entry->hash, client->id);
                        if (!entry->p)
                            want_replay_expired = true;
                    }
                }
            }
            range++;
        }
        if (!client->bucket)
            LOG_WARN("Replay: Client 0x%08x is being rate limited", client->id);
        replay_from = buffer.getHeadCursor();
        LOG_INFO("Replay: Pending replay of %u packets, requested=%u, want_expired=%u", want_replay.count(), requested,
                 want_replay_expired);
        notify(REPLAY_NOTIFY_REPLAY, true);
    } break;
    default:
        LOG_WARN("Replay: Unknown request type %u", wire->header.type);
        break;
    }
}

/**
 * Handle a replay advertisement packet
 */
void ReplayModule::handleAdvertisement(const meshtastic_MeshPacket *p)
{
    LOG_INFO("Received replay advertisement from=0x%08x id=0x%08x size=%u", p->from, p->id, p->decoded.payload.size);
    if (isFromUs(p))
        return; // Ignore our own advertisements

    if (p->decoded.payload.size < sizeof(uint16_t)) {
        LOG_WARN("Replay: Advertisement payload too small");
        return; // Not enough data for even the header
    }
    uint16_t *payload = (uint16_t *)p->decoded.payload.bytes;
    ReplayWire *wire = (ReplayWire *)payload++;
    int payload_words = p->decoded.payload.size / sizeof(uint16_t) - 1;
    ReplayServerInfo _server = {};
    _server.id = p->from;
    ReplayServerInfo *server = &_server;
    for (unsigned int i = 0; i < REPLAY_TRACK_SERVERS; i++) {
        if (servers[i].id == p->from) {
            server = &servers[i];
            if (server->last_advert_millis + REPLAY_SERVER_STALE_SECS * 1000 < millis()) {
                LOG_INFO("Replay: Stale server 0x%08x has become active again after %u seconds", server->id,
                         (millis() - server->last_advert_millis) / 1000);
                invalidateServer(server);
            }
            break;
        }
    }
    server->last_advert_millis = millis();
    server->flag_priority = wire->header.priority;
    server->flag_router = wire->header.router;

    if (wire->header.boot)
        // The server has rebooted, so reset its availability state
        invalidateServer(server);

    switch (wire->header.type) {
    case REPLAY_ADVERT_TYPE_AVAILABLE:
        handleAvailabilityAdvertisement(wire, (unsigned char *)payload,
                                        ((unsigned char *)p->decoded.payload.bytes) + p->decoded.payload.size, server);
        break;
    case REPLAY_ADVERT_TYPE_EXPIRED:
        if (payload_words < 1 || payload_words < 1 /*map*/ + __builtin_popcount(payload[0]) /*ranges*/) {
            LOG_WARN("Replay: Expired advert payload too small");
            return;
        }
        handleExpiredAdvertisement(wire, (unsigned char *)payload,
                                   ((unsigned char *)p->decoded.payload.bytes) + p->decoded.payload.size, server);
        break;
    default:
        LOG_WARN("Replay: Unknown advertisement type %u", wire->header.type);
        return;
    }

    server->adverts_received++;
    if (!server->is_tracked) {
        // Start tracking this server if we have space or it is more useful than an existing tracked server
        server->discovered_millis = millis();
        ReplayServerInfo *target = servers;
        for (unsigned int i = 0; i < REPLAY_TRACK_SERVERS; i++) {
            if (!servers[i].is_tracked) {
                target = &servers[i];
                break; // Always use empty slots first
            }
            if (!servers[i].flag_router && target->flag_router)
                target = &servers[i]; // Prefer replacing non-routers
            else if (servers[i].last_advert_millis < target->last_advert_millis)
                target = &servers[i]; // Prefer replacing older entries
        }
        if (!target->is_tracked                                                          // Target is an empty slot
            || (target->last_advert_millis + REPLAY_SERVER_STALE_SECS * 1000 < millis()) // Target is stale
            || (target->replays_requested < server->replays_requested)                   // Target is less useful
        ) {
            memcpy(target, server, sizeof(ReplayServerInfo));
            server = target;
            server->is_tracked = true;
            LOG_INFO("Replay: Now tracking server=0x%08x", target->id);
        }
    }
    LOG_INFO("Replay: server=0x%08x adverts=%u requests=%u missing=%u/%u seq=%u prio=%u router=%u agg=%u boot=%u", server->id,
             server->adverts_received, server->replays_requested, server->missing.count(), server->available.count(),
             wire->header.sequence, server->flag_priority, server->flag_router, wire->header.aggregate, wire->header.boot);
}

/**
 * Handle an availability advertisement (i.e. which packets the server has available)
 */
void ReplayModule::handleAvailabilityAdvertisement(ReplayWire *wire, unsigned char *data, unsigned char *data_end,
                                                   ReplayServerInfo *server)
{
    int payload_words = (data_end - data) / sizeof(uint16_t);
    if (payload_words < 2 || payload_words < 1 /*map*/ + __builtin_popcount(((uint16_t *)data)[0]) * 2 /*ranges*/) {
        LOG_WARN("Replay: Availability advert payload too small");
        return;
    }
    uint16_t *payload = (uint16_t *)data;
    uint16_t *payload_start = payload;
    ReplayMap map = *payload++;
    for (unsigned int i = 0; i < 16; i++) {
        if (!(map & (1 << i)))
            continue;
        ReplayMap *packet_map = payload++;
        ReplayMap *priority_map = payload++;
        if (payload - payload_start > payload_words ||
            payload - payload_start > payload_words - __builtin_popcount(*packet_map)) {
            LOG_WARN("Replay: Availability advert payload too small");
            return;
        }
        for (unsigned int j = 0; j < 16; j++) {
            if (*packet_map & (1 << j)) {
                ReplayCursor idx = (i * 16 + j) & REPLAY_BUFFER_MASK;
                server->available.set(idx);
                if (*priority_map & (1 << j))
                    server->priority.set(idx);
                else
                    server->priority.reset(idx);
                server->packets[idx] = *payload++;
                if (!isKnown(server->packets[idx])) {
                    LOG_WARN("Replay: Discovered missing packet hash=0x%04x via=0x%08x", server->packets[idx], server->id);
                    server->missing.set(idx);
                } else {
                    LOG_DEBUG("Replay: Discovered known packet hash=0x%04x via=0x%08x", server->packets[idx], server->id);
                    server->missing.reset(idx);
                }
            }
        }
    }

    if (!wire->header.boot) {
        unsigned int this_sequence = (server->last_sequence & ~REPLAY_SEQUENCE_MASK) | wire->header.sequence;
        if (!server->last_sequence) {
            // First ever advert received from this server
            server->last_sequence = REPLAY_SEQUENCE_MASK + 1 + wire->header.sequence;
            this_sequence = (server->last_sequence & ~REPLAY_SEQUENCE_MASK) | wire->header.sequence;
            if (!wire->header.sequence)
                this_sequence += REPLAY_SEQUENCE_MASK + 1; // The wire sequence just wrapped
            LOG_INFO("Replay: First advertisement from server=0x%08x seq=%u, last_seq=%u", server->id, this_sequence,
                     server->last_sequence);
        }
        if (this_sequence <= server->last_sequence - 15)
            this_sequence += REPLAY_SEQUENCE_MASK + 1; // This is a forward wrap, not a reference to an old sequence
        if (!wire->header.aggregate &&
            ((this_sequence < server->last_sequence) ||
             (server->max_sequence > server->last_sequence && server->max_sequence - server->last_sequence > 15))) {
            if (this_sequence < server->last_sequence)
                // If the sequence number went backwards, then we have likely missed many intervening
                // adverts and should reset our tracking state & start with a blank slate. Do not ask
                // for missing adverts, because we have missed way too much for this to be sensible.
                LOG_WARN("Replay: Advertisement sequence went backwards from server=0x%08x seq=%u, last_seq=%u", server->id,
                         this_sequence, server->last_sequence);
            else if (server->max_sequence - server->last_sequence > 15)
                // If we have missed so many adverts that we are this far behind, we are probably never
                // going to catch up via aggregates, so reset our tracking state & start with a blank slate.
                LOG_WARN("Replay: Too many missed adverts from server=0x%08x seq=%u, last_seq=%u, max_seq=%u", server->id,
                         this_sequence, server->last_sequence, server->max_sequence);

            invalidateServer(server);
            server->last_sequence = REPLAY_SEQUENCE_MASK + 1 + wire->header.sequence;
            this_sequence = (server->last_sequence & ~REPLAY_SEQUENCE_MASK) | wire->header.sequence;
            server->max_sequence = this_sequence;
        }
        if (this_sequence > server->max_sequence)
            server->max_sequence = this_sequence;
        for (unsigned int i = 0; i < 32; i++) {
            unsigned int seq = server->last_sequence + i;
            if (seq <= server->last_sequence)
                continue; // We already received the advert with this sequence
            if (seq >= this_sequence)
                break;
            server->missing_sequence |= (1 << i);
            LOG_WARN("Replay: Noticed missing advertisement seq=%u from server=0x%08x", seq, server->id);
        }
        while (server->last_sequence < server->max_sequence && !(server->missing_sequence & 3)) {
            server->missing_sequence >>= 1;
            server->last_sequence++;
        }

        // Handle aggregate advertisements
        if (wire->header.aggregate && data_end - (unsigned char *)payload >= (int)sizeof(ReplayMap)) {
            ReplayMap aggregate_mask = *payload++;
            for (unsigned int i = 0; i < 16; i++) {
                unsigned int seq = this_sequence + i;
                if (seq <= server->last_sequence)
                    continue; // We already received the advert with this sequence
                if (aggregate_mask & (1 << i)) {
                    LOG_DEBUG("Replay: Caught up on missed advertisement server=0x%08x seq=%u", server->id, seq);
                    server->missing_sequence &= ~(1 << (seq - server->last_sequence));
                    if (seq > server->max_sequence)
                        server->max_sequence = seq;
                }
            }
            while (server->last_sequence < server->max_sequence && !(server->missing_sequence & 3)) {
                server->missing_sequence >>= 1;
                server->last_sequence++;
            }
        }

        // Catch up on missing advertisements
        if (server->missing_sequence) {
            requestMissingAdvertisements(server);
        } else {
            LOG_DEBUG("Replay: Sequence assert last=%u this=%u max=%u, wire=%u", server->last_sequence, this_sequence,
                      server->max_sequence, wire->header.sequence);
            assert(server->last_sequence == server->max_sequence);
        }
    }

    if (wire->header.throttle && (unsigned char *)payload < data_end) {
        uint8_t *throttled = (uint8_t *)payload;
        uint8_t me = nodeDB->getNodeNum() & 0x000F;
        while (throttled <= data_end) {
            if (*throttled++ == me)
                return; // We are being throttled by the server, so don't ask for anything
        }
    }

    if (server->missing.any()) {
        for (unsigned int i = 0; i < REPLAY_BUFFER_SIZE; i++) {
            if (server->missing.test(i)) {
                if (!server->available.test(i)) {
                    // This packet is missing but the server does not claim to have it, so stop tracking it
                    server->missing.reset(i);
                } else if (isKnown(server->packets[i])) {
                    // This packet was previously missing, but we have since received it
                    server->missing.reset(i);
                }
            }
        }
        if (server->missing.any())
            requestReplay(server);
    }
}

/**
 * Handle an expiry advertisement (i.e. which packets the server has pruned from its cache)
 */
void ReplayModule::handleExpiredAdvertisement(ReplayWire *wire, unsigned char *data, unsigned char *data_end,
                                              ReplayServerInfo *server)
{
    unsigned int expired = 0;
    uint16_t *payload = (uint16_t *)data;
    ReplayMap map = *payload++;
    for (unsigned int i = 0; i < 16; i++) {
        if (!(map & (1 << i)))
            continue;
        for (unsigned int j = 0; j < 16; j++) {
            if (*payload & (1 << j)) {
                ReplayCursor idx = (i * 16 + j) & REPLAY_BUFFER_MASK;
                server->available.reset(idx);
                expired++;
            }
        }
        payload++;
    }
    LOG_INFO("Replay: Received expiry advertisement from=0x%08x expired_count=%u", server->id, expired);
}

/**
 * Get or allocate a request slot for a specific packet hash
 */
ReplayRequestInfo *ReplayModule::requestInfo(ReplayHash hash)
{
    ReplayRequestInfo *target = NULL;
    for (unsigned int i = 0; i < REPLAY_REQUEST_MAX_OUTSTANDING; i++) {
        if (requests[i].hash == hash && requests[i].timeout_millis >= millis()) {
            return &requests[i];
        }
        if (!target && requests[i].timeout_millis < millis())
            target = &requests[i];
    }
    if (target) {
        target->hash = hash;
        target->timeout_millis = 0;
    }
    return target;
}

/**
 * Add a packet to the replay TX queue for sending
 */
bool ReplayModule::queuePush(ReplayCursor idx)
{
    for (unsigned int i = queue_tail; queue_length && i < queue_next; i++) {
        if (queue[i & REPLAY_QUEUE_MASK] == idx) {
            LOG_INFO("Replay: Packet already in TX queue hash=0x%04x", buffer.get(idx & REPLAY_BUFFER_MASK)->hash);
            return true; // Already queued
        }
    }
    if (queueLength() >= REPLAY_QUEUE_SIZE) {
        LOG_WARN("Replay: Queue full, cannot queue packet hash=0x%04x", buffer.get(idx & REPLAY_BUFFER_MASK)->hash);
        return false; // Queue is full
    }
    idx = (buffer.getTailCursor() & ~REPLAY_BUFFER_MASK) | (idx & REPLAY_BUFFER_MASK);
    queue[queue_next++ & REPLAY_QUEUE_MASK] = idx;
    queue_length++;
    LOG_DEBUG("Replay: Queued packet for TX hash=0x%04x queue=%u", buffer.get(idx & REPLAY_BUFFER_MASK)->hash, queueLength());
    return true;
}

/**
 * Peek at the next packet in the replay TX queue without removing it
 */
meshtastic_MeshPacket *ReplayModule::queuePeek()
{
    if (!queueLength())
        return NULL; // Queue is empty
    ReplayCursor idx = queue[queue_tail & REPLAY_QUEUE_MASK];
    if (idx < buffer.getTailCursor()) {
        LOG_ERROR("Replay: Peeked at a packet older than the buffer tail");
        queue_tail++; // This entry has fallen out of the buffer
        queue_length--;
        return queuePeek();
    }
    ReplayEntry *entry = buffer.get(idx & REPLAY_BUFFER_MASK);
    if (!entry->p) {
        LOG_ERROR("Replay: Peeked at a pruned packet hash=0x%04x", entry->hash);
        queue_tail++; // This entry has been pruned
        queue_length--;
        return queuePeek();
    }
    return entry->p;
}

/**
 * Pop the next packet from the replay TX queue
 */
meshtastic_MeshPacket *ReplayModule::queuePop()
{
    meshtastic_MeshPacket *p = queuePeek();
    if (p) {
        queue_tail++;
        queue_length--;
    }
    return p;
}

/**
 * Invalidate a server record's state and prepare it for reuse
 */
void ReplayModule::invalidateServer(ReplayServerInfo *server, bool stats)
{
    server->last_sequence = 0;
    server->max_sequence = 0;
    server->missing_sequence = 0;
    server->available.reset();
    server->priority.reset();
    server->missing.reset();

    if (stats) {
        server->adverts_received = 0;
        server->replays_requested = 0;
        server->last_advert_millis = 0;
    }
}

/**
 * Handle thread notifications
 */
void ReplayModule::onNotify(uint32_t notification)
{
    LOG_DEBUG("Replay: onNotify %u", notification);

    if (replay_from)
        replay();

    unsigned int now = millis();
    uint32_t deadline = last_advert_millis + REPLAY_FLUSH_SECS * 1000;
    if (now < REPLAY_STARTUP_DELAY_SECS * 1000)
        deadline = REPLAY_STARTUP_DELAY_SECS * 1000; // Ensure we don't advertise too quickly on boot

    if (packets_since_advert > REPLAY_FLUSH_PACKETS || deadline <= now)
        advertise();

    if (replay_from >= buffer.getTailCursor() && replay_from) {
        // We still have packets pending replay
        notifyLater(REPLAY_SPACING_MS, REPLAY_NOTIFY_REPLAY, true);
    } else if (deadline > now) {
        // Sleep until the next advert deadline
        LOG_DEBUG("Sleep to deadline %ld", deadline - now);
        notifyLater(deadline - now, REPLAY_NOTIFY_INTERVAL, false);
    }
}