#pragma once
// Compiled into every Linux meshtasticd build: missing sqlite3 is a build error, never a silent omission.
#if ARCH_PORTDUINO && !ARCH_PORTDUINO_WASM && !MESHTASTIC_EXCLUDE_SFPP
#if !__has_include("sqlite3.h")
#error                                                                                                                           \
    "SF++ needs the sqlite3 headers: install libsqlite3-dev (Debian), sqlite-devel (Fedora) or sqlite-dev (Alpine), or build with -DMESHTASTIC_EXCLUDE_SFPP=1"
#endif
#define SFPP_ENABLED 1
#include "Channels.h"
#include "ProtobufModule.h"
#include "Router.h"
#include "SfppFrame.h"
#include "SfppStore.h"
#include "SinglePortModule.h"
#include "SketchIndex.h"
#include "SketchWire.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"

#include <deque>
#include <string>

/**
 * Store and forward ++ module (content model).
 *
 * The archive stores texts under the root derived from the channel. Each object is filed in the
 * content bucket named by the first byte of its message hash; a bucket is summarised by its short
 * IDs, their XOR checksum and their count.
 *
 * A node advertises a bucket with a scope and summary, fetches the difference from a peer, and
 * stores the missing objects. The store is content-addressed: there is no chain, tip or authority.
 * Only text messages on the primary channel are archived.
 */
class StoreForwardPlusPlusModule : public ProtobufModule<meshtastic_StoreForwardPlusPlus>, private concurrency::OSThread
{
  public:
#ifdef PIO_UNIT_TESTING
    void testPromoteScratchMatches(const uint8_t *root, uint32_t bucket, std::vector<pinsketch::Element> &ids)
    {
        promoteScratchMatches(root, bucket, ids);
    }
    SfppStore &testStore() { return store_; }
#endif

    // Stamped into PRAGMA user_version. A store with any other value is moved aside, never migrated.
    static constexpr int kSchemaVersion = SfppStore::kSchemaVersion;

    /** Constructor
     *
     */
    StoreForwardPlusPlusModule();

    /*
      -Override the wantPacket method.
    */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
            p->decoded.portnum == (portduino_config.sfpp_steal_port ? meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP
                                                                    : meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP)) {
            return true;
        }
        // The legacy Store & Forward module owns portnum 65 whenever it is acting as a server.
        if (p->decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP)
            return !legacyStoreForwardServes();
        return false;
    }

    void handleEncrypted(const meshtastic_MeshPacket *, const meshtastic_MeshPacket *);

  protected:
    /** Called to handle a particular incoming message
    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t) override;

    virtual int32_t runOnce() override;

    // Summarises one content bucket: its short IDs, their XOR checksum and their count.
    bool buildBucketSummary(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket, sfppsr::BucketSummary &out);

  private:
    using stored_object = SfppStore::stored_object;
    using RowTime = SfppStore::RowTime;
    using RowAge = SfppStore::RowAge;
    using IdentityHit = SfppStore::IdentityHit;

    SfppStore store_;

    bool linkIsFresh(const stored_object &lo) const;

    // Answers a legacy CLIENT_HISTORY from the stored texts. One text per runOnce() keeps the
    // archive inside the legacy module's pacing, which clients already expect.
    struct legacy_history_item {
        NodeNum to;
        NodeNum from;
        PacketId id;
        uint32_t rx_time;
        bool broadcast;
        std::string payload;
    };
    std::deque<legacy_history_item> legacyHistory;
    bool legacyHistoryBusy = false;

    // Portnum 65 belongs to the legacy module whenever it is a server; otherwise the shim serves it.
    bool legacyStoreForwardServes();

    void handleLegacyClientHistory(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *req);

    // Sends one queued legacy history text; returns false when the queue is empty.
    bool sendNextLegacyHistory();

    void sendLegacyReply(NodeNum to, const meshtastic_StoreAndForward &payload);

    // The module's single send boundary; EnumListenOnly drops every packet here.
    ErrorCode sendSfppPacket(meshtastic_MeshPacket *p, RxSource src, bool ccToPhone);

    // --- Arm C: detector + short-ID enumeration (StoreAndForward.Repair = enum) ---

    void advertiseOneBucket(uint8_t *root_hash, size_t root_hash_len);
    void advertiseWindow(uint8_t *root_hash, size_t root_hash_len);
    void rememberPeerScope(NodeNum peer, const uint8_t *root_hash, const sfppsr::AdvertScope &scope);
    void expireEnumExchange();
    bool sendEnumMessage(const meshtastic_StoreForwardPlusPlus &msg, NodeNum to, const char *type, size_t sketchBytes = 0,
                         const sfppsr::Scope *scopeOverride = nullptr);
    void logEnum(const char *dir, const char *type, const sfppsr::Scope &scope, size_t payloadBytes, NodeNum peer, int ids,
                 int frameIndex, int frameTotal, size_t sketchBytes = 0);
    void beginEnumExchange(const uint8_t *root_hash, uint32_t bucket, NodeNum peer, uint32_t peerCount, uint64_t peerChecksum);
    void endEnumExchange();
    bool bucketMatchesPeer(const uint8_t *root_hash, uint32_t bucket);
    bool enumBucketSuppressed(const uint8_t *root_hash, uint32_t bucket, uint32_t count, uint64_t checksum) const;
    void suppressEnumBucket(const uint8_t *root_hash, uint32_t bucket, uint32_t count, uint64_t checksum);

    void handleEnumAdvert(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t);
    bool trySketchExchange(const sfppsr::Advert &advert, sfppsr::BucketSummary &local, const uint8_t *root_hash, uint32_t bucket,
                           NodeNum peer);
    void fallbackToEnumList();
    void handleEnumRequest(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t);
    void handleEnumProvide(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t);
    void handleOverheardListing(NodeNum peer, const uint8_t *root_hash, const sfppsr::ShortIdList &list);
    bool takeHeardListing(const uint8_t *root_hash, uint32_t bucket, NodeNum &peer, std::vector<pinsketch::Element> &ids);
    void actOnListing(const uint8_t *root_hash, uint32_t bucket, NodeNum peer);
    void handleItemRequest(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t);
    void handleItemFrame(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t);
    void requestEnumList(const uint8_t *root_hash, uint32_t bucket, NodeNum peer);
    void requestMissingItems(const uint8_t *root_hash, uint32_t bucket, NodeNum peer, const std::vector<pinsketch::Element> &ids);
    bool itemInFlight(const uint8_t *root_hash, uint32_t bucket, pinsketch::Element id);
    void noteItemInFlight(const uint8_t *root_hash, uint32_t bucket, pinsketch::Element id);
    void forgetItemInFlight(pinsketch::Element id);

    // Item frames are queued per requester and drained from runOnce() round-robin, so a 32-object
    // request cannot overflow the radio's 16-entry TX queue and one requester cannot starve another.
    struct EnumOutgoing {
        meshtastic_StoreForwardPlusPlus msg;
        NodeNum to;
        sfppsr::Scope scope;
        const char *type;
        pinsketch::Element id = 0;
    };
    struct EnumRequester {
        NodeNum to = 0;
        uint32_t lastUsedMs = 0;
        uint32_t lastServeMs = 0; // last frame sent to this requester, for idle-slot reclaim
        uint32_t windowStartMs = 0;
        uint8_t sentInWindow = 0;
        std::deque<EnumOutgoing> queue;
    };
    void queueEnumFrames(const std::vector<meshtastic_StoreForwardPlusPlus> &frames, NodeNum requester, NodeNum dest,
                         const sfppsr::Scope &scope, const char *type, pinsketch::Element id = 0);
    void queueItemFrames(const std::vector<meshtastic_StoreForwardPlusPlus> &frames, NodeNum requester, NodeNum dest,
                         const sfppsr::Scope &scope, pinsketch::Element id = 0);
    bool enumRequesterHasQueued(NodeNum to, pinsketch::Element id) const;
    void noteRequesterActivity(NodeNum to);
    bool drainOneEnumFrame();
    bool consumeServeBudget();
    void pumpEnumRetries();
    void resendEnumList();
    void resendItemRequest();
    bool anyEnumQueuePending() const;
    void beginEnumLateAccept();
    void addPendingBucket(const uint8_t *root_hash, NodeNum authority, uint32_t bucket, uint32_t count, uint64_t checksum,
                          uint32_t tip);
    void startPendingPull();
    void rememberFailedPull();
    void repairPromotions();
    void expireEnumLate();

    // A scratch row whose short ID the peer also holds is already "held"; move it into
    // the bucket and drop it from the fetch list instead of re-fetching it.
    void promoteScratchMatches(const uint8_t *root_hash, uint32_t bucket, std::vector<pinsketch::Element> &ids);

    // Derives the channel's root from its name and key; no node number or clock is involved.
    bool deriveRootFromChannel(ChannelHash, uint8_t *);

    // Returns the derived root, replacing a stale mapping from an earlier root rule or channel.
    size_t getOrDeriveRootFromChannel(ChannelHash, uint8_t *);

    // Takes the decrypted MeshPacket and the encrypted packet copy, and builds a stored object.
    // Generates a message hash, but does not set the commit hash
    stored_object ingestTextPacket(const meshtastic_MeshPacket &, const meshtastic_MeshPacket *);

    uint32_t rebroadcastTimeout = 3600; // Messages older than this (in seconds) will not be rebroadcast
    bool doing_split_send = false;
    stored_object split_link_out;

    bool doing_split_receive = false;
    stored_object split_link_in;

    bool did_announce_last = false;

    uint32_t texts_rebroadcast = 0;
    uint32_t scratch_timed_out = 0;
    uint32_t sent_from_scratch = 0;
    uint32_t received_from_scratch = 0;

    // Enumeration (arm C) state. One exchange is in flight at a time; the next advert tick retries.
    sfppframe::Reassembler enumReassembler{16};
    uint32_t enumBucketCursor = 0;
    // Canonical text writes since the previous advert; the next advert's sketch capacity follows it.
    uint32_t enumWritesSinceAdvert = 0;

    bool enumExchangeActive = false;
    bool enumExchangeSucceeded = false;
    uint8_t enumExchangeRoot[SFPP_HASH_SIZE] = {0};
    uint32_t enumExchangeBucket = 0;
    NodeNum enumExchangePeer = 0;
    uint32_t enumExchangeStartedMs = 0;
    uint32_t enumRecovered = 0;
    uint32_t enumTx = 0;
    uint32_t enumRx = 0;
    uint32_t enumTxBytes = 0;
    uint32_t enumRxBytes = 0;

    bool enumProvideActive = false;
    uint32_t enumProvideBucket = 0;
    NodeNum enumProvidePeer = 0;
    uint8_t enumProvideTotal = 0;
    uint8_t enumProvideSeenCount = 0;
    bool enumProvideSeen[255] = {};
    std::vector<pinsketch::Element> enumProvideIds;

    std::vector<pinsketch::Element> enumWanted;
    size_t enumWantedRemaining = 0;
    struct EnumInFlight {
        pinsketch::Element id;
        uint8_t root[SFPP_HASH_SIZE];
        uint32_t bucket;
        uint32_t expiresMs;
    };
    std::deque<EnumInFlight> enumInFlight;

    // The peer summary this exchange is trying to reach, and the short-ID-collision probe state.
    uint32_t enumPeerCount = 0;
    uint64_t enumPeerChecksum = 0;
    bool enumCollisionProbe = false;
    bool enumSketchAttempt = false;
    uint32_t enumSketchIds = 0;
    uint32_t enumLastItemMs = 0;

    // A bucket whose short-ID lists match but whose checksum never does is not pulled again until
    // a peer changes what it advertises for it.
    struct EnumSuppression {
        uint8_t root[SFPP_HASH_SIZE];
        uint32_t bucket;
        uint32_t count;
        uint64_t checksum;
    };
    std::vector<EnumSuppression> enumSuppressed;

    // What each peer last declared about its store: schema, retention, window and size.
    struct EnumPeerScope {
        uint8_t root[SFPP_HASH_SIZE];
        NodeNum peer;
        sfppsr::AdvertScope scope;
        uint32_t heardMs;
    };
    std::vector<EnumPeerScope> enumPeerScopes;

    std::vector<EnumRequester> enumTxQueues;
    size_t enumQueueCursor = 0;
    uint32_t enumLastDrainMs = 0;
    std::deque<uint32_t> serveWindowSends; // rolling 10 min of served-frame timestamps

    uint32_t enumPeerTip = 0;
    struct EnumPendingBucket {
        uint32_t bucket;
        uint32_t count;
        uint64_t checksum;
        uint32_t tip;
        uint8_t root[SFPP_HASH_SIZE];
        NodeNum authority;
        uint32_t retryAfterMs = 0;
        uint32_t firstFailedMs = 0;
        uint8_t attempts = 0;
        bool summaryKnown = true; // false when only an overheard listing named the hole
    };
    std::vector<EnumPendingBucket> enumPending;
    struct EnumPromoted {
        pinsketch::Element id;
        uint8_t hash[SFPP_HASH_SIZE];
    };
    std::vector<EnumPromoted> enumPromoted;
    uint8_t enumSeenRoot[SFPP_HASH_SIZE] = {0};
    struct EnumBucketSeen {
        uint32_t bucket;
        uint32_t count;
        uint64_t checksum;
    };
    std::vector<EnumBucketSeen> enumAdvertSeen;

    // A broadcast listing overheard outside our own exchange stands in for our ENUM_REQUEST.
    struct EnumHeardListing {
        uint8_t root[SFPP_HASH_SIZE];
        uint32_t bucket;
        NodeNum peer;
        uint8_t total;
        uint8_t seenCount;
        bool seen[255];
        std::vector<pinsketch::Element> ids;
        uint32_t heardMs;
    };
    std::vector<EnumHeardListing> enumHeardListings;
    // False for a pull started from an overheard listing, which carries no count or checksum.
    bool enumPeerSummaryKnown = true;

    bool enumAwaitingList = false;
    uint8_t enumListRetries = 0;
    uint8_t enumItemRetries = 0;
    uint32_t enumRequestSentMs = 0;

    // Log a foreign full-root advert once per peer, not once per advert.
    NodeNum enumForeignRootLoggedPeer = 0;
    uint32_t enumAdvertTick = 0;

    // Frames still in flight after an item exchange gave up.
    bool enumLateActive = false;
    uint8_t enumLateRoot[SFPP_HASH_SIZE] = {0};
    uint32_t enumLateBucket = 0;
    NodeNum enumLatePeer = 0;
    std::vector<pinsketch::Element> enumLateWanted;
    uint32_t enumLateStartedMs = 0;
};

extern StoreForwardPlusPlusModule *storeForwardPlusPlusModule;
#endif