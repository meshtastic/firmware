#pragma once

#include "MeshTypes.h"

#include <atomic>
#include <queue>

/**
 * A priority queue of packets
 */
class MeshPacketQueue
{
    size_t maxLen;
    std::vector<meshtastic_MeshPacket *> queue;
    std::atomic<uint32_t> queuedMs{0};

    /// Walk the queue and total its time-on-air. Only ever called from a mutator or from
    /// refreshAirtime(), never from a reader.
    uint32_t sumAirtimeMsec() const;

    /** Replace a lower priority package in the queue with 'mp' (provided there are lower pri packages). Return true if replaced.
     */
    bool replaceLowerPriorityPacket(meshtastic_MeshPacket *mp);

  public:
    explicit MeshPacketQueue(size_t _maxLen);

    /** enqueue a packet, return false if full
     * @param dropped Optional pointer to a bool that will be set to true if a packet was dropped
     */
    bool enqueue(meshtastic_MeshPacket *p, bool *dropped = nullptr);

    /** return true if the queue is empty */
    bool empty();

    /** return amount of free packets in Queue */
    size_t getFree() { return maxLen - queue.size(); }

    /** return total size of the Queue */
    size_t getMaxLen() { return maxLen; }

    meshtastic_MeshPacket *dequeue();

    meshtastic_MeshPacket *getFront();

    /** Get a packet from this queue. Returns a pointer to the packet, or NULL if not found. */
    meshtastic_MeshPacket *getPacketFromQueue(NodeNum from, PacketId id);

    /** Attempt to find and remove a packet from this queue.  Returns the packet which was removed from the queue */
    meshtastic_MeshPacket *remove(NodeNum from, PacketId id, bool tx_normal = true, bool tx_late = true,
                                  uint8_t hop_limit_lt = 0);

    /* Attempt to find a packet from this queue. Return true if it was found. */
    bool find(const NodeNum from, const PacketId id);

    /** Time-on-air of everything waiting, in ms.
     *
     * Summed when the queue last changed rather than walked by the caller: on nRF52 a phone's send
     * runs on the Bluefruit task, which preempts the loop task two priorities down, so a reader
     * there can be torn by an enqueue. Mutators own the sum instead, and the reader is a load.
     */
    uint32_t queuedAirtimeMsec() const { return queuedMs.load(std::memory_order_relaxed); }

    /** Re-derive the sum. dequeue() leaves it high rather than spend a walk between the channel
     * scan and the transmission; the caller settles it once the packet is away. */
    void refreshAirtime() { queuedMs.store(sumAirtimeMsec(), std::memory_order_relaxed); }
};