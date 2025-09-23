#pragma once
#include "SinglePortModule.h"
#include "concurrency/NotifiedWorkerThread.h"
#include <bitset>

#define REPLAY_FAKE_PACKET_LOSS_PERCENT 0 // Simulate this percentage of packet loss for testing

#define REPLAY_REMEMBER_MASK 0x3FF /*1024*/             // Mask for wrapping packet memory index
#define REPLAY_REMEMBER_SIZE (REPLAY_REMEMBER_MASK + 1) // Remember the most recent n received packets
#define REPLAY_BUFFER_MASK 0xFF /*256*/                 // Mask for wrapping buffer indices
#define REPLAY_BUFFER_SIZE (REPLAY_BUFFER_MASK + 1)     // Track at most this many packets
#define REPLAY_BUFFER_CACHE_MAX REPLAY_BUFFER_SIZE      // Cache at most this many packets
#define REPLAY_QUEUE_MASK 0x0F                          // Mask for wrapping the replay queue index
#define REPLAY_QUEUE_SIZE (REPLAY_QUEUE_MASK + 1)       // Size of the replay
#define REPLAY_STATS_MASK 0x7F                          // Mask for wrapping the stats index
#define REPLAY_STATS_SIZE (REPLAY_STATS_MASK + 1)       // Size of the stats array
#define REPLAY_FLUSH_PACKETS 16                         // Send an advertisement after at most this many packets
#define REPLAY_FLUSH_SECS 20 // Send an advertisement after at most this many seconds (if unadvertised packets are pending)
#define REPLAY_STARTUP_DELAY_SECS 30   // Wait this many seconds after boot before sending the first advertisement
#define REPLAY_ADVERT_MAX_PACKETS 64   // Advertise at most this many packets at a time
#define REPLAY_CHUTIL_THRESHOLD_PCT 35 // If chutil is >= this, only advertise high-priority packets
#define REPLAY_CHUTIL_PRIORITY meshtastic_MeshPacket_Priority_RELIABLE // Packets with priority >= this are high-priority
#define REPLAY_HEAP_THRESHOLD_PCT 10      // If we are using more than this much of the heap on cache, enable proactive pruning
#define REPLAY_HEAP_RESERVE_PCT 5         // Don't prune the cache to below this much of the heap
#define REPLAY_HEAP_FREE_MIN_PCT 10       // Prune packets if free heap is below this
#define REPLAY_HEAP_FREE_TARGET_PCT 15    // Prune packets until free heap is above this
#define REPLAY_SPACING_MS 1000            // Spacing between replayed packets (TODO: scale based on radio settings)
#define REPLAY_EXPIRED_SPACING_SECS 10    // Minimum spacing between advertisements of expired packets
#define REPLAY_SEQUENCE_MASK 0x1F         // Mask for wrapping advertisement sequence number
#define REPLAY_TRACK_SERVERS 8            // Keep track of state for this many servers
#define REPLAY_REQUEST_MAX_PACKETS 16     // Request at most this many packets at a time
#define REPLAY_REQUEST_MAX_OUTSTANDING 32 // Keep track of this many outstanding requested packets
#define REPLAY_REQUEST_TIMEOUT_SECS 45    // Consider a requested packet lost or unfilled after this many seconds
#define REPLAY_SERVER_STALE_SECS 300      // Consider a server stale if we haven't heard from it in this many seconds
#define REPLAY_CLIENT_BURST 16            // Allow at most this many replay requests per client in a burst
#define REPLAY_CLIENT_RATE_MS 1000        // Allow at most one replay request per client every this many milliseconds on average
#define REPLAY_CLIENT_SIZE 128            // Track at most this many clients
#define REPLAY_CLIENT_THROTTLE_ADVERT_MAX 64 // Advertise at most this many throttled clients at a time
#define REPLAY_STATS_INTERVAL_SECS 900       // Send statistics every n seconds

#define REPLAY_REQUEST_TYPE_ADVERTISEMENT 0 // Request an advertisement
#define REPLAY_REQUEST_TYPE_PACKETS 1       // Request a replay of the specified packets
#define REPLAY_REQUEST_TYPE_RESERVED_2 2    // Reserved for future use
#define REPLAY_REQUEST_TYPE_RESERVED_3 3    // Reserved for future use
#define REPLAY_ADVERT_TYPE_AVAILABLE 0      // Advertise available packets
#define REPLAY_ADVERT_TYPE_EXPIRED 1        // Advertise expired packets (i.e. cannot be replayed)
#define REPLAY_ADVERT_TYPE_STATISTICS 2     // Transmit statistics about the replay system
#define REPLAY_ADVERT_TYPE_RESERVED_3 3     // Reserved for future use

#define REPLAY_NOTIFY_ADOPT 1    // A packet has been adopted into the cache
#define REPLAY_NOTIFY_INTERVAL 2 // The interval timer fired
#define REPLAY_NOTIFY_REPLAY 3   // Trigger replay of wanted packets

#define REPLAY_HASH(a, b) ((((a ^ b) >> 16) & 0xFFFF) ^ ((a ^ b) & 0xFFFF))

typedef uint16_t ReplayHash;
typedef uint16_t ReplayMap;
typedef unsigned long ReplayCursor;

typedef struct ReplayWire {
    union {
        uint16_t bitfield = 0;
        struct {
            uint8_t type : 2;       // Request or advertisement type
            uint8_t priority : 1;   // Please only request / send high-priority packets
            uint8_t boot : 1;       // (adverts only) The sending node just booted
            uint8_t router : 1;     // The sending node is a router (prioritise following & replaying for)
            uint8_t aggregate : 1;  // (adverts only) This is an aggregate replay of prior adverts
            uint8_t throttle : 1;   // (adverts only) Lists clients that should not request replays in response to this advert
            uint8_t reserved_0 : 1; // Reserved for future use
            uint8_t sequence : 5;   // Incremented with each advertisement
            uint8_t reserved_1 : 3; // Reserved for future use
        };
    } header;
    /**
     * Advertisement payload is:
     *   - uint16 range map (which 16-packet ranges are included)
     *   - for each range:
     *     - uint16 packet bitmap (which packets in the range are included)
     *     - uint16 priority bitmap (which packets in the range are high priority)
     *     - uint16[] packet hashes
     *   - (aggregate only) uint16 aggregate mask (which adverts are included in this aggregate)
     *   - (throttle only) uint8[] list of clients that should not request replays in response to this advert
     *
     * Expired advertisement payload is:
     *   - uint16 range map (which 16-packet ranges are included)
     *   - for each included range:
     *     - uint16 packet bitmap (which packets in the range are expired)
     *
     * Request payload is:
     *   - uint16 range map (which 16-packet ranges are included)
     *   - for each included range:
     *     - uint16 packet bitmap (which packets in the range are requested)
     */

} ReplayWire;
static_assert(sizeof(ReplayWire::header) == sizeof(ReplayWire::header.bitfield));

typedef struct ReplayEntry {
    meshtastic_MeshPacket *p = NULL;
    uint32_t last_replay_millis = 0;
    uint16_t replay_count = 0;
    ReplayHash hash = 0;
} ReplayEntry;

typedef struct ReplayAdvertisement {
    unsigned int sequence = 0;
    ReplayCursor head = 0;
    ReplayCursor tail = 0;
    std::bitset<REPLAY_BUFFER_SIZE> dirty = {};
} ReplayAdvertisement;

typedef struct ReplayServerInfo {
    NodeNum id = 0;
    unsigned int discovered_millis = 0;
    unsigned long last_advert_millis = 0;
    unsigned int last_sequence = 0;
    unsigned int max_sequence = 0;
    unsigned long missing_sequence = 0;
    unsigned int replays_requested = 0;
    unsigned int adverts_received = 0;
    unsigned int packets_missed = 0;
    bool flag_priority = false;
    bool flag_router = false;
    bool is_tracked = false;
    ReplayHash packets[REPLAY_BUFFER_SIZE] = {};
    std::bitset<REPLAY_BUFFER_SIZE> available = {};
    std::bitset<REPLAY_BUFFER_SIZE> priority = {};
    std::bitset<REPLAY_BUFFER_SIZE> missing = {};
} ReplayServerInfo;

typedef struct ReplayClientInfo {
    NodeNum id = 0;
    unsigned long last_request_millis = 0;
    unsigned int bucket = REPLAY_CLIENT_BURST;
    unsigned int requests = 0;
} ReplayClientInfo;

typedef struct ReplayRequestInfo {
    ReplayHash hash = 0;
    unsigned long timeout_millis = 0;
} ReplayRequestInfo;

typedef struct ReplayStats {
    NodeNum id = 0;
    uint8_t adverts_from = 0;   // Number of adverts received from this node
    uint8_t expired_from = 0;   // Number of expiry adverts received from this node
    uint8_t missed_from = 0;    // Number of missed adverts & packets sent by this node
    uint8_t requests_from = 0;  // Number of requests received from this node
    uint8_t throttled_from = 0; // Number of times we were throttled by this node
    uint8_t requests_to = 0;    // Number of requests sent to this node
    uint8_t replays_for = 0;    // Number of packets replayed for this node
    union {
        uint8_t bitfield = 0;
        struct {
            uint8_t is_router : 1; // This node is a router
            uint8_t throttled : 1; // This node was throttled at some point within the stats window
            uint8_t priority : 1;  // This node indicated priority constraints at some point within the stats window
            uint8_t reserved : 5;  // Reserved for future use
        };
    };
} ReplayStats;

class ReplayBuffer
{
  public:
    ReplayBuffer(){};
    ReplayEntry *adopt(meshtastic_MeshPacket *p);
    unsigned int getLength() const { return next - last; };
    unsigned int getNumCached() const { return num_cached; };
    ReplayCursor getHeadCursor() const { return next ? next - 1 : 0; };
    ReplayCursor getTailCursor() const { return last; };
    ReplayEntry *get(unsigned int idx) { return &entries[idx & REPLAY_BUFFER_MASK]; };
    ReplayEntry *search(ReplayHash hash);
    ReplayEntry *search(NodeNum from, uint32_t id);
    ReplayEntry *search(meshtastic_MeshPacket *p, bool strict = false);

  private:
    unsigned int num_cached = 0;
    ReplayCursor next = 0;
    ReplayCursor last = 0;
    ReplayEntry entries[REPLAY_BUFFER_SIZE];
    MemoryDynamicReplayAware<meshtastic_MeshPacket> packets;
    void prune(unsigned int keep = REPLAY_BUFFER_CACHE_MAX); // Free up memory by releasing cached packets
    void truncate(unsigned int keep = REPLAY_BUFFER_SIZE);   // Discard all but the most recent n packets
};

class ReplayModule : public SinglePortModule, private concurrency::NotifiedWorkerThread
{
  public:
    ReplayModule() : SinglePortModule("replay", meshtastic_PortNum_REPLAY_APP), concurrency::NotifiedWorkerThread("replay") {}
    void adopt(meshtastic_MeshPacket *p);
    bool isKnown(ReplayHash hash);
    bool isKnown(meshtastic_MeshPacket *p) { return isKnown(REPLAY_HASH(p->from, p->id)); };
    void remember(ReplayHash hash) { memory[memory_next++ & REPLAY_REMEMBER_MASK] = hash; };
    void remember(meshtastic_MeshPacket *p) { remember(REPLAY_HASH(p->from, p->id)); };
    meshtastic_MeshPacket *queuePeek();
    meshtastic_MeshPacket *queuePop();
    unsigned int queueLength() { return queue_length; };

  private:
    ReplayBuffer buffer;
    ReplayCursor last_advert_cursor = 0;
    unsigned long last_advert_millis = 0;
    unsigned long last_expired_millis = 0;
    unsigned long last_stats_millis = 0;
    unsigned int packets_since_advert = 0;
    unsigned int next_sequence = 0;
    std::bitset<REPLAY_BUFFER_SIZE> dirty = {};
    std::bitset<REPLAY_BUFFER_SIZE> dirty_prio = {};
    std::bitset<REPLAY_BUFFER_SIZE> want_replay = {};
    ReplayHash memory[REPLAY_REMEMBER_SIZE] = {};
    ReplayAdvertisement advertisements[32] = {};
    ReplayServerInfo servers[REPLAY_TRACK_SERVERS] = {};
    ReplayClientInfo clients[REPLAY_CLIENT_SIZE] = {};
    ReplayRequestInfo requests[REPLAY_REQUEST_MAX_OUTSTANDING] = {};
    ReplayStats stats[REPLAY_STATS_SIZE] = {};
    ReplayCursor stats_next = 0;
    ReplayCursor memory_next = 1;
    ReplayCursor replay_from = 0;
    ReplayCursor queue[REPLAY_QUEUE_SIZE] = {};
    ReplayCursor queue_next = 0;
    ReplayCursor queue_tail = 0;
    ReplayCursor queue_length = 0;
    bool want_replay_prio = false;
    bool want_replay_expired = false;
    struct {
        unsigned int adverts_sent = 0;
        unsigned int adverts_sent_agg = 0;
        unsigned int adverts_sent_expired = 0;
        unsigned int packets_rebroadcast = 0;
        unsigned int packets_rebroadcast_prio = 0;
        unsigned int packets_replayed = 0;
        unsigned int packets_replayed_prio = 0;
        unsigned int packets_requested = 0;
        unsigned int packets_requested_prio = 0;
        unsigned int window_start_millis = 0;
    } metrics;
    ReplayClientInfo *client(NodeNum id);
    void advertise(bool aggregate = false, unsigned int from_sequence = 0, ReplayMap aggregate_mask = 0);
    void advertiseExpired();
    void replay();
    void requestReplay(ReplayServerInfo *server);
    void requestMissingAdvertisements(ReplayServerInfo *server);
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    void handleRequest(const meshtastic_MeshPacket *p);
    void handleAdvertisement(const meshtastic_MeshPacket *p);
    void handleAvailabilityAdvertisement(ReplayWire *wire, unsigned char *payload, unsigned char *payload_end,
                                         ReplayServerInfo *server);
    void handleExpiredAdvertisement(ReplayWire *wire, unsigned char *payload, unsigned char *payload_end,
                                    ReplayServerInfo *server);
    ReplayRequestInfo *requestInfo(ReplayHash hash);
    bool queuePush(ReplayCursor idx);
    void invalidateServer(ReplayServerInfo *server, bool stats = false);
    ReplayStats *getStats(NodeNum id);
    void resetStats();
    void sendStats();
    void printStats(meshtastic_ReplayStats *rs);
    void onNotify(uint32_t notification);
};

extern ReplayModule *replayModule;