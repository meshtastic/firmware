#ifdef MODE_SHARED_NODE
#include "PairingPolicy.h"

/**
 * @file SharedNodePairingPolicy.cpp
 * @brief Implements shared-node pairing role and slot assignment.
 */

#include "configuration.h"
#include "RTC.h"
#include "concurrency/LockGuard.h"
#include "mesh/NodeDB.h"

#include <cstring>

namespace SharedNode {

/**
 * @brief Global shared-node pairing policy used by Bluetooth transports.
 */
PairingPolicy pairingPolicy;

/*
 * Mental model for this file:
 *
 * - SharedNode::PeerIdentity is the durable key. BLE backends must build it from bond data
 *   (IRK / identity address / peer ID), not from the current OTA address.
 * - connHandle is only a live transport handle. It is useful while connected,
 *   but it is cleared on disconnect and is never persisted as identity.
 * - connectionState is the lifecycle owner. EMPTY/DISCONNECTED are allocatable,
 *   NOT_ACTIVE/ACTIVE remain reserved for the known identity.
 * - pendingPairingSlot bridges the BLE passkey callback and the later
 *   authentication-complete callback, where the backend finally has the stable
 *   SharedNode::PeerIdentity. Role is always derived from the slot index.
 */
PairingPolicy::PairingPolicy() : recordSlots(records) {}

Pairing PairingPolicy::beginPairing()
{
    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();

    // The BLE stack may ask for the passkey before it exposes the peer's bond
    // identity. Reserve a slot now, then bind it to SharedNode::PeerIdentity
    // after authentication completes.
    Pairing pairing;
    if (pendingPairingSlot != SharedNode::INVALID_SLOT) {
        // Reuse the same pending decision if the stack repeats the passkey
        // request during one pairing flow.
        pairing.slot = pendingPairingSlot;
    } else {
        pairing = choosePairingLocked();
        pendingPairingSlot = pairing.slot;
    }

    // The first client (admin) gets a random PIN. Guests use the configured
    // shared PIN so the admin can hand it out without changing the admin bond.
    const Role role = roleForSlot(pairing.slot);
    if (role == Role::ADMIN) {
        pairing.passkey = random(100000, 999999);
    } else if (role == Role::GUEST) {
        pairing.passkey = config.bluetooth.fixed_pin;
    } else if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
        pairing.passkey = random(100000, 999999);
    } else {
        pairing.passkey = config.bluetooth.fixed_pin;
    }

    return pairing;
}

uint8_t PairingPolicy::consumePendingPairingSlot()
{
    concurrency::LockGuard guard(&policyLock);
    const uint8_t slot = pendingPairingSlot;
    pendingPairingSlot = SharedNode::INVALID_SLOT;
    return slot;
}

uint8_t PairingPolicy::peekPendingPairingSlot() const
{
    concurrency::LockGuard guard(&policyLock);
    return pendingPairingSlot;
}

Role PairingPolicy::roleForConnection(uint16_t connHandle) const
{
    concurrency::LockGuard guard(&policyLock);
    const_cast<PairingPolicy *>(this)->loadFromNodeDBLocked();
    const int8_t slot = findSlotByConnectionLocked(connHandle);
    return slot >= 0 ? roleForSlot(static_cast<uint8_t>(slot)) : Role::UNKNOWN;
}

Role PairingPolicy::roleForIdentity(const PeerIdentity &identity) const
{
    concurrency::LockGuard guard(&policyLock);
    const_cast<PairingPolicy *>(this)->loadFromNodeDBLocked();
    const int8_t slot = findSlotByIdentityLocked(identity);
    return slot >= 0 ? roleForSlot(static_cast<uint8_t>(slot)) : Role::UNKNOWN;
}

uint8_t PairingPolicy::slotForConnection(uint16_t connHandle) const
{
    concurrency::LockGuard guard(&policyLock);
    const_cast<PairingPolicy *>(this)->loadFromNodeDBLocked();
    const int8_t slot = findSlotByConnectionLocked(connHandle);
    return slot >= 0 ? static_cast<uint8_t>(slot) : SharedNode::INVALID_SLOT;
}

uint8_t PairingPolicy::slotForIdentity(const PeerIdentity &identity) const
{
    concurrency::LockGuard guard(&policyLock);
    const_cast<PairingPolicy *>(this)->loadFromNodeDBLocked();
    const int8_t slot = findSlotByIdentityLocked(identity);
    return slot >= 0 ? static_cast<uint8_t>(slot) : SharedNode::INVALID_SLOT;
}

uint32_t PairingPolicy::virtualNodeIdForSlot(uint8_t slotIndex) const
{
    concurrency::LockGuard guard(&policyLock);
    const_cast<PairingPolicy *>(this)->loadFromNodeDBLocked();
    return (slotIndex < records.size() && records[slotIndex].hasIdentity()) ? records[slotIndex].virtualNodeId : 0;
}

bool PairingPolicy::setVirtualNodeIdForSlot(uint8_t slotIndex, uint32_t virtualNodeId)
{
    // Virtual node ID 0 has special packet semantics, so guest IDs start at a
    // non-zero value allocated by VirtualNodeManager.
    if (slotIndex >= records.size() || roleForSlot(slotIndex) != Role::GUEST || virtualNodeId == 0) {
        return false;
    }

    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();

    ClientRecord &record = records[slotIndex];
    if (!record.hasIdentity()) {
        return false;
    }

    // No persistence needed if this slot is already bound to the same virtual
    // node ID.
    if (record.virtualNodeId == virtualNodeId) {
        return true;
    }

    record.virtualNodeId = virtualNodeId;
    // Guest names are deterministic from the virtual ID so reconnecting guests
    // keep recognizable local node labels.
    if (roleForSlot(slotIndex) == Role::GUEST) {
        snprintf(record.shortName, sizeof(record.shortName), "G%02X", static_cast<unsigned>(virtualNodeId) & 0xff);
        snprintf(record.longName, sizeof(record.longName), "Guest %02X", static_cast<unsigned>(virtualNodeId) & 0xff);
    }
    persistToNodeDBLocked();
    return true;
}

Role PairingPolicy::resolveRoleForConnection(uint16_t connHandle, const PeerIdentity &identity)
{
    return roleForSlot(resolveSlotForConnection(connHandle, identity));
}

uint8_t PairingPolicy::resolveSlotForConnection(uint16_t connHandle, const PeerIdentity &identity)
{
    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();

    if (!identity) {
        LOG_WARN("Shared-node cannot resolve BLE conn %u without a bond identity", connHandle);
        pendingPairingSlot = SharedNode::INVALID_SLOT;
        return SharedNode::INVALID_SLOT;
    }

    // Resolution order matters:
    // 1. a known durable identity always wins;
    int8_t slot = findSlotByIdentityLocked(identity);

    // 2. otherwise use the slot reserved when the passkey was shown;
    if (slot < 0 && pendingPairingSlot != SharedNode::INVALID_SLOT) {
        slot = pendingPairingSlot;
    }

    // 3. if the backend did not call beginPairing(), choose a fresh slot now.
    if (slot < 0 || static_cast<size_t>(slot) >= records.size()) {
        Pairing pairing = choosePairingLocked();
        slot = pairing.slot;
    }

    pendingPairingSlot = SharedNode::INVALID_SLOT;

    // From here on, connHandle is only an in-memory live binding for PhoneAPI
    // routing. The stable thing persisted to NodeDB is identity.
    if (slot >= 0 && roleForSlot(static_cast<uint8_t>(slot)) != Role::UNKNOWN) {
        rememberSlotLocked(static_cast<uint8_t>(slot), connHandle, identity);
        return static_cast<uint8_t>(slot);
    }

    return SharedNode::INVALID_SLOT;
}

bool PairingPolicy::hasKnownAdmin() const
{
    concurrency::LockGuard guard(&policyLock);
    const_cast<PairingPolicy *>(this)->loadFromNodeDBLocked();
    return hasKnownAdminLocked();
}

void PairingPolicy::rememberConnectionSlot(uint16_t connHandle, const PeerIdentity &identity, uint8_t slotIndex)
{
    // INVALID_SLOT means the backend has not authenticated/resolved this peer yet.
    if (roleForSlot(slotIndex) == Role::UNKNOWN) {
        return;
    }

    if (!identity) {
        LOG_WARN("Shared-node cannot remember BLE conn %u without a bond identity", connHandle);
        return;
    }

    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();

    // Reconnect path: if the backend can resolve an existing bond identity
    // during onConnect/onSecured, attach the live handle to that known slot.
    int8_t slot = findSlotByIdentityLocked(identity);
    if (slot < 0) {
        slot = slotIndex;
    }

    if (slot < 0 || roleForSlot(static_cast<uint8_t>(slot)) == Role::UNKNOWN) {
        LOG_WARN("Shared-node has no valid slot for BLE conn %u", connHandle);
        return;
    }

    rememberSlotLocked(static_cast<uint8_t>(slot), connHandle, identity);
}

void PairingPolicy::clearConnection(uint16_t connHandle)
{
    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();

    const int8_t slot = findSlotByConnectionLocked(connHandle);
    if (slot < 0) {
        return;
    }

    ClientRecord &record = records[slot];
    if (record.isActive() && connectedCount > 0) {
        connectedCount--;
    }

    // A BLE drop only clears the live handle. The identity remains reserved as
    // NOT_ACTIVE so a later reconnect can reclaim the same slot.
    record.connectionState = ConnectionState::NOT_ACTIVE;
    record.connHandle = 0;
    if (pendingPairingSlot == static_cast<uint8_t>(slot)) {
        pendingPairingSlot = SharedNode::INVALID_SLOT;
    }
}

void PairingPolicy::disconnectSlot(uint8_t slotIndex)
{
    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();
    if (slotIndex >= records.size()) {
        return;
    }

    disconnectSlotLocked(slotIndex);
    persistToNodeDBLocked();
}

void PairingPolicy::invalidateSlot(uint8_t slotIndex)
{
    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();
    if (slotIndex >= records.size()) {
        return;
    }

    clearSlotLocked(slotIndex);
    persistToNodeDBLocked();
}

void PairingPolicy::clearAll()
{
    concurrency::LockGuard guard(&policyLock);
    loadFromNodeDBLocked();

    pendingPairingSlot = SharedNode::INVALID_SLOT;
    connectedCount = 0;
    for (uint8_t i = 0; i < records.size(); i++) {
        clearSlotLocked(i);
    }
    persistToNodeDBLocked();
}

void PairingPolicy::loadFromNodeDBLocked()
{
    if (loadedFromNodeDB) {
        return;
    }

    for (uint8_t i = 0; i < records.size(); i++) {
        records[i] = ClientRecord{};
    }

    if (nodeDB) {
        nodeDB->copySharedNodeRecords(records.data(), records.size());
    }

    // Persisted records describe known identities. Live connection handles are
    // reconstructed after boot, so never trust connHandle from storage.
    connectedCount = 0;
    for (uint8_t i = 0; i < records.size(); i++) {
        if (records[i].connectionState == ConnectionState::ACTIVE) {
            records[i].connectionState = ConnectionState::NOT_ACTIVE;
        }
        records[i].connHandle = 0;
    }
    loadedFromNodeDB = true;
}

void PairingPolicy::persistToNodeDBLocked()
{
    if (nodeDB) {
        nodeDB->saveSharedNodeRecords(records.data(), records.size());
    }
}

Pairing PairingPolicy::choosePairingLocked()
{
    Pairing pairing;

    // Slot 0 is special: the first stable identity to pair becomes the admin.
    // Once it exists, all future new pairings are guests.
    if (!hasKnownAdminLocked() && records[SharedNode::ADMIN_SLOT].canAllocate()) {
        pairing.slot = SharedNode::ADMIN_SLOT;
        return pairing;
    }

    const int8_t guestSlot = findAvailableGuestSlotLocked();
    if (guestSlot >= 0) {
        pairing.slot = static_cast<uint8_t>(guestSlot);
    }
    return pairing;
}

int8_t PairingPolicy::findSlotByConnectionLocked(uint16_t connHandle) const
{
    return recordSlots.findIndex(
        [connHandle](const ClientRecord &record) { return record.isActive() && record.connHandle == connHandle; });
}

int8_t PairingPolicy::findSlotByIdentityLocked(const PeerIdentity &identity) const
{
    if (!identity) {
        return -1;
    }

    return recordSlots.findIndex(
        [&identity](const ClientRecord &record) { return record.hasIdentity() && record.peerIdentity == identity; });
}

int8_t PairingPolicy::findAvailableGuestSlotLocked() const
{
    // New peers may reuse only never-used slots first, then slots explicitly
    // disconnected by the app. Known inactive and active slots stay reserved
    // for their current peer identity.
    int8_t slot = recordSlots.findIndex(
        [](const ClientRecord &record) { return record.connectionState == ConnectionState::EMPTY; }, 1);
    if (slot >= 0) {
        return slot;
    }

    return recordSlots.findIndex(
        [](const ClientRecord &record) { return record.connectionState == ConnectionState::DISCONNECTED; }, 1);
}

void PairingPolicy::rememberSlotLocked(uint8_t slotIndex, uint16_t connHandle, const PeerIdentity &identity)
{
    // Validation
    if (slotIndex >= records.size()) {
        return;
    }

    if (!identity) {
        LOG_WARN("Shared-node slot %u cannot be remembered without a bond identity", static_cast<unsigned>(slotIndex));
        return;
    }

    ClientRecord &record = records[slotIndex];
    if (record.isActive() && record.connHandle != connHandle && record.peerIdentity != identity) {
        // A slot can have at most one live connection. This protects against a
        // second peer taking over an active guest/admin slot.
        LOG_WARN("Shared-node slot %u is already active", slotIndex);
        return;
    }

    const bool wasActive = record.isActive();
    const bool wasDisconnected = record.connectionState == ConnectionState::DISCONNECTED;
    const bool changedIdentity = !record.hasIdentity() || record.peerIdentity != identity;

    if (changedIdentity) {
        // The slot identity changed, so reset per-client metadata. Guest virtual
        // node ID intentionally stays attached to the slot unless reassigned.
        const Role role = roleForSlot(slotIndex);
        const uint32_t previousVirtualNodeId = (role == Role::GUEST) ? record.virtualNodeId : 0;
        record = SharedNode::ClientRecord{};
        record.peerIdentity = identity;
        record.virtualNodeId = previousVirtualNodeId;
        record.registerTime = nowSeconds();
    }

    record.connectionState = ConnectionState::ACTIVE;
    record.connHandle = connHandle;
    record.lastSeen = nowSeconds();

    if (!wasActive) {
        connectedCount++;
    }

    // Persist durable identity changes. If the same identity reconnects
    // from DISCONNECTED, save it back as NOT_ACTIVE so the slot is reserved
    // again after reboot.
    if (changedIdentity || wasDisconnected) {
        persistToNodeDBLocked();
    }
}

void PairingPolicy::disconnectSlotLocked(uint8_t slotIndex)
{
    if (slotIndex >= records.size()) {
        return;
    }

    ClientRecord &record = records[slotIndex];
    if (record.isActive() && connectedCount > 0) {
        connectedCount--;
    }

    const bool hadIdentity = record.hasIdentity();
    const PeerIdentity peerIdentity = record.peerIdentity;
    const uint32_t virtualNodeId = record.virtualNodeId;
    const uint32_t registerTime = record.registerTime;
    char shortName[SHORT_NAME_SIZE] = {};
    char longName[LONG_NAME_SIZE] = {};
    strncpy(shortName, record.shortName, sizeof(shortName) - 1);
    strncpy(longName, record.longName, sizeof(longName) - 1);

    record = SharedNode::ClientRecord{};
    record.connectionState = ConnectionState::DISCONNECTED;
    record.connHandle = 0;
    if (hadIdentity) {
        record.peerIdentity = peerIdentity.c_str();
    }
    record.virtualNodeId = virtualNodeId;
    record.registerTime = registerTime;
    strncpy(record.shortName, shortName, sizeof(record.shortName) - 1);
    strncpy(record.longName, longName, sizeof(record.longName) - 1);
    record.lastSeen = nowSeconds();

    if (pendingPairingSlot == slotIndex) {
        pendingPairingSlot = SharedNode::INVALID_SLOT;
    }
}

void PairingPolicy::clearSlotLocked(uint8_t slotIndex)
{
    if (slotIndex >= records.size()) {
        return;
    }
    recordSlots.invalidate(slotIndex);
}

bool PairingPolicy::hasKnownAdminLocked() const
{
    const ClientRecord &admin = records[ADMIN_SLOT];
    // An admin is considered claimed only after a backend supplied a formatted
    // stable identity. A pending pairing alone must not block first-admin setup.
    return admin.hasIdentity() && admin.peerIdentity.stable();
}

uint32_t PairingPolicy::nowSeconds()
{
    uint32_t now = getValidTime(RTCQualityFromNet);
    if (now == 0) {
        now = millis() / 1000;
        if (now == 0) {
            now = 1;  // Fallback: ensure non-zero
        }
    }
    return now;
}

} // namespace SharedNode
#endif