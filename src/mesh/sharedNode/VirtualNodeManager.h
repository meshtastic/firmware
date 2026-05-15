#ifdef MODE_SHARED_NODE
#pragma once

/**
 * @file VirtualNodeManager.h
 * @brief Shared-node virtual session and packet routing manager.
 */

#include <Arduino.h>
#include <array>

#include "configuration.h"
#include "MeshTypes.h"
#include "concurrency/Lock.h"
#include "mesh/sharedNode/Types.h"
#include "mesh/sharedNode/static/RingQueue.h"
#include "mesh/sharedNode/static/SlotTable.h"

class PhoneAPI;

/**
 * @brief Manages shared-node PhoneAPI sessions and guest virtual node IDs.
 *
 * VirtualNodeManager maps connected PhoneAPI clients to either the real local
 * node identity for the admin session or to per-guest virtual node IDs. It also
 * enforces guest restrictions on outgoing admin packets and queues packets that
 * can be delivered locally between clients on the same device.
 */
class VirtualNodeManager
{
  public:
    /**
     * @brief Maximum number of locally queued packets per PhoneAPI session.
     */
    static constexpr size_t MAX_PENDING_PACKETS_PER_API = 32;

    /**
     * @brief Runtime state for one connected admin or guest session.
     */
    struct SessionInfo {
        /**
         * @brief Connected client's PhoneAPI instance.
         */
        PhoneAPI *api = nullptr;

        /**
         * @brief Virtual node ID assigned to this session, or 0 if unassigned.
         */
        NodeNum virtualNodeId = 0;

        /**
         * @brief Shared-node slot owned by this session.
         */
        uint8_t sharedNodeSlot = SharedNode::INVALID_SLOT;

        /**
         * @brief Indicates whether this slot currently contains an active session.
         */
        bool used = false;

        /**
         * @brief Queue of packets waiting for local delivery to this session.
         */
        StaticRingQueue<meshtastic_MeshPacket, MAX_PENDING_PACKETS_PER_API, DropOldest> localPackets{};
    };

    /**
     * @brief Decision returned after processing a client-originated packet.
     */
    enum OutgoingPacketDecision : uint8_t {
      /**
       * @brief Allow the packet to continue to the radio path.
       */
      OUTGOING_ALLOW_RADIO = 0,

      /**
       * @brief Reject the packet before it reaches the radio path.
       */
      OUTGOING_REJECT = 1,

      /**
       * @brief Packet was queued for another local virtual node.
       */
      OUTGOING_HANDLED_LOCAL = 2,
    };

    /**
     * @brief Creates a manager backed by the internal static session table.
     */
    VirtualNodeManager();

    /**
     * @brief Connects a PhoneAPI session as the shared-node admin.
     *
     * Only one active admin session is allowed at a time.
     *
     * @param api PhoneAPI instance for the connected client.
     * @return true when the session was connected as admin.
     */
    bool connectAsAdmin(PhoneAPI *api);

    /**
     * @brief Connects a PhoneAPI session as a shared-node guest.
     *
     * A guest receives a stable virtual node ID for its assigned shared-node
     * slot. If the slot has no ID yet, one is allocated and persisted through
     * SharedNodePairingPolicy.
     *
     * @param api PhoneAPI instance for the connected client.
     * @return true when the session was connected as guest.
     */
    bool connectAsGuest(PhoneAPI *api);

    /**
     * @brief Disconnects a PhoneAPI session and clears its runtime state.
     *
     * @param api PhoneAPI instance to disconnect.
     */
    void disconnect(PhoneAPI *api);

    /**
     * @brief Returns the virtual node ID assigned to a session.
     *
     * @param api PhoneAPI instance to look up.
     * @return Assigned virtual node ID, or 0 when no session exists.
     */
    NodeNum getVirtualNodeId(const PhoneAPI *api) const;

    /**
     * @brief Checks whether a node number belongs to an active local guest session.
     *
     * @param nodeNum Node number to test.
     * @return true when the node number is assigned to a live local session.
     */
    bool isLocalVirtualNode(NodeNum nodeNum) const;

    /**
     * @brief Checks whether a PhoneAPI session is the active admin session.
     *
     * @param api PhoneAPI instance to test.
     * @return true when the session exists and its slot implies admin privileges.
     */
    bool isAdmin(const PhoneAPI *api) const;

    /**
     * @brief Checks whether any admin session is currently active.
     *
     * @return true when an admin session is connected.
     */
    bool hasActiveAdminSession() const;

    /**
     * @brief Applies shared-node rules to a packet emitted by a client.
     *
     * Guest packets are rewritten to use the guest virtual node ID as source.
     * Guest admin packets targeting local nodes are rejected. Packets addressed
     * to another local virtual node are queued locally instead of sent by radio.
     *
     * @param packet Packet to inspect and possibly rewrite.
     * @param sourceApi Source PhoneAPI instance, or nullptr for non-client packets.
     * @return Decision describing how the caller should handle the packet.
     */
    OutgoingPacketDecision handleOutgoingPacket(meshtastic_MeshPacket &packet, PhoneAPI *sourceApi);

    /**
     * @brief Queues an incoming mesh packet for matching local guest sessions.
     *
     * @param packet Incoming packet to offer to local guest queues.
     */
    void handleIncomingPacket(meshtastic_MeshPacket &packet);

    /**
     * @brief Checks whether a PhoneAPI session has queued local packets.
     *
     * @param api PhoneAPI instance to inspect.
     * @return true when at least one local packet is queued.
     */
    bool hasLocalPacketForApi(const PhoneAPI *api) const;

    /**
     * @brief Pops the oldest locally queued packet for a PhoneAPI session.
     *
     * @param api PhoneAPI instance to inspect.
     * @param packetOut Destination updated with the popped packet.
     * @return true when a packet was popped.
     */
    bool popLocalPacketForApi(const PhoneAPI *api, meshtastic_MeshPacket &packetOut);

  private:
    /**
     * @brief Guards access to session state.
     */
    mutable concurrency::Lock sessionLock;

    /**
     * @brief Next candidate virtual node ID for guest allocation.
     */
    NodeNum nextVirtualNodeId = 0x0A;

    /**
     * @brief Fixed-size table of active admin and guest sessions.
     */
    std::array<SessionInfo, SharedNode::MAX_CONNECTIONS> sessions{};

    /**
     * @brief Predicate helper used to search the session table.
     */
    StaticSlotTable<SessionInfo, SharedNode::MAX_CONNECTIONS> sessionSlots;

    /**
     * @brief Allocates a free session slot.
     *
     * @pre sessionLock is held by the caller.
     * @return Mutable session slot, or nullptr when no slot is free.
     */
    SessionInfo *allocateSessionLocked();

    /**
     * @brief Finds a mutable session by PhoneAPI pointer.
     *
     * @pre sessionLock is held by the caller.
     * @param api PhoneAPI instance to find.
     * @return Matching session, or nullptr when not connected.
     */
    SessionInfo *findSessionByApiLocked(PhoneAPI *api);

    /**
     * @brief Finds a const session by PhoneAPI pointer.
     *
     * @pre sessionLock is held by the caller.
     * @param api PhoneAPI instance to find.
     * @return Matching session, or nullptr when not connected.
     */
    const SessionInfo *findSessionByApiLocked(const PhoneAPI *api) const;

    /**
     * @brief Finds a mutable session by virtual node ID.
     *
     * @pre sessionLock is held by the caller.
     * @param virtualNodeId Virtual node ID to find.
     * @return Matching session, or nullptr when not connected.
     */
    SessionInfo *findSessionByVirtualNodeLocked(NodeNum virtualNodeId);

    /**
     * @brief Finds a const session by virtual node ID.
     *
     * @pre sessionLock is held by the caller.
     * @param virtualNodeId Virtual node ID to find.
     * @return Matching session, or nullptr when not connected.
     */
    const SessionInfo *findSessionByVirtualNodeLocked(NodeNum virtualNodeId) const;

    /**
     * @brief Checks whether an admin session exists.
     *
     * @pre sessionLock is held by the caller.
     * @param exceptApi Optional PhoneAPI instance to ignore during the check.
     * @return true when another admin session is active.
     */
    bool hasAdminLocked(const PhoneAPI *exceptApi = nullptr) const;

    /**
     * @brief Queues a packet for local delivery to a PhoneAPI session.
     *
     * @pre sessionLock is held by the caller.
     * @param api Destination PhoneAPI instance.
     * @param packet Packet to queue.
     */
    void enqueueLocalPacketLocked(const PhoneAPI *api, const meshtastic_MeshPacket &packet);

    /**
     * @brief Allocates the next unused guest virtual node ID.
     *
     * @pre sessionLock is held by the caller.
     * @return Virtual node ID candidate.
     */
    NodeNum allocateVirtualNodeIdLocked();
};

/**
 * @brief Global shared-node virtual manager.
 */
extern VirtualNodeManager virtualNodeManager;
#endif
