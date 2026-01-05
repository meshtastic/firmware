#include "AckBatcher.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

AckBatcher *ackBatcher = nullptr;

AckBatcher::AckBatcher() {}

void AckBatcher::queueAck(NodeNum to, PacketId id, ChannelIndex channel,
                          uint8_t hopLimit, meshtastic_Routing_Error error) {
  uint32_t now = millis();

  // If this is the first pending ACK, record the time
  if (totalPending == 0) {
    oldestPendingTime = now;
  }

  // Add to the queue for this destination
  PendingAck pending = {id, channel, hopLimit, error, now};
  pendingByDest[to].push_back(pending);
  totalPending++;

  LOG_DEBUG("AckBatcher: Queued %s for 0x%x (id=0x%x), total pending=%d",
            error == meshtastic_Routing_Error_NONE ? "ACK" : "NAK", to, id,
            totalPending);

  // Flush immediately if we've hit size limits
  if (pendingByDest[to].size() >= MAX_BATCH_SIZE) {
    LOG_DEBUG("AckBatcher: Batch full for 0x%x, flushing", to);
    flushForDest(to);
  } else if (totalPending >= MAX_PENDING_TOTAL) {
    LOG_DEBUG("AckBatcher: Total pending limit reached, flushing all");
    flushAll();
  }
}

void AckBatcher::checkAndFlush(uint32_t currentTimeMs) {
  if (totalPending == 0) {
    return;
  }

  // Check if the oldest pending ACK has waited long enough
  if (currentTimeMs - oldestPendingTime >= DEFAULT_BATCH_WINDOW_MS) {
    LOG_DEBUG("AckBatcher: Batch window expired (%dms), flushing all",
              currentTimeMs - oldestPendingTime);
    flushAll();
  }
}

void AckBatcher::flushAll() {
  if (totalPending == 0) {
    return;
  }

  // Collect all destination keys first (since we'll modify the map)
  std::vector<NodeNum> destinations;
  destinations.reserve(pendingByDest.size());
  for (auto it = pendingByDest.begin(); it != pendingByDest.end(); ++it) {
    destinations.push_back(it->first);
  }

  // Flush each destination
  for (size_t i = 0; i < destinations.size(); i++) {
    flushForDest(destinations[i]);
  }
}

void AckBatcher::flushForDest(NodeNum dest) {
  auto it = pendingByDest.find(dest);
  if (it == pendingByDest.end() || it->second.empty()) {
    return;
  }

  sendBatchedAck(dest, it->second);

  totalPending -= it->second.size();
  pendingByDest.erase(it);

  // Update oldest pending time if there are still pending ACKs
  updateOldestPendingTime();
}

void AckBatcher::updateOldestPendingTime() {
  if (totalPending == 0) {
    oldestPendingTime = 0;
    return;
  }

  oldestPendingTime = UINT32_MAX;
  for (auto mapIt = pendingByDest.begin(); mapIt != pendingByDest.end();
       ++mapIt) {
    for (size_t i = 0; i < mapIt->second.size(); i++) {
      if (mapIt->second[i].queuedAt < oldestPendingTime) {
        oldestPendingTime = mapIt->second[i].queuedAt;
      }
    }
  }
}

/**
 * Batched ACK Packet Format
 *
 * The payload is encoded as:
 *   [MAGIC:1][VERSION:1][COUNT:1][ACK_ENTRIES...]
 *
 * Each ACK_ENTRY is:
 *   [PACKET_ID:4][ERROR:1]
 *
 * Total size per ACK: 5 bytes
 * Header: 3 bytes
 * Max 8 ACKs = 3 + (8 * 5) = 43 bytes
 *
 * This is much more efficient than 8 separate ACK packets!
 */
void AckBatcher::sendBatchedAck(NodeNum dest,
                                const std::vector<PendingAck> &acks) {
  if (acks.empty()) {
    return;
  }

  LOG_INFO("AckBatcher: Sending batched ACK to 0x%x with %d acknowledgments",
           dest, acks.size());

  meshtastic_MeshPacket *p = router->allocForSending();
  p->to = dest;
  p->channel = acks[0].channel; // Use channel from first ACK
  p->hop_limit = acks[0].hopLimit;
  p->want_ack = false;
  p->priority = meshtastic_MeshPacket_Priority_ACK;
  p->decoded.portnum = meshtastic_PortNum_ROUTING_APP;

  // Build the batched payload
  uint8_t *payload = p->decoded.payload.bytes;
  size_t offset = 0;

  // Header: Magic, Version, Count
  payload[offset++] = BATCHED_ACK_MAGIC;
  payload[offset++] = BATCHED_ACK_VERSION;
  payload[offset++] = (uint8_t)acks.size();

  // Encode each ACK: PacketId (4 bytes little-endian) + Error (1 byte)
  for (size_t i = 0; i < acks.size(); i++) {
    const PendingAck &pending = acks[i];

    // PacketId as little-endian 32-bit
    payload[offset++] = (pending.id >> 0) & 0xFF;
    payload[offset++] = (pending.id >> 8) & 0xFF;
    payload[offset++] = (pending.id >> 16) & 0xFF;
    payload[offset++] = (pending.id >> 24) & 0xFF;

    // Error code
    payload[offset++] = (uint8_t)pending.error;
  }

  p->decoded.payload.size = offset;

  // Set request_id to first ACK's ID for compatibility with existing logic
  p->decoded.request_id = acks[0].id;

  router->sendLocal(p);
}

bool AckBatcher::isBatchedAckPacket(const uint8_t *payload,
                                    size_t payloadSize) {
  // Must have at least header (3 bytes) + one entry (5 bytes)
  if (payloadSize < 8) {
    return false;
  }

  return payload[0] == BATCHED_ACK_MAGIC && payload[1] == BATCHED_ACK_VERSION;
}

bool AckBatcher::parseBatchedAck(const meshtastic_MeshPacket *p,
                                 std::vector<BatchedAckEntry> &entries) {
  entries.clear();

  if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
    return false;
  }

  const uint8_t *payload = p->decoded.payload.bytes;
  size_t payloadSize = p->decoded.payload.size;

  if (!isBatchedAckPacket(payload, payloadSize)) {
    return false;
  }

  // Parse header
  uint8_t count = payload[2];

  // Validate size: header (3) + entries (count * 5)
  size_t expectedSize = 3 + (count * 5);
  if (payloadSize < expectedSize) {
    LOG_WARN("AckBatcher: Malformed batched ACK, size=%d expected=%d",
             payloadSize, expectedSize);
    return false;
  }

  LOG_INFO("AckBatcher: Parsing batched ACK with %d entries from 0x%x", count,
           p->from);

  // Parse each ACK entry
  entries.reserve(count);
  size_t offset = 3;
  for (uint8_t i = 0; i < count; i++) {
    BatchedAckEntry entry;

    // Decode PacketId (little-endian)
    entry.id = (PacketId)payload[offset] |
               ((PacketId)payload[offset + 1] << 8) |
               ((PacketId)payload[offset + 2] << 16) |
               ((PacketId)payload[offset + 3] << 24);
    offset += 4;

    entry.error = (meshtastic_Routing_Error)payload[offset++];

    LOG_DEBUG("AckBatcher: Parsed %s for 0x%x",
              entry.error == meshtastic_Routing_Error_NONE ? "ACK" : "NAK",
              entry.id);

    entries.push_back(entry);
  }

  return true;
}
