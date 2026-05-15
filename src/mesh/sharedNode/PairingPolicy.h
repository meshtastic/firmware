#ifdef MODE_SHARED_NODE
#pragma once

/**
 * @file SharedNodePairingPolicy.h
 * @brief Shared-node pairing role and slot assignment policy.
 */

#include "Types.h"
#include "static/SlotTable.h"
#include "concurrency/Lock.h"

#include <Arduino.h>
#include <array>

/**
 * @brief Assigns shared-node roles and slots to authenticated clients.
 *
 * SharedNodePairingPolicy keeps a small persisted table of known clients,
 * reserves slot 0 for the admin client, assigns guest slots to additional
 * clients, and bridges transient Bluetooth connection handles to stable
 * bonding identities supplied by the BLE backend.
 */
namespace SharedNode {

/**
 * @brief Pairing decision returned to the Bluetooth backend.
 */
struct Pairing {
    /**
     * @brief Passkey the backend should use for the pairing attempt.
     */
    uint32_t passkey = 0;

    /**
     * @brief Shared-node client slot selected for this pairing.
     */
    uint8_t slot = INVALID_SLOT;
};

class PairingPolicy
{
  public:
    using Role = SharedNode::Role;

    /**
     * @brief Creates a pairing policy backed by the internal static record table.
     */
    PairingPolicy();

    /**
     * @brief Chooses the slot and passkey for a new pairing attempt.
     *
     * The selected slot is kept as pending state until the backend resolves
     * the connection or consumes the pending pairing.
     *
     * @return Pairing decision for the Bluetooth backend.
     */
    Pairing beginPairing();

    /**
     * @brief Returns and clears the pending pairing slot.
     *
     * @return Pending slot, or INVALID_SLOT when none is pending.
     */
    uint8_t consumePendingPairingSlot();

    /**
     * @brief Returns the pending pairing slot without clearing it.
     *
     * @return Pending slot, or INVALID_SLOT when none is pending.
     */
    uint8_t peekPendingPairingSlot() const;

    /**
     * @brief Returns the role implied by a shared-node slot.
     *
     * @param slotIndex Shared-node client slot.
     * @return Role implied by the slot, or Role::UNKNOWN for invalid values.
     */
    static Role roleForSlot(uint8_t slotIndex) { return SharedNode::roleForSlot(slotIndex); }

    /**
     * @brief Returns and clears the pending pairing role implied by its slot.
     *
     * @return Pending role, or Role::UNKNOWN when none is pending.
     */
    Role consumePendingPairingRole() { return roleForSlot(consumePendingPairingSlot()); }

    /**
     * @brief Returns the pending pairing role implied by its slot.
     *
     * @return Pending role, or Role::UNKNOWN when none is pending.
     */
    Role peekPendingPairingRole() const { return roleForSlot(peekPendingPairingSlot()); }

    /**
     * @brief Looks up the role currently associated with a connection handle.
     *
     * @param connHandle Bluetooth connection handle.
     * @return Assigned role, or Role::UNKNOWN when unknown.
     */
    Role roleForConnection(uint16_t connHandle) const;

    /**
     * @brief Looks up the slot-implied role for a peer bond identity.
     *
     * @param identity Stable peer bond identity.
     * @return Assigned role, or Role::UNKNOWN when unknown.
     */
    Role roleForIdentity(const PeerIdentity &identity) const;

    /**
     * @brief Resolves a connection slot and returns its implied role.
     *
     * Existing bond identity records win first, then pending pairing state,
     * then a new admin or guest slot is selected if available.
     *
     * @param connHandle Bluetooth connection handle.
     * @param identity Stable peer bond identity reported by the backend.
     * @return Assigned role, or Role::UNKNOWN when no role is available.
     */
    Role resolveRoleForConnection(uint16_t connHandle, const PeerIdentity &identity);

    /**
     * @brief Resolves and records the slot for an authenticated connection.
     *
     * Existing bond identity records win first, then pending pairing state,
     * then a new admin or guest slot is selected if available.
     *
     * @param connHandle Bluetooth connection handle.
     * @param identity Stable peer bond identity reported by the backend.
     * @return Assigned slot, or INVALID_SLOT when no slot is available.
     */
    uint8_t resolveSlotForConnection(uint16_t connHandle, const PeerIdentity &identity);

    /**
     * @brief Checks whether an admin client identity is already known.
     *
     * @return true when the admin slot contains a known admin identity.
     */
    bool hasKnownAdmin() const;

    /**
     * @brief Looks up the shared-node slot associated with a connection handle.
     *
     * @param connHandle Bluetooth connection handle.
     * @return Slot index, or SharedNode::INVALID_SLOT when unknown.
     */
    uint8_t slotForConnection(uint16_t connHandle) const;

    /**
     * @brief Looks up the shared-node slot associated with a peer bond identity.
     *
     * @param identity Stable peer bond identity.
     * @return Slot index, or SharedNode::INVALID_SLOT when unknown.
     */
    uint8_t slotForIdentity(const PeerIdentity &identity) const;

    /**
     * @brief Returns the virtual node ID persisted for a slot.
     *
     * @param slotIndex Shared-node client slot.
     * @return Virtual node ID, or 0 when the slot is invalid or unassigned.
     */
    uint32_t virtualNodeIdForSlot(uint8_t slotIndex) const;

    /**
     * @brief Persists the virtual node ID assigned to a shared-node slot.
     *
     * Guest display names are regenerated when a guest virtual node ID changes.
     *
     * @param slotIndex Shared-node client slot.
     * @param virtualNodeId Virtual node ID to store.
     * @return true when the slot contains a known identity and the ID was stored.
     */
    bool setVirtualNodeIdForSlot(uint8_t slotIndex, uint32_t virtualNodeId);

    /**
     * @brief Records a slot for a live connection after slot resolution.
     *
     * @param connHandle Bluetooth connection handle.
     * @param identity Stable peer bond identity reported by the backend.
     * @param slotIndex Shared-node slot assigned to the connection.
     */
    void rememberConnectionSlot(uint16_t connHandle, const PeerIdentity &identity, uint8_t slotIndex);

    /**
     * @brief Marks a connection handle as disconnected.
     *
     * @param connHandle Bluetooth connection handle to clear.
     */
    void clearConnection(uint16_t connHandle);

    /**
     * @brief Marks a slot after an explicit ToRadio.disconnect from a phone.
     *
     * The slot becomes DISCONNECTED rather than EMPTY. Its bond identity is
     * retained for reconnects, but new pairing may reuse it after EMPTY slots.
     *
     * @param slotIndex Shared-node client slot to mark disconnected.
     */
    void disconnectSlot(uint8_t slotIndex);

    /**
     * @brief Removes the persisted identity stored in one slot.
     *
     * @param slotIndex Shared-node client slot to invalidate.
     */
    void invalidateSlot(uint8_t slotIndex);

    /**
     * @brief Clears all known shared-node client identities.
     */
    void clearAll();

    /**
     * @brief Clears all known shared-node client identities.
     */
    void clearAllKnownClients() { clearAll(); }

  private:
    /**
     * @brief Loads persisted client records from NodeDB if not loaded yet.
     *
     * @pre policyLock is held by the caller.
     */
    void loadFromNodeDBLocked();

    /**
     * @brief Saves current client records to NodeDB.
     *
     * @pre policyLock is held by the caller.
     */
    void persistToNodeDBLocked();

    /**
     * @brief Selects an admin or guest slot for a new pairing attempt.
     *
     * @pre policyLock is held by the caller.
     * @return Pairing decision with role UNKNOWN when no slot is available.
     */
    Pairing choosePairingLocked();

    /**
     * @brief Finds the slot currently bound to a connection handle.
     *
     * @pre policyLock is held by the caller.
     * @param connHandle Bluetooth connection handle.
     * @return Slot index, or -1 when no live record matches.
     */
    int8_t findSlotByConnectionLocked(uint16_t connHandle) const;

    /**
     * @brief Finds the slot persisted for a peer bond identity.
     *
     * @pre policyLock is held by the caller.
     * @param identity Stable peer bond identity.
     * @return Slot index, or -1 when no record matches.
     */
    int8_t findSlotByIdentityLocked(const PeerIdentity &identity) const;

    /**
     * @brief Finds an allocatable guest slot.
     *
     * Allocation considers only EMPTY first, then DISCONNECTED. NOT_ACTIVE and
     * ACTIVE slots are owned by known peers and must not be reused.
     *
     * @pre policyLock is held by the caller.
     * @return Guest slot index, or -1 when no guest slot is available.
     */
    int8_t findAvailableGuestSlotLocked() const;

    /**
     * @brief Stores a live connection in a slot and persists identity changes.
     *
     * @pre policyLock is held by the caller.
     * @param slotIndex Shared-node client slot.
     * @param connHandle Bluetooth connection handle.
     * @param identity Stable peer bond identity reported by the backend.
     */
    void rememberSlotLocked(uint8_t slotIndex, uint16_t connHandle, const PeerIdentity &identity);

    /**
     * @brief Marks a slot as explicitly disconnected by its phone.
     *
     * @pre policyLock is held by the caller.
     * @param slotIndex Shared-node client slot to mark disconnected.
     */
    void disconnectSlotLocked(uint8_t slotIndex);

    /**
     * @brief Resets one slot.
     *
     * @pre policyLock is held by the caller.
     * @param slotIndex Shared-node client slot to clear.
     */
    void clearSlotLocked(uint8_t slotIndex);

    /**
     * @brief Checks whether the admin slot contains a known admin identity.
     *
     * @pre policyLock is held by the caller.
     * @return true when a known admin identity exists.
     */
    bool hasKnownAdminLocked() const;

    /**
     * @brief Returns a non-zero seconds-since-boot timestamp.
     *
     * @return Current uptime in seconds, clamped to at least 1.
     */
    static uint32_t nowSeconds();

    /**
     * @brief Guards access to pairing state and client records.
     */
    mutable concurrency::Lock policyLock;

    /**
     * @brief Slot selected by beginPairing() and waiting for connection resolution.
     */
    uint8_t pendingPairingSlot = INVALID_SLOT;

    /**
     * @brief Indicates whether records have been loaded from NodeDB.
     */
    bool loadedFromNodeDB = false;

    /**
     * @brief Number of client records currently marked connected.
     */
    size_t connectedCount = 0;

    /**
     * @brief Fixed-size table of persisted client records.
     */
    std::array<ClientRecord, MAX_CONNECTIONS> records{};

    /**
     * @brief Predicate helper used to search the record table.
     */
    StaticSlotTable<ClientRecord, MAX_CONNECTIONS> recordSlots;
};

/**
 * @brief Global shared-node pairing policy instance.
 */
extern PairingPolicy pairingPolicy;

} // namespace SharedNode
#endif
