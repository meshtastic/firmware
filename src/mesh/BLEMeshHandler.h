#pragma once

#if HAS_BLE_MESH

#include "MeshTransportBase.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Router.h"
#include "concurrency/OSThread.h"
#include "mesh-pb-constants.h"

#include <array>

// Meshtastic BLE mesh manufacturer data identifier. 0xFFFF is the SIG-reserved "internal/test"
// company ID, not usable in a shipping build.
#define BLE_MESH_COMPANY_ID 0xFFFF
#define BLE_MESH_PROTOCOL_VERSION 1

// One HCI LE Extended Advertising Report carries at most 229 bytes (Core 5.4 Vol 4 Part E
// 7.7.65.13), and Linux delivers nothing above that intact. The budget node-kmp shares.
#define BLE_MESH_ADV_TOTAL_MAX 229

// Flags AD structure (3) + manufacturer-data AD header (2) + company ID (2) + version (1).
#define BLE_MESH_ADV_OVERHEAD 8
#define BLE_MESH_MAX_PROTO_LEN (BLE_MESH_ADV_TOTAL_MAX - BLE_MESH_ADV_OVERHEAD)

// A relay can grow a packet by up to 5 bytes (next_hop on a DM), so one this node originates leaves
// that room for the next hop to re-advertise it.
#define BLE_MESH_MAX_ORIGIN_PROTO_LEN (BLE_MESH_MAX_PROTO_LEN - 5)

// Outbound frames waiting for the advertiser. Extended advertising is set-and-repeat, not a packet
// queue, so a burst has to be clocked through one frame at a time.
#ifndef BLE_MESH_TX_QUEUE_SIZE
#define BLE_MESH_TX_QUEUE_SIZE 8
#endif

// Advertising interval in units of 0.625 ms.
#ifndef BLE_MESH_ADV_INTERVAL
#define BLE_MESH_ADV_INTERVAL 48 // 30 ms
#endif

/// `units` x 0.625 ms, in ms.
constexpr int32_t bleMeshUnitsToMs(uint32_t units)
{
    return (int32_t)(units * 625 / 1000);
}

// Burst length is a per-node radio parameter, sized for the slowest scanner the mesh must reach, and
// need not match other implementations. That scanner is a phone whose app is not visible: Android runs
// its scan at LOW_POWER, a 140 ms window every 1.4 s. A burst spanning one interval plus one window
// always contains a whole window, and that window holds at least three 30-40 ms events; the two extra
// advertising intervals cover a burst starting or ending mid-event.
#ifndef BLE_MESH_PEER_SCAN_INTERVAL_MS
#define BLE_MESH_PEER_SCAN_INTERVAL_MS 1400
#endif
#ifndef BLE_MESH_PEER_SCAN_WINDOW_MS
#define BLE_MESH_PEER_SCAN_WINDOW_MS 140
#endif
#define BLE_MESH_ADV_BURST_MS                                                                                                    \
    (BLE_MESH_PEER_SCAN_INTERVAL_MS + BLE_MESH_PEER_SCAN_WINDOW_MS + 2 * bleMeshUnitsToMs(BLE_MESH_ADV_INTERVAL))

// Both controllers bound a burst by time in 10 ms units; rounded up so the burst is never shorter.
#define BLE_MESH_ADV_BURST_10MS ((BLE_MESH_ADV_BURST_MS + 9) / 10)
static_assert(BLE_MESH_ADV_BURST_10MS <= 0xFFFF, "burst must fit the controllers' 16-bit duration");

// Frames are advertised one at a time, so a 1.6 s burst caps a node near 0.6 frames/s. A backlog waits
// in the TX queue, and past its depth a frame is refused and counted in txDroppedQueueFull.

/**
 * Carries mesh frames between nodes over connectionless BLE extended advertisements.
 *
 * A second broadcast transport alongside LoRa, wired as UdpMulticastHandler is. Never the only path
 * to the mesh: Router::send() still asserts a LoRa iface.
 *
 * One advertisement reaches every neighbour at once, the same one-to-many shape LoRa has, which is
 * what makes onCancelSending() dupe suppression possible. Strictly same-medium.
 *
 * onSend() only encodes and queues; runOnce() clocks the advertising on the main thread. Advertising
 * inline would stall Router::send(), and with it LoRa timing and the whole main loop.
 */
class BLEMeshHandler : private concurrency::OSThread, public MeshTransportBase
{
  public:
    BLEMeshHandler() : concurrency::OSThread("BLEMesh") {}
    virtual ~BLEMeshHandler() {}

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void onBluetoothReady() {}

    bool isEnabled() const override
    {
        return config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_BROADCAST;
    }

    /// Packets refused for not fitting one advertisement. The ceiling is the whole encoded
    /// MeshPacket: BLE_MESH_MAX_PROTO_LEN for a relay, BLE_MESH_MAX_ORIGIN_PROTO_LEN for our own.
    uint32_t txDroppedTooLarge = 0;

    /// Frames lost to a full TX queue, refused or displaced. txDroppedTooLarge means the bearer
    /// cannot carry the packet at all; this means not right now.
    uint32_t txDroppedQueueFull = 0;

    /// Called from Router::send(). Encodes and queues; never transmits inline.
    bool onSend(const meshtastic_MeshPacket *mp) override;

    /// Drop our queued copy of (from, id) because a neighbour was heard rebroadcasting it on BLE.
    /// Ignores every other medium: an overhear is evidence about one radio only.
    bool onCancelSending(meshtastic_MeshPacket_TransportMechanism medium, NodeNum from, PacketId id) override;

  protected:
    /// One queued outbound frame, built into a complete AD payload. `from`/`id` let a cancel match
    /// without decoding the queue; `priority` is here because stripForTransmit() keeps it off the air.
    struct AdvSlot {
        std::array<uint8_t, BLE_MESH_ADV_TOTAL_MAX> data;
        uint8_t len;
        NodeNum from;
        PacketId id;
        uint8_t priority;
    };

    // --- platform hooks ---------------------------------------------------------------------
    /// Hand `adv`/`len` to the controller and begin a bounded burst. Must not block.
    virtual bool platformBeginAdvertising(const uint8_t *adv, size_t len) = 0;
    /// True while the burst started by platformBeginAdvertising is still running.
    virtual bool platformAdvertisingActive() = 0;
    /// Tear the burst down and hand the radio back to scanning / phone advertising.
    virtual void platformEndAdvertising() = 0;
    /// True once the stack is up and it is safe to touch the GAP API. Must query the BLE stack
    /// itself, NOT a flag set by onBluetoothReady(): either may be constructed first.
    virtual bool platformReady() = 0;

    int32_t runOnce() override;

    size_t highestPrioritySlot() const;
    size_t lowestPrioritySlot() const;
    void removeSlot(size_t index);

    /// Decode a received advertisement payload and enqueue it into the router.
    void deliverToRouter(const uint8_t *data, size_t len, int8_t rssi);

    /// Find the mesh manufacturer-data structure in a whole advertisement and deliver its packet.
    void handleAdvertisementData(const uint8_t *data, size_t len, int8_t rssi);

    /// Hand an accepted packet on. Virtual only so the native tests can observe what survives the
    /// ingress guards without standing up a live Router; production always takes the default.
    virtual void enqueueReceived(meshtastic_MeshPacket *p);

    /// Build the complete AD payload (flags + manufacturer data) for `mp`. Returns 0 on refusal.
    uint8_t buildAdvPayload(const meshtastic_MeshPacket *mp, uint8_t *out, size_t outCap);

    bool isRunning = false;

  private:
    // No lock. Both ends run on the main task: onSend() is reached from Router::send(), runOnce() is
    // an OSThread on the same task, and the BLE callbacks only ever reach deliverToRouter(), which
    // touches the packet pool and the router's FreeRTOS queue, never this array. <mutex>/<atomic>
    // also pull in <chrono>, which does not survive Arduino's round()/abs() macros on the nRF52
    // arm-none-eabi toolchain.
    // A bag, not a ring: frames leave in priority order, so runOnce() picks the best slot and closes
    // the gap rather than advancing a head.
    std::array<AdvSlot, BLE_MESH_TX_QUEUE_SIZE> txQueue{};
    size_t txCount = 0;
    bool advertising = false;

    // runOnce() pops before advertising, so a frame on air is no longer queued; its identity lives
    // here so a cancel can end a live burst.
    NodeNum advertisingFrom = 0;
    PacketId advertisingId = 0;

    // setBluetoothEnable() can bring the stack up before main() constructs this handler, so a
    // one-shot readiness callback would race. runOnce() polls platformReady() and calls
    // onBluetoothReady() itself, exactly once.
    bool readyHandled = false;
};

extern BLEMeshHandler *bleMeshHandler;

#endif // HAS_BLE_MESH
