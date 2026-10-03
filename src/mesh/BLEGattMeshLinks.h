#pragma once

#if HAS_BLE_GATT_MESH

#include "BLEGattMeshHandler.h"

#include <algorithm>
#include <array>
#include <cstring>

// Received writes waiting for the main task; each holds one ATT value, so up to 512 bytes.
#ifndef BLE_GATT_MESH_RX_QUEUE_SIZE
#define BLE_GATT_MESH_RX_QUEUE_SIZE 6
#endif

/**
 * The peripheral link table and RX ring the platform glue shares. The BLE stack's task writes them
 * from its callbacks and the pump reads them on the main task, so the platform holds its own lock
 * around every call. Nothing here logs: a caller logs after releasing that lock.
 */
template <size_t Links, size_t RxSlots = BLE_GATT_MESH_RX_QUEUE_SIZE> class BLEGattMeshLinks
{
  public:
    struct Link {
        bool used;
        uint16_t conn;
        bool subscribed;     // wrote the CCCD: a notify target, and the mark of a mesh peer
        bool everSubscribed; // subscribed at any point, which outlives an unsubscribe
    };

    std::array<Link, Links> links{};
    uint32_t rxAccepted = 0;
    uint32_t rxDropped = 0;

    Link *find(uint16_t conn)
    {
        for (auto &l : links) {
            if (l.used && l.conn == conn)
                return &l;
        }
        return nullptr;
    }

    Link *add(uint16_t conn)
    {
        if (Link *l = find(conn))
            return l;
        for (auto &l : links) {
            if (l.used)
                continue;
            l.used = true;
            l.conn = conn;
            l.subscribed = false;
            l.everSubscribed = false;
            return &l;
        }
        return nullptr;
    }

    size_t count() const
    {
        size_t n = 0;
        for (const auto &l : links)
            n += l.used ? 1 : 0;
        return n;
    }

    void setSubscribed(uint16_t conn, bool subscribed)
    {
        if (Link *l = add(conn)) {
            l->subscribed = subscribed;
            l->everSubscribed |= subscribed;
        }
    }

    /// Forget the link and queue its disconnect marker. True when it had ever subscribed.
    bool remove(uint16_t conn)
    {
        Link *l = find(conn);
        if (!l)
            return false;
        const bool wasPeer = l->everSubscribed;
        l->used = false;
        pushRx(conn, nullptr, 0); // the pump drops its half-built packets
        return wasPeer;
    }

    /// Up to cap subscribed links, in table order.
    size_t subscribed(BLEGattMeshPeer *out, size_t cap) const
    {
        size_t n = 0;
        for (const auto &l : links) {
            if (!l.used || !l.subscribed)
                continue;
            if (n >= cap)
                break;
            out[n].id = l.conn;
            out[n].outbound = false; // every link here was dialled by the peer
            n++;
        }
        return n;
    }

    /// Queue one write, or a disconnect marker when len is 0. False when a write was turned away.
    bool pushRx(uint16_t conn, const uint8_t *data, uint16_t len)
    {
        if (rxCount >= rxQueue.size()) {
            if (len) {
                rxDropped++;
                return false;
            }
            // A disconnect marker must land or the pump keeps that handle's half-built packets for the
            // next peer the stack gives it: overwrite the newest chunk, which belongs to a dead link anyway.
            rxTail = (rxTail + rxQueue.size() - 1) % rxQueue.size();
            rxCount--;
        }
        RxChunk &r = rxQueue[rxTail];
        r.conn = conn;
        r.len = len;
        if (len)
            memcpy(r.data, data, len);
        rxTail = (rxTail + 1) % rxQueue.size();
        rxCount++;
        if (len)
            rxAccepted++;
        return true;
    }

    /// Take the oldest queued chunk; len 0 is a disconnect marker. False when none waits.
    bool popRx(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len)
    {
        if (rxCount == 0)
            return false;
        const RxChunk &r = rxQueue[rxHead];
        peer = r.conn;
        len = std::min<size_t>(r.len, cap);
        if (len)
            memcpy(buf, r.data, len);
        rxHead = (rxHead + 1) % rxQueue.size();
        rxCount--;
        return true;
    }

    void reset()
    {
        for (auto &l : links)
            l.used = false;
        rxHead = rxTail = rxCount = 0;
    }

  private:
    struct RxChunk {
        uint16_t conn;
        uint16_t len; // 0 marks a disconnect
        uint8_t data[BLE_GATT_MESH_MAX_CHUNK];
    };
    std::array<RxChunk, RxSlots> rxQueue{};
    size_t rxHead = 0;
    size_t rxTail = 0;
    size_t rxCount = 0;
};

#endif // HAS_BLE_GATT_MESH
