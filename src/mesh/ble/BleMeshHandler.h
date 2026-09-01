#pragma once
#if HAS_BLE_MESH

#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/MeshTypes.h"
#include "mesh/RadioInterface.h"

#include <array>
#include <atomic>
#include <mutex>

// AD manufacturer-data wrapper: [len][0xFF][company id LE][proto version][LoRa frame].
// 0xFFFF is the SIG-reserved "internal/test" company identifier. A shipping build needs either a
// member company ID or an assigned 16-bit service UUID - see the spike write-up.
#define BLE_MESH_COMPANY_ID 0xFFFF
#define BLE_MESH_PROTO_VERSION 1
#define BLE_MESH_AD_OVERHEAD 5 // len + type + 2-byte company id + version

// A single unfragmented advertising payload is capped at BLE_HCI_MAX_EXT_ADV_DATA_LEN (251), not
// the 254 an AUX_ADV_IND could theoretically hold: the HCI LE Set Extended Advertising Data command
// spends four of its 255 parameter bytes on handle, operation, fragment preference and length. The
// static_assert in the .cpp pins this to NimBLE's own constant so it cannot drift.
//
// Chaining past one PDU is possible (ESP32 allows up to 1650) but the receive side pays for it:
// ble_gap_ext_disc_desc.length_data is a uint8_t, so a chained advertisement arrives as several
// reports flagged INCOMPLETE and has to be reassembled per advertiser. Not worth it here -
// deliberately capped at one PDU, and frames that do not fit are dropped loudly rather than
// silently truncated.
//
// That cap costs the top of the payload range: 251 minus 5 bytes of AD wrapper minus the 16-byte
// PacketHeader leaves 230 bytes of ciphertext, against a DATA_PAYLOAD_LEN of 237. So the largest
// handful of packets cannot ride BLE. They still go out over LoRa - this transport is an additional
// copy path, never the only one.
#define BLE_MESH_SINGLE_PDU_BUDGET 251
#define BLE_MESH_MAX_ADV_DATA BLE_MESH_SINGLE_PDU_BUDGET
#define BLE_MESH_MAX_FRAME_LEN (BLE_MESH_SINGLE_PDU_BUDGET - BLE_MESH_AD_OVERHEAD)

#define BLE_MESH_TX_QUEUE_SIZE 8
#define BLE_MESH_ADV_INSTANCE 1  // instance 0 stays with the PhoneAPI connectable advertisement
#define BLE_MESH_ADV_EVENTS 3    // repeats per queued frame, standing in for LoRa's natural redundancy
#define BLE_MESH_ADV_MIN_ITVL 32 // 20 ms in 0.625 ms units
#define BLE_MESH_ADV_MAX_ITVL 48 // 30 ms

/**
 * Carries mesh frames between nodes over connectionless BLE 5 extended advertisements.
 *
 * This is a second broadcast transport alongside LoRa, wired the same way UdpMulticastHandler is:
 * ingress hands decoded frames to Router::enqueueReceivedMessage, egress is a copy taken in
 * Router::send. It is never the only path to the mesh - Router::send still asserts a LoRa iface.
 *
 * Connectionless, not GATT, on purpose. GATT is point-to-point: reaching N peers costs N writes and
 * no peer overhears another, where one advertisement reaches every neighbour at once - the same
 * one-to-many shape LoRa has. Note this does not yet buy dupe suppression: FloodingRouter's
 * perhapsCancelDupe is gated on TRANSPORT_LORA and Router::cancelSending reaches only iface's TX
 * queue, not the ring below. Advertising keeps that possible later; it is not active today.
 *
 * Frames go out in LoRa wire format (PacketHeader + ciphertext), not as an encoded MeshPacket the
 * way UDP does it. Two reasons: it is smaller, which matters against a 251-byte advertising budget
 * where UDP has a 1500-byte MTU; and a BLE-heard frame is then byte-identical to a LoRa-heard one,
 * so a future BLE<->LoRa bridge is a memcpy rather than a translation.
 */
class BleMeshHandler : private concurrency::OSThread
{
  public:
    BleMeshHandler();

    /// Configure the advertising instance and start the scanner. Idempotent.
    void start();
    void stop();

    /// Queue a frame for broadcast. Mirrors UdpMulticastHandler::onSend - returns false when the
    /// transport is not carrying traffic, so the caller can tell "dropped" from "not running".
    bool onSend(const meshtastic_MeshPacket *mp);

    /// Called from the NimBLE host task for every extended advertising report.
    void onScanReport(const uint8_t *data, uint8_t len, int8_t rssi);

  protected:
    int32_t runOnce() override;

  private:
    /// One queued outbound frame, already serialised into the advertising payload.
    struct AdvSlot {
        std::array<uint8_t, BLE_MESH_MAX_ADV_DATA> data;
        uint8_t len;
    };

    bool configureAdvInstance();
    bool startScanning();
    /// Push `slot` into the advertising instance and start a bounded burst.
    bool beginAdvertising(const AdvSlot &slot);
    void stopAdvertising();

    /// Serialise `mp` into the LoRa wire frame wrapped in an AD manufacturer-data structure.
    /// Returns 0 if the packet cannot be represented.
    uint8_t encodeAdvPayload(const meshtastic_MeshPacket *mp, uint8_t *out, size_t outCap);

    bool isRunning = false;
    bool isAdvertising = false;
    uint32_t advStartedAtMsec = 0;

    // TX ring. Extended advertising is a set-and-repeat model, not a packet queue: the radio
    // repeats whatever payload the instance currently holds. So a burst of mesh traffic has to be
    // clocked through the single instance one frame at a time, which is what runOnce does.
    std::mutex txMutex;
    std::array<AdvSlot, BLE_MESH_TX_QUEUE_SIZE> txQueue{};
    size_t txHead = 0;
    size_t txTail = 0;
    std::atomic<size_t> txCount{0};
};

extern BleMeshHandler *bleMeshHandler;

#endif // HAS_BLE_MESH
