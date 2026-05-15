#ifdef MODE_SHARED_NODE
#pragma once

/**
 * @file SharedNodeTypes.h
 * @brief Shared-node role, peer identity, and persisted client record types.
 */

#include <Arduino.h>
#include <cstring>

#ifndef SHARED_NODE_MAX_GUESTS
#define SHARED_NODE_MAX_GUESTS 3
#endif

/**
 * @brief Shared-node namespace for small value types and helpers.
 */
namespace SharedNode
{

/**
 * @brief Maximum number of guest clients allowed in shared-node mode.
 */
static constexpr size_t MAX_GUESTS = SHARED_NODE_MAX_GUESTS;

/**
 * @brief Maximum number of shared-node client connections.
 *
 * The value includes the admin slot plus all guest slots.
 */
static constexpr size_t MAX_CONNECTIONS = SHARED_NODE_MAX_GUESTS + 1;

/**
 * @brief Slot index reserved for the admin client.
 */
static constexpr uint8_t ADMIN_SLOT = 0;

/**
 * @brief Sentinel slot value used when no shared-node slot is assigned.
 */
static constexpr uint8_t INVALID_SLOT = 0xff;

/**
 * @brief Peer identity buffer size, including the null terminator.
 */
static constexpr size_t PEER_IDENTITY_SIZE = 32;

/**
 * @brief Generated short-name buffer size, including the null terminator.
 */
static constexpr size_t SHORT_NAME_SIZE = 5;

/**
 * @brief Generated long-name buffer size, including the null terminator.
 */
static constexpr size_t LONG_NAME_SIZE = 40;

/**
 * @brief Shared-node connection role.
 *
 * This is a mesh/session concept, not a Bluetooth concept. BLE backends use it
 * only to bind an authenticated transport connection to a PhoneAPI session.
 */
enum class Role : uint8_t {
    /**
     * @brief Role has not been resolved or is not known.
     */
    UNKNOWN = 0,

    /**
     * @brief Admin session with permission to manage shared-node state.
     */
    ADMIN = 1,

    /**
     * @brief Guest session that receives a virtual node identity.
     */
    GUEST = 2,
};

/**
 * @brief Returns the shared-node role implied by a slot index.
 *
 * Slot 0 is the admin identity. Every valid non-zero slot is a guest identity.
 * INVALID_SLOT and out-of-range slot values do not imply a role.
 *
 * @param slotIndex Shared-node client slot.
 * @return Role implied by the slot, or UNKNOWN for invalid slot values.
 */
inline Role roleForSlot(uint8_t slotIndex)
{
    if (slotIndex == INVALID_SLOT || slotIndex >= MAX_CONNECTIONS) {
        return Role::UNKNOWN;
    }
    return slotIndex == ADMIN_SLOT ? Role::ADMIN : Role::GUEST;
}

/**
 * @brief Lifecycle/connection state for one shared-node slot.
 *
 * Allocation intentionally only considers states 0 and 1. States 2 and 3 keep
 * ownership for a known phone: 2 may reconnect later, and 3 is currently live.
 */
enum class ConnectionState : uint8_t {
    /**
     * @brief Slot has never been assigned to a peer.
     */
    EMPTY = 0,

    /**
     * @brief Peer explicitly sent ToRadio.disconnect and released the slot.
     *
     * The BLE bond identity is still retained so the same OS-level paired
     * device can reconnect without becoming a new guest. This state is also
     * allocatable when no EMPTY slots remain.
     */
    DISCONNECTED = 1,

    /**
     * @brief Peer owns this slot, but the BLE link is not active right now.
     */
    NOT_ACTIVE = 2,

    /**
     * @brief Peer owns this slot and currently has a live BLE connection.
     */
    ACTIVE = 3,
};

/**
 * @brief Checks whether a connection state retains a peer identity.
 *
 * DISCONNECTED still retains identity because ToRadio.disconnect is an
 * application-level disconnect, not an OS-level bond removal.
 */
inline bool connectionStateRetainsIdentity(ConnectionState state)
{
    return state == ConnectionState::DISCONNECTED || state == ConnectionState::NOT_ACTIVE || state == ConnectionState::ACTIVE;
}

/**
 * @brief Checks whether a new peer may be assigned to a slot in this state.
 */
inline bool connectionStateCanAllocate(ConnectionState state)
{
    return state == ConnectionState::EMPTY || state == ConnectionState::DISCONNECTED;
}

/**
 * @brief Decodes a persisted connection state value.
 */
inline ConnectionState connectionStateFromValue(uint32_t value)
{
    switch (static_cast<ConnectionState>(value)) {
    case ConnectionState::EMPTY:
    case ConnectionState::DISCONNECTED:
    case ConnectionState::NOT_ACTIVE:
    case ConnectionState::ACTIVE:
        return static_cast<ConnectionState>(value);
    default:
        return ConnectionState::EMPTY;
    }
}

/**
 * @brief Null-terminated stack-provided bond identity wrapper.
 *
 * Bluetooth backends format platform-specific identity information from their
 * bonding layer into this fixed buffer before the pairing policy compares or
 * persists it. The value should come from the peer identity address, IRK-backed
 * bond record, or equivalent stack peer ID, not from the current over-the-air
 * BLE address.
 */
struct PeerIdentity {
    /**
     * @brief Null-terminated identity storage.
     */
    char value[PEER_IDENTITY_SIZE] = {};

    /**
     * @brief Clears the stored identity.
     */
    void clear() { value[0] = '\0'; }

    /**
     * @brief Checks whether the stored identity is present.
     *
     * This lets callers write `if (identity)` for the "has identity" case.
     *
     * @return true when an identity string is stored.
     */
    explicit operator bool() const { return !operator!(); }

    /**
     * @brief Checks whether the stored identity is empty.
     *
     * This lets callers write `if (!identity)` for the "no identity" case.
     *
     * @return true when no identity is stored.
     */
    bool operator!() const { return value[0] == '\0'; }

    /**
     * @brief Checks whether this value was produced by a bond identity formatter.
     *
     * Backends prefix identities so records from different BLE stacks cannot
     * collide accidentally.
     *
     * @return true when the value uses a stable identity prefix.
     */
    bool stable() const
    {
        return strncmp(value, "bf:", 3) == 0 || strncmp(value, "nb:", 3) == 0;
    }

    /**
     * @brief Returns the stored identity as a C string.
     *
     * @return Null-terminated identity string.
     */
    const char *c_str() const { return value; }

    /**
     * @brief Assigns a peer identity string to this wrapper.
     *
     * A null input clears the identity. Non-null input is truncated to fit the
     * fixed-size buffer and is always null-terminated.
     *
     * @param identity Source identity string, or nullptr to clear.
     */
    PeerIdentity &operator=(const char *identity)
    {
        if (!identity) {
            clear();
            return *this;
        }

        strncpy(value, identity, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        return *this;
    }

    /**
     * @brief Compares this identity with another PeerIdentity.
     *
     * @param other Identity wrapper to compare with.
     * @return true when both identities are non-empty and equal.
     */
    bool operator==(const PeerIdentity &other) const { return *this == other.c_str(); }

    /**
     * @brief Compares this identity with another PeerIdentity for inequality.
     *
     * @param other Identity wrapper to compare with.
     * @return true when operator== returns false.
     */
    bool operator!=(const PeerIdentity &other) const { return !(*this == other); }

    /**
     * @brief Compares this identity with a C string.
     *
     * Empty identities never compare equal.
     *
     * @param other Identity string to compare with.
     * @return true when both identities are non-empty and equal.
     */
    bool operator==(const char *other) const
    {
        return *this && other && strncmp(value, other, sizeof(value)) == 0;
    }

    /**
     * @brief Compares this identity with a C string for inequality.
     *
     * @param other Identity string to compare with.
     * @return true when operator== returns false.
     */
    bool operator!=(const char *other) const { return !(*this == other); }
};

/**
 * @brief Persisted pairing and runtime state for one shared-node client slot.
 */
struct ClientRecord {
    /**
     * @brief Slot lifecycle state used for allocation and reconnect handling.
     */
    ConnectionState connectionState = ConnectionState::EMPTY;

    /**
     * @brief Active transport connection handle, or 0 when disconnected.
     */
    uint16_t connHandle = 0;

    /**
     * @brief Virtual node ID assigned to a guest client.
     */
    uint32_t virtualNodeId = 0;

    /**
     * @brief Stable bond identity used to match reconnecting clients.
     */
    PeerIdentity peerIdentity = {};

    /**
     * @brief Generated short node name for the virtual identity.
     */
    char shortName[SHORT_NAME_SIZE] = {};

    /**
     * @brief Generated long node name for the virtual identity.
     */
    char longName[LONG_NAME_SIZE] = {};

    /**
     * @brief Seconds-since-boot timestamp when this identity was registered.
     */
    uint32_t registerTime = 0;

    /**
     * @brief Seconds-since-boot timestamp when this identity was last seen.
     */
    uint32_t lastSeen = 0;

    /**
     * @brief Checks whether this slot is available for a newly paired peer.
     */
    bool canAllocate() const { return connectionStateCanAllocate(connectionState); }

    /**
     * @brief Checks whether this slot currently owns a durable peer identity.
     */
    bool hasIdentity() const
    {
        return connectionStateRetainsIdentity(connectionState) && peerIdentity;
    }

    /**
     * @brief Checks whether this slot has a live transport connection.
     */
    bool isActive() const { return connectionState == ConnectionState::ACTIVE; }
};

} // namespace SharedNode
#endif
