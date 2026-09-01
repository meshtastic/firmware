#pragma once

#if HAS_BLE_MESH

#include "MeshTypes.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Router.h"
#include "concurrency/OSThread.h"
#include "mesh-pb-constants.h"

#include <array>

// Meshtastic BLE mesh manufacturer data identifier.
// 0xFFFF is the SIG-reserved "internal/test" company ID. A shipping build needs either a member
// company ID or - better, because iOS can only scan in the background when filtering by service
// UUID - an assigned 16-bit service UUID with the payload as service data.
#define BLE_MESH_COMPANY_ID 0xFFFF
#define BLE_MESH_PROTOCOL_VERSION 1

// A single unfragmented extended advertising payload is capped at 251 bytes, not the 254 an
// AUX_ADV_IND could hold: the HCI LE Set Extended Advertising Data command spends four of its 255
// parameter bytes on handle, operation, fragment preference and length. Each platform static_asserts
// this against its own stack's constant (NimBLE's BLE_HCI_MAX_EXT_ADV_DATA_LEN, the SoftDevice's
// BLE_GAP_ADV_SET_DATA_SIZE_EXTENDED_MAX_SUPPORTED).
#define BLE_MESH_ADV_TOTAL_MAX 251

// Flags AD structure (3) + manufacturer-data AD header (2) + company ID (2) + version (1).
#define BLE_MESH_ADV_OVERHEAD 8
#define BLE_MESH_MAX_PROTO_LEN (BLE_MESH_ADV_TOTAL_MAX - BLE_MESH_ADV_OVERHEAD)

// Outbound frames waiting for the advertiser. Extended advertising is set-and-repeat, not a packet
// queue - the instance holds one payload and repeats it - so a burst has to be clocked through one
// frame at a time.
#ifndef BLE_MESH_TX_QUEUE_SIZE
#define BLE_MESH_TX_QUEUE_SIZE 8
#endif

// Repeats per queued frame, standing in for the natural redundancy LoRa gets from its own retries.
#ifndef BLE_MESH_ADV_EVENTS
#define BLE_MESH_ADV_EVENTS 3
#endif

/**
 * Carries mesh frames between nodes over connectionless BLE extended advertisements.
 *
 * A second broadcast transport alongside LoRa, wired the way UdpMulticastHandler is: ingress hands
 * decoded frames to Router::enqueueReceivedMessage, egress is a copy taken in Router::send. It is
 * never the only path to the mesh - Router::send still asserts a LoRa iface.
 *
 * Connectionless, not GATT. GATT is point-to-point: reaching N peers costs N writes and no peer
 * overhears another, where one advertisement reaches every neighbour at once - the same one-to-many
 * shape LoRa has. Note this does not yet buy dupe suppression: FloodingRouter::perhapsCancelDupe is
 * gated on TRANSPORT_LORA and Router::cancelSending reaches only iface's TX queue, never this ring.
 * Advertising keeps suppression possible later; it is not active today.
 *
 * onSend() only encodes and queues. The advertising itself is clocked by runOnce() on the main
 * thread, because Router::send() is not a place to block: an implementation that advertises
 * synchronously stalls the router - and therefore LoRa timing and the whole main loop - for the
 * length of every burst.
 */
class BLEMeshHandler : private concurrency::OSThread
{
  public:
    BLEMeshHandler() : concurrency::OSThread("BLEMesh") {}
    virtual ~BLEMeshHandler() {}

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void onBluetoothReady() {}

    /// Called from Router::send(). Encodes and queues; never transmits inline.
    bool onSend(const meshtastic_MeshPacket *mp);

  protected:
    /// One queued outbound frame, already built into a complete AD payload.
    struct AdvSlot {
        std::array<uint8_t, BLE_MESH_ADV_TOTAL_MAX> data;
        uint8_t len;
    };

    // --- platform hooks ---------------------------------------------------------------------
    /// Hand `adv`/`len` to the controller and begin a bounded burst. Must not block.
    virtual bool platformBeginAdvertising(const uint8_t *adv, size_t len) = 0;
    /// True while the burst started by platformBeginAdvertising is still running.
    virtual bool platformAdvertisingActive() = 0;
    /// Tear the burst down and hand the radio back to scanning / phone advertising.
    virtual void platformEndAdvertising() = 0;
    /// True once the stack is up and it is safe to touch the GAP API.
    virtual bool platformReady() = 0;

    int32_t runOnce() override;

    /// Decode a received advertisement payload and enqueue it into the router.
    void deliverToRouter(const uint8_t *data, size_t len, int8_t rssi);

    /// Build the complete AD payload (flags + manufacturer data) for `mp`. Returns 0 on refusal.
    uint8_t buildAdvPayload(const meshtastic_MeshPacket *mp, uint8_t *out, size_t outCap);

    bool isRunning = false;

  private:
    // No lock. Both ends of this ring run on the main task: onSend() is reached from Router::send(),
    // and runOnce() is an OSThread on the same task. The BLE callbacks (NimBLE host task on ESP32,
    // SoftDevice on nRF52) only ever reach deliverToRouter(), which touches the packet pool and the
    // router's FreeRTOS queue - both explicitly safe from other contexts - and never this ring.
    //
    // <mutex>/<atomic> are also actively harmful here: they pull in <chrono>, which does not survive
    // Arduino's round()/abs() macros on the nRF52 arm-none-eabi toolchain.
    std::array<AdvSlot, BLE_MESH_TX_QUEUE_SIZE> txQueue{};
    size_t txHead = 0;
    size_t txTail = 0;
    size_t txCount = 0;
    bool advertising = false;
};

extern BLEMeshHandler *bleMeshHandler;

#endif // HAS_BLE_MESH
