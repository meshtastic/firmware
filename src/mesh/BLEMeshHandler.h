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

// One unfragmented extended advertising payload caps at 251 bytes, not the 254 an AUX_ADV_IND
// holds: HCI LE Set Extended Advertising Data spends four of its 255 parameter bytes on handle,
// operation, fragment preference and length. Each platform static_asserts this against its own
// stack's constant.
#define BLE_MESH_ADV_TOTAL_MAX 251

// Flags AD structure (3) + manufacturer-data AD header (2) + company ID (2) + version (1).
#define BLE_MESH_ADV_OVERHEAD 8
#define BLE_MESH_MAX_PROTO_LEN (BLE_MESH_ADV_TOTAL_MAX - BLE_MESH_ADV_OVERHEAD)

// Outbound frames waiting for the advertiser. Extended advertising is set-and-repeat, not a packet
// queue, so a burst has to be clocked through one frame at a time.
#ifndef BLE_MESH_TX_QUEUE_SIZE
#define BLE_MESH_TX_QUEUE_SIZE 8
#endif

// Repeats per queued frame, standing in for the redundancy LoRa gets from its own retries.
#ifndef BLE_MESH_ADV_EVENTS
#define BLE_MESH_ADV_EVENTS 3
#endif

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

    /// Packets refused for not fitting one unfragmented advertisement. The ceiling is
    /// BLE_MESH_MAX_PROTO_LEN for the whole encoded MeshPacket, below LoRa's MAX_RADIO_PAYLOAD_LEN.
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
    /// without decoding the queue; `priority` is here because strippedForAir() keeps it off the air.
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
