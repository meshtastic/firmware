#pragma once

#if HAS_BLE_GATT_MESH

#include "MeshTransportBase.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "Router.h"
#include "concurrency/OSThread.h"
#include "mesh-pb-constants.h"

#include <array>

// The mesh-peer service a phone connects to. Private UUIDs shared with the client library
// (node-transport-ble-gatt); deliberately not the phone-API service, which a stock app dials.
#define BLE_GATT_MESH_SERVICE_UUID "4d657368-4e6f-6465-4741-545400000001"
#define BLE_GATT_MESH_CHARACTERISTIC_UUID "4d657368-4e6f-6465-4741-545400000002"

// One write or notify carries one fragment: [version][id lo][id hi][index][total][payload...].
// The id only separates packets in flight from the same peer; the link already tags chunks by peer.
#define BLE_GATT_MESH_FRAG_VERSION 1
#define BLE_GATT_MESH_FRAG_HEADER 5
#define BLE_GATT_MESH_MAX_FRAGMENTS 255

// ATT spends 3 bytes of the negotiated MTU on its own header. 23 is the MTU every peer supports,
// 512 is the largest attribute value NimBLE will carry.
#define BLE_GATT_MESH_MIN_CHUNK 20
#define BLE_GATT_MESH_MAX_CHUNK 512

// Reassembly is bounded because the input is attacker-controlled: anyone in range can connect to an
// open service and start writing. A fragment that never completes costs memory until it expires.
#ifndef BLE_GATT_MESH_MAX_PEERS
#define BLE_GATT_MESH_MAX_PEERS 2
#endif
#ifndef BLE_GATT_MESH_MAX_IN_FLIGHT_PER_PEER
#define BLE_GATT_MESH_MAX_IN_FLIGHT_PER_PEER 2
#endif
#ifndef BLE_GATT_MESH_REASSEMBLY_EXPIRY_MS
#define BLE_GATT_MESH_REASSEMBLY_EXPIRY_MS 15000
#endif

// Whole outbound packets waiting for the pump; each is fragmented per peer as it goes out.
#ifndef BLE_GATT_MESH_TX_QUEUE_SIZE
#define BLE_GATT_MESH_TX_QUEUE_SIZE 4
#endif

// A refused notify means the host is out of buffers, not that the peer is gone; retry with the
// pump's 10 ms tick, bounded so a departed peer fails the send instead of stalling the ring.
#ifndef BLE_GATT_MESH_TX_ATTEMPTS
#define BLE_GATT_MESH_TX_ATTEMPTS 50

// A dialled link that has carried nothing for this long is probed with a write the peer must answer.
// The central keeps the ACL alive by itself, so a peer whose app died looks exactly like a quiet one
// until something is asked of it - and the one central slot is lost until reboot otherwise.
#ifndef BLE_GATT_MESH_PROBE_IDLE_MS
#define BLE_GATT_MESH_PROBE_IDLE_MS 30000
#endif
// One unanswered probe may be the stack refusing a second GATTC procedure (an MTU exchange in flight,
// a write queue full); the retry after this long is what tells that apart from a peer that is gone.
#ifndef BLE_GATT_MESH_PROBE_RETRY_MS
#define BLE_GATT_MESH_PROBE_RETRY_MS 2000
#endif
// How long a started probe may stay unanswered before it counts as a miss. A backgrounded iPhone on
// a long connection interval answers later than Bluefruit's own 100 ms write timeout but inside this.
#ifndef BLE_GATT_MESH_PROBE_WAIT_MS
#define BLE_GATT_MESH_PROBE_WAIT_MS 1500
#endif
#endif

// (from, id) -> arrival peer, so a relay is never written back to the peer that delivered it.
#define BLE_GATT_MESH_RECENT_ARRIVALS 8

// Link-level control, shared with the client library (GattControl.kt): a chunk that starts with the
// marker is the link's own business, never a fragment. HELLO carries an id that stands for this node
// on GATT links and nowhere else - random per boot, deliberately not the node number.
#define BLE_GATT_MESH_CTRL_MARKER 0xC0
#define BLE_GATT_MESH_CTRL_HELLO 0x01
#define BLE_GATT_MESH_LINK_ID_SIZE 8
#define BLE_GATT_MESH_HELLO_SIZE (2 + BLE_GATT_MESH_LINK_ID_SIZE)

/// A connection handle. Stable for the life of a connection; not an identity.
typedef uint16_t BLEGattPeerId;
#define BLE_GATT_MESH_NO_PEER 0xFFFF

struct BLEGattMeshPeer {
    BLEGattPeerId id;
    uint16_t chunk;        // negotiated ATT MTU - 3
    bool outbound = false; // this node dialled it, so nothing but this node will ever notice it die
};

/**
 * Carries mesh frames between this node and phones connected over a BLE GATT link, the SIG Mesh
 * "GATT proxy" role. The node is the GATT server; each phone is a central that writes fragments to
 * the mesh characteristic and subscribes to it. ESP32 never dials out; the nRF52 build also dials a
 * phone whose advertisement shows the mesh-peer service (NRF52BLEGattMesh).
 *
 * Point-to-point where LoRa and the advertisement transport are one-to-many: a broadcast here is N
 * notifies to N peers. Egress skips the peer a relayed packet arrived from. Framing, reassembly
 * bounds and ingress sanitising live here in platform-neutral code so the native suite covers them;
 * the platform half only moves opaque chunks.
 *
 * Both ends of every ring run on the main task: onSend() is reached from Router::send(), runOnce()
 * is an OSThread on the same task, and platformPollInbound() is called from runOnce(). The BLE
 * stack's own task never touches this class.
 */
class BLEGattMeshHandler : private concurrency::OSThread, public MeshTransportBase
{
  public:
    BLEGattMeshHandler() : concurrency::OSThread("BLEGattMesh") {}
    virtual ~BLEGattMeshHandler() {}

    virtual void start() = 0;
    virtual void stop() = 0;

    bool isEnabled() const override
    {
        return config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER;
    }

    /// Called from Router::send(). Encodes and queues; never notifies inline.
    bool onSend(const meshtastic_MeshPacket *mp) override;

    /// Schedule the pump now. Safe from the BLE stack's task (mirrors BluetoothPhoneAPI).
    void wake();

    struct FragmentHeader {
        uint16_t id;
        uint8_t index;
        uint8_t total;
    };
    /// Read a fragment header; false if it is not ours or is self-contradictory.
    static bool parseFragment(const uint8_t *chunk, size_t len, FragmentHeader &hdr);
    /// Fragments a packet of packetLen needs at this chunk size; 0 when it cannot be carried.
    static uint8_t fragmentCount(size_t packetLen, uint16_t chunk);
    /// Write fragment `index` of `packet` into out. Returns the fragment length, 0 on refusal.
    static size_t buildFragment(const uint8_t *packet, size_t packetLen, uint16_t fragId, uint8_t index, uint8_t total,
                                uint16_t chunk, uint8_t *out, size_t outCap);
    /// The id a HELLO names, into idOut (BLE_GATT_MESH_LINK_ID_SIZE bytes); false when chunk is not one.
    static bool parseHello(const uint8_t *chunk, size_t len, uint8_t *idOut);
    /// True for any control chunk, a HELLO or an opcode this build does not know.
    static bool isControl(const uint8_t *chunk, size_t len);

  protected:
    // --- platform hooks ---------------------------------------------------------------------
    /// True once the stack is up and the service is registered. Must query the stack itself.
    virtual bool platformReady() = 0;
    /// Copy the peers currently subscribed to the mesh characteristic into out; returns the count.
    virtual size_t platformPeers(BLEGattMeshPeer *out, size_t cap) = 0;
    /// Notify one fragment to one peer. False when the stack refused it; the pump retries.
    virtual bool platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len) = 0;
    /// Take one received chunk. A zero-length chunk means the peer disconnected. False when none waits.
    virtual bool platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len) = 0;
    /// One node named itself on two links and the election went against this node: drop `peer` if it is
    /// a link this node dialled. An inbound link is the peer's to shed, so the default keeps everything.
    virtual void platformShedOutbound(BLEGattPeerId peer) { (void)peer; }
    /// This node's link id, drawn on first use, and the greeting that carries it - the platform's probe
    /// sends the same frame, so both live here rather than below.
    void ensureLinkId();
    size_t buildHello(uint8_t *out, size_t cap);
    /// Greet every listed link once, and start its quiet window; then probe the dialled ones that have
    /// gone quiet. A test drives both with its own clock.
    void pumpGreet(uint32_t nowMs);
    void pumpLiveness(uint32_t nowMs);
    /// What became of the probe last started on a link.
    enum class ProbeAnswer : uint8_t { Pending, Answered, Refused };
    /// Ask `peer` for proof of life - a write it must acknowledge - and return at once; the answer is
    /// read by platformProbeAnswer on later pumps. False means it could not even be asked (the stack
    /// is busy, or the link is already gone), which counts as a miss. Never blocks: this runs on the
    /// main task, which also times LoRa transmissions. A platform that cannot ask says true and
    /// answers at once, and so never sheds a link for silence.
    virtual bool platformProbeStart(BLEGattPeerId peer)
    {
        (void)peer;
        return true;
    }
    virtual ProbeAnswer platformProbeAnswer(BLEGattPeerId peer)
    {
        (void)peer;
        return ProbeAnswer::Answered;
    }

    int32_t runOnce() override;

    /// Reassemble one chunk from `peer`, delivering the packet to the router when it completes.
    void handleChunk(BLEGattPeerId peer, const uint8_t *chunk, size_t len, uint32_t nowMs);

    /// Decode a reassembled packet, apply the ingress guards, and enqueue it into the router.
    void deliverToRouter(BLEGattPeerId peer, const uint8_t *data, size_t len);

    /// Hand an accepted packet on. Virtual only so the native tests can observe what survives the
    /// ingress guards without a live Router; production always takes the default.
    virtual void enqueueReceived(meshtastic_MeshPacket *p);

    /// Assemblies still waiting on fragments. Exposed so the bounds are observable in tests.
    size_t pendingAssemblies() const;

    bool isRunning = false;

  private:
    struct Assembly {
        bool used;
        BLEGattPeerId peer;
        uint16_t id;
        uint8_t total;
        uint8_t received;
        uint16_t bytes;
        uint32_t startedMs;
        std::array<uint8_t, meshtastic_MeshPacket_size> data;
    };
    std::array<Assembly, BLE_GATT_MESH_MAX_PEERS * BLE_GATT_MESH_MAX_IN_FLIGHT_PER_PEER> assemblies{};

    /// Offer one chunk; returns the whole packet's length in out once its last fragment lands, else 0.
    size_t reassemble(BLEGattPeerId peer, const uint8_t *chunk, size_t len, uint32_t nowMs, uint8_t *out, size_t outCap);
    Assembly *findAssembly(BLEGattPeerId peer, uint16_t id);
    Assembly *newAssembly(BLEGattPeerId peer, uint16_t id, uint8_t total, uint32_t nowMs);
    void forgetPeer(BLEGattPeerId peer);
    void expireAssemblies(uint32_t nowMs);

    struct Arrival {
        NodeNum from;
        PacketId id;
        BLEGattPeerId peer;
        // Whether this peer originated the packet rather than relaying it: hop_start == hop_limit on
        // arrival, read before the Router decrements. Decides whether the relay may go back to it.
        bool fromOrigin;
    };
    std::array<Arrival, BLE_GATT_MESH_RECENT_ARRIVALS> arrivals{};
    size_t arrivalNext = 0;
    void rememberArrival(NodeNum from, PacketId id, BLEGattPeerId peer, bool fromOrigin);

    // This node's link id, and what each peer has said. A greeting goes out once per link, retried
    // while the stack refuses it; a HELLO in names the peer, and the same name on two links is one
    // node reached both ways - the lower id is the central, the loser sheds the link it dialled.
    uint8_t linkId[BLE_GATT_MESH_LINK_ID_SIZE] = {};
    bool linkIdReady = false;
    struct Greeting {
        bool used;
        BLEGattPeerId peer;
        bool sent;
        bool heard;
        uint8_t id[BLE_GATT_MESH_LINK_ID_SIZE];
        uint32_t heardMs;    // last chunk of any kind from this peer, or when the link was first listed
        uint32_t probedMs;   // last proof-of-life asked of it
        uint8_t probeMisses; // unanswered probes in a row; two is gone, one may be a busy stack
        bool probing;        // a probe is out and its answer not yet read
    };
    std::array<Greeting, BLE_GATT_MESH_MAX_PEERS * 2> greetings{};
    Greeting *greeting(BLEGattPeerId peer, bool create);
    void handleHello(BLEGattPeerId peer, const uint8_t *id);
    BLEGattPeerId arrivalPeer(NodeNum from, PacketId id) const;
    // The peer a relay of this packet must skip, which is none when the arrival came straight from
    // its originator - that echo is the originator's only implicit ack on a point-to-point bearer.
    BLEGattPeerId relayExclusion(NodeNum from, PacketId id) const;

    struct TxSlot {
        std::array<uint8_t, meshtastic_MeshPacket_size> data;
        uint16_t len;
        uint16_t fragId;
        BLEGattPeerId exclude;
    };
    std::array<TxSlot, BLE_GATT_MESH_TX_QUEUE_SIZE> txQueue{};
    size_t txHead = 0;
    size_t txTail = 0;
    size_t txCount = 0;

    // The send in progress: the peer snapshot taken when it started, and the position within it.
    bool txActive = false;
    std::array<BLEGattMeshPeer, BLE_GATT_MESH_MAX_PEERS> txPeers{};
    size_t txPeerCount = 0;
    size_t txPeerIdx = 0;
    uint8_t txFragIdx = 0;
    uint8_t txAttempts = 0;
    uint16_t nextFragId = 0;

    void pumpRx(uint32_t nowMs);
    /// Advance the send in progress. True while there is more to do.
    bool pumpTx();
};

extern BLEGattMeshHandler *bleGattMeshHandler;

#endif // HAS_BLE_GATT_MESH
