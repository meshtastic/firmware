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
#include "SinglePortModule.h"
#include "SketchIndex.h"
#include "SketchWire.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include "sqlite3.h"

#include <deque>
#include <string>

#define SFPP_HASH_SIZE 16
#define SFPP_SHORT_HASH_SIZE 8

/**
 * Store and forward ++ module
 * There's an obvious need for a store-and-forward mechanism in Meshtastic.
 * This module takes heavy inspiration from Git, building a chain of messages that can be synced between nodes.
 * Each message is hashed, and the chain is built by hashing the previous commit hash and the current message hash.
 * Nodes can request missing messages by requesting the next message after a given commit hash.
 *
 * The current focus is text messages, limited to the primary channel.
 *
 * Each chain is identified by a root hash, which is derived from the channelHash, the local nodenum, and the timestamp when
 * created.
 *
 * Each message is also given a message hash, derived from the encrypted payload, the to, from, id.
 * Notably not the timestamp, as we want these to match across nodes, even if the timestamps differ.
 *
 * The authoritative node for the chain will generate a commit hash for each message when adding it to the chain.
 * The first message's commit hash is derived from the root hash and the message hash.
 * Subsequent messages' commit hashes are derived from the previous commit hash and the current message hash.
 * This allows a node to see only the last commit hash, and confirm it hasn't missed any messages.
 *
 * Nodes can request the next message in the chain by sending a LINK_REQUEST message with the root hash and the last known commit
 * hash. Any node that has the next message can respond with a LINK_PROVIDE message containing the next message.
 *
 * When a satellite node sees a new text message, it stores it in a scratch database.
 * These messages are periodically offered to the authoritative node for inclusion in the chain.
 *
 * The LINK_PROVIDE message does double-duty, sending both on-chain and off-chain messages.
 * The differentiator is whether the commit hash is set or left empty.
 *
 * When a satellite node receives a canonical link message, it checks if it has the message in scratch.
 * And evicts it when adding it to the canonical chain.
 *
 * This approach allows a node to know whether it has seen a given message before, or if it is new coming via SFPP.
 * If new, and the timestamp is within the rebroadcast timeout, it will process that message as if it were just received from the
 * mesh, allowing it to be decrypted, shown to the user, and rebroadcast.
 */
class StoreForwardPlusPlusModule : public ProtobufModule<meshtastic_StoreForwardPlusPlus>, private concurrency::OSThread
{
    struct link_object {
        uint32_t to;
        uint32_t from;
        uint32_t id;
        uint32_t rx_time = 0;
        ChannelHash channel_hash;
        uint8_t encrypted_bytes[256] = {0};
        size_t encrypted_len;
        uint8_t message_hash[SFPP_HASH_SIZE] = {0};
        size_t message_hash_len = 0;
        uint8_t root_hash[SFPP_HASH_SIZE] = {0};
        size_t root_hash_len = 0;
        uint8_t commit_hash[SFPP_HASH_SIZE] = {0};
        size_t commit_hash_len = 0;
        uint32_t counter = 0;
        std::string payload;
        bool validObject = true; // set this false when a chain calulation fails, etc.
    };

  public:
    // Stamped into PRAGMA user_version. A store with any other value is moved aside, never migrated.
    static constexpr int kSchemaVersion = 1;

    /** Constructor
     *
     */
    StoreForwardPlusPlusModule();
    ~StoreForwardPlusPlusModule();

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

  private:
    sqlite3 *ppDb;
    sqlite3_stmt *chain_insert_stmt;
    sqlite3_stmt *scratch_insert_stmt;
    sqlite3_stmt *checkDupMessageHash;
    sqlite3_stmt *checkDupCommitHash;
    sqlite3_stmt *checkScratch;
    sqlite3_stmt *removeScratch;
    sqlite3_stmt *updatePayloadStmt;
    sqlite3_stmt *getPayloadFromScratchStmt;
    sqlite3_stmt *fromScratchStmt;
    sqlite3_stmt *fromScratchByHashStmt;
    sqlite3_stmt *getNextHashStmt;
    sqlite3_stmt *getChainEndStmt;
    sqlite3_stmt *getLinkStmt;
    sqlite3_stmt *getLinkFromMessageHashStmt;
    sqlite3_stmt *getHashFromRootStmt;
    sqlite3_stmt *addRootToMappingsStmt;
    sqlite3_stmt *getRootFromChannelHashStmt;
    sqlite3_stmt *getFullRootHashStmt;
    sqlite3_stmt *setChainCountStmt;
    sqlite3_stmt *getChainCountStmt;
    sqlite3_stmt *getScratchCountStmt;
    sqlite3_stmt *getRootCanonScratchCountStmt;
    sqlite3_stmt *pruneScratchQueueStmt;
    sqlite3_stmt *trimOldestLinkStmt;
    sqlite3_stmt *maybeAddPeerStmt;
    sqlite3_stmt *getPeerStmt;
    sqlite3_stmt *updatePeerStmt;
    sqlite3_stmt *clearChainStmt;
    sqlite3_stmt *canon_scratch_insert_stmt;
    sqlite3_stmt *getCanonScratchCountStmt;
    sqlite3_stmt *getCanonScratchStmt;
    sqlite3_stmt *removeCanonScratch;
    sqlite3_stmt *clearCanonScratchStmt;
    sqlite3_stmt *bucketSummaryStmt;
    sqlite3_stmt *bucketIdsStmt;
    sqlite3_stmt *getLinkByShortIdStmt;
    sqlite3_stmt *unindexedLinksStmt;
    sqlite3_stmt *setLinkSetIndexStmt;
    sqlite3_stmt *legacyHistoryStmt;

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

    // Derives the short ID and checksum contribution for any chain row that predates those columns.
    // Bounded by the chain length, and a no-op on every run after the first.
    void backfillSetIndex();

    // Summarises one bucket of a chain: its members' short IDs, their XOR checksum and their count.
    // Returns false when the bucket holds nothing, which is not the same as an empty sketch.
    bool buildBucketSummary(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket, sfppsr::BucketSummary &out);

    // The short IDs stored in one bucket, for the enumeration list and for a windowed object lookup.
    bool collectBucketIds(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket, std::vector<pinsketch::Element> &out);

    // --- Arm C: detector + short-ID enumeration (StoreAndForward.Repair = enum) ---

    void advertiseOneBucket(uint8_t *root_hash, size_t root_hash_len);
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
    void handleItemRequest(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t);
    void handleItemFrame(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t);
    void requestEnumList(const uint8_t *root_hash, uint32_t bucket, NodeNum peer);
    void requestMissingItems(const uint8_t *root_hash, uint32_t bucket, NodeNum peer, const std::vector<pinsketch::Element> &ids);
    bool storeRecoveredObject(const sfppframe::Replay &replay, const uint8_t *root_hash, uint32_t bucket);

    // Item frames are queued per requester and drained from runOnce() round-robin, so a 32-object
    // request cannot overflow the radio's 16-entry TX queue and one requester cannot starve another.
    struct EnumOutgoing {
        meshtastic_StoreForwardPlusPlus msg;
        NodeNum to;
        sfppsr::Scope scope;
    };
    struct EnumRequester {
        NodeNum to = 0;
        std::deque<EnumOutgoing> queue;
    };
    void queueItemFrames(const std::vector<meshtastic_StoreForwardPlusPlus> &frames, NodeNum to, const sfppsr::Scope &scope);
    bool drainOneEnumFrame();
    void pumpEnumRetries();
    void resendEnumList();
    void resendItemRequest();
    bool anyEnumQueuePending() const;

    // For a given Meshtastic ChannelHash, fills the root_hash buffer with a 32-byte root hash
    // returns true if the root hash was found
    bool getRootFromChannelHash(ChannelHash, uint8_t *);

    // For a given root hash, returns the ChannelHash
    // can handle partial root hashes
    ChannelHash getChannelHashFromRoot(uint8_t *_root_hash, size_t);

    // given a root hash and commit hash, returns the next commit hash in the chain
    // can handle partial root and commit hashes, always fills the buffer with 32 bytes
    // returns true if a next hash was found
    bool getNextHash(uint8_t *, size_t, uint8_t *, size_t, uint8_t *);

    // For a given Meshtastic ChannelHash, fills the root_hash buffer with a 32-byte root hash
    // but this function will add the root hash if it is not already present
    // returns hash size or 0 if not found/added
    size_t getOrAddRootFromChannelHash(ChannelHash, uint8_t *);

    // adds the ChannelHash and root_hash to the mappings table
    void addRootToMappings(ChannelHash, uint8_t *);

    // requests the next message in the chain from the mesh network
    // Sends a LINK_REQUEST message
    void requestNextMessage(uint8_t *, size_t, uint8_t *, size_t);

    // request the message X entries from the end.
    // used to bootstrap a chain, without downloading all of the history
    void requestMessageCount(uint8_t *, size_t, uint32_t);

    // sends a LINK_PROVIDE message broadcasting the given link object
    void broadcastLink(uint8_t *, size_t);

    // sends a LINK_PROVIDE message broadcasting the given link object
    void broadcastLink(link_object &, bool, bool = false);

    // sends a LINK_PROVIDE message broadcasting the given link object from scratch message store
    bool sendFromScratch(uint8_t *);

    // Adds the given link object to the canonical chain database
    bool addToChain(link_object &);

    // Adds an incoming text message to the scratch database
    bool addToScratch(link_object &);

    // sends a CANON_ANNOUNCE message, specifying the given root and commit hashes
    void canonAnnounce(link_object &);

    // checks if the message hash is present in the canonical chain database
    bool isInDB(uint8_t *, size_t);

    // checks if the commit hash is present in the canonical chain database
    bool isCommitInDB(uint8_t *, size_t);

    // checks if the message hash is present in the scratch database
    bool isInScratch(uint8_t *, size_t);

    // retrieves a link object from the scratch database
    link_object getFromScratch(uint8_t *, size_t);

    // removes a link object from the scratch database
    void removeFromScratch(uint8_t *, size_t);

    // iterate through our scratch database, and see if we can speculate a chain up to the given commit hash
    bool speculateScratchChain(uint8_t *, size_t, uint8_t *, uint8_t *);

    // retrieves the next link object from scratch given a root hash
    link_object getNextScratchObject(uint8_t *);

    // fills the payload section with the decrypted data for the given message hash
    // probably not needed for production, but useful for testing
    void updatePayload(uint8_t *, size_t, std::string);

    // Takes the decrypted MeshPacket and the encrypted packet copy, and builds a link_object
    // Generates a message hash, but does not set the commit hash
    link_object ingestTextPacket(const meshtastic_MeshPacket &, const meshtastic_MeshPacket *);

    // ingests a LINK_PROVIDE message and builds a link_object
    // confirms the root hash and commit hash
    link_object ingestLinkMessage(meshtastic_StoreForwardPlusPlus *);

    // retrieves a link object from the canonical chain database given a commit hash
    link_object getLink(uint8_t *, size_t);

    // retrieves a link object from the canonical chain database given a message hash
    link_object getLinkFromMessageHash(uint8_t *, size_t);

    // puts the encrypted payload back into the queue as if it were just received
    void rebroadcastLinkObject(link_object &);

    // Check if an incoming link object's commit hash matches the calculated commit hash
    bool checkCommitHash(link_object &lo, uint8_t *commit_hash_bytes, size_t hash_len);

    // given a partial root hash, looks up the full 32-byte root hash
    // returns true if found
    bool lookUpFullRootHash(uint8_t *partial_root_hash, size_t partial_root_hash_len, uint8_t *full_root_hash);

    // update the mappings table to set the chain count for the given root hash
    void setChainCount(uint8_t *, size_t, uint32_t);

    // get the chain count for the given root hash
    uint32_t getChainCount(uint8_t *, size_t);

    // get the scratch count for the given root hash
    uint32_t getScratchCount(uint8_t *, size_t);

    // get the canon scratch count for the given root hash
    uint32_t getCanonScratchCount(uint8_t *, size_t);

    link_object getLinkFromPositionFromTip(uint32_t, uint8_t *, size_t);

    void pruneScratchQueue();

    void trimOldestLink(uint8_t *, size_t);

    void clearChain(uint8_t *, size_t);

    void recalculateMessageHash(link_object &);

    // given a link object with a payload and other fields, recalculates the message hash
    // returns true if a match
    bool recalculateHash(link_object &, uint8_t *, size_t, uint8_t *, size_t);

    void updatePeers(const meshtastic_MeshPacket &, meshtastic_StoreForwardPlusPlus_SFPP_message_type);

    void maybeMoveFromCanonScratch(uint8_t *, size_t);

    void addToCanonScratch(link_object &);

    link_object getfromCanonScratch(uint8_t *, size_t);
    void removeFromCanonScratch(uint8_t *, size_t);

    void clearCanonScratch(uint8_t *, size_t, uint32_t);

    bool isInCanonScratch(uint8_t *, size_t);

    void logLinkObject(link_object &);

    // Track if we have a scheduled runOnce pending
    // useful to not accudentally delay a scheduled runOnce
    bool pendingRun = false;

    // Once we have multiple chain types, we can extend this
    enum chain_types {
        channel_chain = 0,
    };

    uint32_t rebroadcastTimeout = 3600; // Messages older than this (in seconds) will not be rebroadcast
    bool doing_split_send = false;
    link_object split_link_out;

    bool doing_split_receive = false;
    link_object split_link_in;

    bool did_announce_last = false;

    uint32_t texts_rebroadcast = 0;
    uint32_t links_speculated = 0;
    uint32_t canon_announces = 0;
    uint32_t links_requested = 0;
    uint32_t links_provided = 0;
    uint32_t links_added = 0;
    uint32_t links_from_canon_scratch = 0;
    uint32_t links_from_scratch = 0;
    uint32_t split_links_sent = 0;
    uint32_t split_links_received = 0;
    uint32_t links_pruned = 0;
    uint32_t scratch_timed_out = 0;
    uint32_t sent_from_scratch = 0;
    uint32_t received_from_scratch = 0;

    // Enumeration (arm C) state. One exchange is in flight at a time; the next advert tick retries.
    sfppframe::Reassembler enumReassembler;
    uint32_t enumBucketCursor = 0;

    bool enumExchangeActive = false;
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
    bool enumProvideSeen[8] = {};
    std::vector<pinsketch::Element> enumProvideIds;

    std::vector<pinsketch::Element> enumWanted;
    size_t enumWantedRemaining = 0;

    // The peer summary this exchange is trying to reach, and the short-ID-collision probe state.
    uint32_t enumPeerCount = 0;
    uint64_t enumPeerChecksum = 0;
    bool enumCollisionProbe = false;
    bool enumSketchAttempt = false;
    uint32_t enumSketchIds = 0;
    uint32_t enumLastItemMs = 0;

    // A bucket whose short-ID lists match but whose checksum never does is not pulled again until
    // the authority changes what it advertises for it.
    struct EnumSuppression {
        uint8_t root[SFPP_HASH_SIZE];
        uint32_t bucket;
        uint32_t count;
        uint64_t checksum;
    };
    std::vector<EnumSuppression> enumSuppressed;

    std::vector<EnumRequester> enumTxQueues;
    size_t enumQueueCursor = 0;
    uint32_t enumLastDrainMs = 0;

    bool enumAwaitingList = false;
    uint8_t enumListRetries = 0;
    uint8_t enumItemRetries = 0;
    uint32_t enumRequestSentMs = 0;
};

extern StoreForwardPlusPlusModule *storeForwardPlusPlusModule;
#endif