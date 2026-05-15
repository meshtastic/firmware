#pragma once

/**
 * @file BluetoothTransport.h
 * @brief Shared Bluetooth transport helpers for fixed-slot PhoneAPI handling.
 */

#include "mesh/PhoneAPI.h"
#ifdef MODE_SHARED_NODE
#include "mesh/sharedNode/PairingPolicy.h"
#endif
#include "mesh/sharedNode/static/ObjectPool.h"
#include "mesh/sharedNode/static/SlotTable.h"

#include <array>
#include <cstring>

/**
 * @namespace bluetooth
 * @brief Bluetooth transport helpers shared by platform-specific backends.
 */
namespace bluetooth
{

#ifdef MODE_SHARED_NODE
static constexpr size_t MAX_BLUETOOTH_CONNECTIONS = SharedNode::MAX_CONNECTIONS;
#else
static constexpr size_t MAX_BLUETOOTH_CONNECTIONS = 1;
#endif

/**
 * @brief Fixed-capacity PhoneAPI pool keyed by Bluetooth connection handle.
 *
 * PhoneApiPool owns PhoneAPI-like instances without heap allocation. It creates
 * APIs lazily, applies the current shared-node slot to reused or newly created
 * instances, and closes APIs when their connection is removed.
 *
 * @tparam ApiT PhoneAPI-compatible type constructed from a uint16_t connection
 * handle and exposing close() and setSharedNodeSlot().
 * @tparam MaxConnections Maximum number of simultaneously pooled APIs.
 */
template <typename ApiT, size_t MaxConnections = MAX_BLUETOOTH_CONNECTIONS> class PhoneApiPool
{
  public:
    /**
     * @brief Finds the API instance associated with a connection handle.
     *
     * @param connHandle Bluetooth connection handle.
     * @return API instance, or nullptr when no instance exists.
     */
    ApiT *find(uint16_t connHandle)
    {
        return pool.find(connHandle);
    }

    /**
     * @brief Finds or creates the API instance for a connection handle.
     *
     * The instance receives the currently resolved shared-node slot before it
     * is returned.
     *
     * @param connHandle Bluetooth connection handle.
     * @return API instance, or nullptr when the pool is full.
     */
    ApiT *ensure(uint16_t connHandle)
    {
        ApiT *api = pool.ensure(connHandle, connHandle);
        if (!api) {
            return nullptr;
        }
        applySlot(connHandle, api);
        return api;
    }

    /**
     * @brief Closes and removes the API instance for a connection handle.
     *
     * The shared-node pairing policy is also notified that the connection is no
     * longer active.
     *
     * @param connHandle Bluetooth connection handle.
     */
    void remove(uint16_t connHandle)
    {
        pool.remove(connHandle, [](ApiT *api, const uint16_t &) {
            api->close();
        });
#ifdef MODE_SHARED_NODE
        SharedNode::pairingPolicy.clearConnection(connHandle);
#endif
    }

    /**
     * @brief Visits, closes, and removes every API instance in the pool.
     *
     * @tparam Visitor Callable with signature compatible with `void(ApiT *api)`.
     * @param visitor Callable invoked before each API is closed.
     */
    template <typename Visitor> void closeAll(Visitor visitor)
    {
        pool.clear([&visitor](ApiT *api, const uint16_t &connHandle) {
            visitor(api);
            api->close();
#ifdef MODE_SHARED_NODE
            SharedNode::pairingPolicy.clearConnection(connHandle);
#else
            (void)connHandle;
#endif
        });
    }

    /**
     * @brief Closes and removes every API instance in the pool.
     */
    void closeAll()
    {
        closeAll([](ApiT *) {});
    }

  private:
    /**
     * @brief Applies the resolved shared-node slot to an API.
     *
     * @param connHandle Bluetooth connection handle.
     * @param api API instance to update.
     */
    static void applySlot(uint16_t connHandle, ApiT *api)
    {
        if (!api) {
            return;
        }

#ifdef MODE_SHARED_NODE
        api->setSharedNodeSlot(SharedNode::pairingPolicy.slotForConnection(connHandle));
#else
        (void)connHandle;
#endif
    }

    /**
     * @brief Static object pool storing APIs by connection handle.
     */
    StaticObjectPool<ApiT, MaxConnections, uint16_t> pool;
};

/**
 * @brief Per-connection duplicate filter for ToRadio payloads.
 *
 * DuplicateToRadioTracker remembers the last payload sent by each Bluetooth
 * connection and reports whether a new payload differs from the last one. It is
 * intended to suppress immediate duplicate ToRadio writes from the same client.
 *
 * @tparam MaxConnections Maximum number of tracked Bluetooth connections.
 */
template <size_t MaxConnections = MAX_BLUETOOTH_CONNECTIONS> class DuplicateToRadioTracker
{
  public:
    /**
     * @brief Creates a tracker backed by the internal static slot table.
     */
    DuplicateToRadioTracker() : slotTable(slots) {}

    /**
     * @brief Remembers a payload for a connection if it differs from the last one.
     *
     * @param connHandle Bluetooth connection handle.
     * @param data Payload bytes to compare and remember.
     * @param len Number of payload bytes in @p data.
     * @return true when the payload is valid and new for this connection.
     */
    bool rememberIfNew(uint16_t connHandle, const uint8_t *data, size_t len)
    {
        if (!data || len > MAX_TO_FROM_RADIO_SIZE) {
            return false;
        }

        Slot *slot = slotTable.find([connHandle](const Slot &candidate) { return candidate.used && candidate.connHandle == connHandle; });
        if (!slot) {
            slot = slotTable.allocate([](const Slot &candidate) { return !candidate.used; });
            if (slot) {
                slot->used = true;
                slot->connHandle = connHandle;
            }
        }
        if (!slot) {
            return false;
        }

        if (slot->lastLen == len && memcmp(slot->lastPacket.data(), data, len) == 0) {
            return false;
        }

        memcpy(slot->lastPacket.data(), data, len);
        slot->lastLen = len;
        return true;
    }

    /**
     * @brief Clears the remembered payload for one connection.
     *
     * @param connHandle Bluetooth connection handle.
     */
    void clear(uint16_t connHandle)
    {
        Slot *slot = slotTable.find([connHandle](const Slot &candidate) { return candidate.used && candidate.connHandle == connHandle; });
        if (slot) {
            *slot = Slot{};
        }
    }

    /**
     * @brief Clears remembered payloads for all connections.
     */
    void clearAll()
    {
        for (auto &slot : slots) {
            slot = Slot{};
        }
    }

  private:
    /**
     * @brief Last payload remembered for one Bluetooth connection.
     */
    struct Slot {
        /**
         * @brief Indicates whether this slot currently tracks a connection.
         */
        bool used = false;

        /**
         * @brief Bluetooth connection handle associated with this slot.
         */
        uint16_t connHandle = 0;

        /**
         * @brief Last payload bytes seen for this connection.
         */
        std::array<uint8_t, MAX_TO_FROM_RADIO_SIZE> lastPacket{};

        /**
         * @brief Number of bytes stored in lastPacket.
         */
        size_t lastLen = 0;
    };

    /**
     * @brief Fixed-size duplicate tracking slots.
     */
    std::array<Slot, MaxConnections> slots{};

    /**
     * @brief Predicate helper used to search tracking slots.
     */
    StaticSlotTable<Slot, MaxConnections> slotTable;
};

} // namespace bluetooth
