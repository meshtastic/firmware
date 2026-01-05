#pragma once

#include "Channels.h"
#include "MeshTypes.h"
#include <map>
#include <vector>

/**
 * Manages batching of ACK/NAK responses to reduce airtime.
 *
 * Instead of sending individual ACKs immediately, this class queues them
 * and flushes as a single combined packet after a configurable window.
 *
 * Multiple ACKs are encoded into a single packet payload,
 * significantly reducing airtime overhead in busy meshes.
 *
 * Benefits:
 * - Reduces airtime in busy meshes (e.g., 5 packets = 1 ACK instead of 5)
 * - Improves channel availability for actual message traffic
 * - Lower power consumption from fewer radio transmissions
 */
class AckBatcher {
public:
  /// Configuration constants
  static constexpr uint32_t DEFAULT_BATCH_WINDOW_MS =
      200; // Max wait time before flush
  static constexpr size_t MAX_BATCH_SIZE =
      8; // Max ACKs per destination before flush
  static constexpr size_t MAX_PENDING_TOTAL =
      32; // Max total pending across all destinations

  /// Magic byte to identify batched ACK packets (placed at start of payload)
  static constexpr uint8_t BATCHED_ACK_MAGIC = 0xBA;

  /// Version byte for future format changes
  static constexpr uint8_t BATCHED_ACK_VERSION = 0x01;

  /// Entry returned when parsing a batched ACK packet
  struct BatchedAckEntry {
    PacketId id;
    meshtastic_Routing_Error error;
  };

  struct PendingAck {
    PacketId id;
    ChannelIndex channel;
    uint8_t hopLimit;
    meshtastic_Routing_Error error;
    uint32_t queuedAt; // Timestamp when queued
  };

  AckBatcher();

  /**
   * Queue an ACK or NAK for batched sending.
   * @param to Destination node number
   * @param id Packet ID being acknowledged
   * @param channel Channel index for the response
   * @param hopLimit Hop limit for the response packet
   * @param error Error code (NONE for ACK, other values for NAK)
   */
  void queueAck(NodeNum to, PacketId id, ChannelIndex channel, uint8_t hopLimit,
                meshtastic_Routing_Error error);

  /**
   * Check if any batches are ready to flush based on time.
   * Should be called periodically from runOnce().
   * @param currentTimeMs Current time in milliseconds (from millis())
   */
  void checkAndFlush(uint32_t currentTimeMs);

  /**
   * Force flush all pending ACKs immediately.
   * Useful for shutdown or when immediate delivery is needed.
   */
  void flushAll();

  /**
   * Check if batching is currently enabled.
   * @return true if batching is active
   */
  bool isEnabled() const { return enabled; }

  /**
   * Enable or disable ACK batching.
   * When disabled, ACKs are sent immediately as individual packets.
   * @param enable true to enable batching
   */
  void setEnabled(bool enable) { enabled = enable; }

  /**
   * Get the number of currently pending ACKs.
   * @return Total count of queued ACKs across all destinations
   */
  size_t getPendingCount() const { return totalPending; }

  /**
   * Check if a received packet is a batched ACK packet.
   * @param payload Pointer to payload bytes
   * @param payloadSize Size of payload
   * @return true if this is a batched ACK packet
   */
  static bool isBatchedAckPacket(const uint8_t *payload, size_t payloadSize);

  /**
   * Parse a received batched ACK packet into individual entries.
   * The caller is responsible for stopping retransmissions for each entry.
   * @param p The received mesh packet
   * @param entries Output vector of parsed ACK entries
   * @return true if successfully parsed
   */
  static bool parseBatchedAck(const meshtastic_MeshPacket *p,
                              std::vector<BatchedAckEntry> &entries);

private:
  // Map of destination node -> list of pending ACKs for that node
  std::map<NodeNum, std::vector<PendingAck>> pendingByDest;

  // Track the oldest pending ACK time for timeout calculation
  uint32_t oldestPendingTime = 0;

  // Total count of pending ACKs across all destinations
  size_t totalPending = 0;

  // Feature toggle - DISABLED by default for backwards compatibility.
  // Old nodes won't understand the batched ACK format.
  // Enable via setEnabled(true) when all mesh nodes support this feature.
  bool enabled = false;

  /**
   * Send a batched ACK packet to a single destination.
   * Encodes all pending ACKs for that destination into one packet.
   * @param dest Destination node number
   * @param acks Vector of pending ACKs to send
   */
  void sendBatchedAck(NodeNum dest, const std::vector<PendingAck> &acks);

  /**
   * Flush all pending ACKs for a specific destination.
   * @param dest Destination node number
   */
  void flushForDest(NodeNum dest);

  /**
   * Update the oldestPendingTime after removing items from the queue.
   */
  void updateOldestPendingTime();
};

extern AckBatcher *ackBatcher;
