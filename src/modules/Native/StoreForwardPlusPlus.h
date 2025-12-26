#pragma once
#if __has_include("sqlite3.h")
#define SFPP_ENABLED 1
#include "Channels.h"
#include "ProtobufModule.h"
#include "Router.h"
#include "SinglePortModule.h"
#include "sqlite3.h"

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
        uint8_t message_hash[32] = {0};
        size_t message_hash_len = 0;
        uint8_t root_hash[32] = {0};
        size_t root_hash_len = 0;
        uint8_t commit_hash[32] = {0};
        size_t commit_hash_len = 0;
        uint32_t counter = 0;
        std::string payload;
        bool validObject = true; // set this false when a chain calulation fails, etc.
    };

  public:
    /** Constructor
     *
     */
    StoreForwardPlusPlusModule();

    /*
      -Override the wantPacket method.
    */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        switch (p->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
            return true;
        default:
            return false;
        }
    }

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
    sqlite3_stmt *checkDup;
    sqlite3_stmt *checkScratch;
    sqlite3_stmt *removeScratch;
    sqlite3_stmt *updatePayloadStmt;
    sqlite3_stmt *getPayloadFromScratchStmt;
    sqlite3_stmt *fromScratchStmt;
    sqlite3_stmt *fromScratchByHashStmt;
    sqlite3_stmt *getNextHashStmt;
    sqlite3_stmt *getChainEndStmt;
    sqlite3_stmt *getLinkStmt;
    sqlite3_stmt *getHashFromRootStmt;
    sqlite3_stmt *addRootToMappingsStmt;
    sqlite3_stmt *getRootFromChannelHashStmt;
    sqlite3_stmt *getFullRootHashStmt;
    sqlite3_stmt *setChainCountStmt;
    sqlite3_stmt *getChainCountStmt;

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
    // returns true if the hash is new
    bool getOrAddRootFromChannelHash(ChannelHash, uint8_t *);

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
    void broadcastLink(link_object &, bool);

    // sends a LINK_PROVIDE message broadcasting the given link object from scratch message store
    bool sendFromScratch(uint8_t *);

    // Adds the given link object to the canonical chain database
    bool addToChain(link_object &);

    // Adds an incoming text message to the scratch database
    bool addToScratch(link_object &);

    // sends a CANON_ANNOUNCE message, specifying the given root and commit hashes
    void canonAnnounce(uint8_t *, uint8_t *, uint8_t *, uint32_t);

    // checks if the message hash is present in the canonical chain database
    bool isInDB(uint8_t *, size_t);

    // checks if the message hash is present in the scratch database
    bool isInScratch(uint8_t *, size_t);

    // retrieves a link object from the scratch database
    link_object getFromScratch(uint8_t *, size_t);

    // removes a link object from the scratch database
    void removeFromScratch(uint8_t *, size_t);

    // fills the payload section with the decrypted data for the given message hash
    // probably not needed for production, but useful for testing
    void updatePayload(uint8_t *, size_t, std::string);

    // Takes the decrypted MeshPacket and the encrypted packet copy, and builds a link_object
    // Generates a message hash, but does not set the commit hash
    link_object ingestTextPacket(const meshtastic_MeshPacket &, const meshtastic_MeshPacket *);

    // ingests a LINK_PROVIDE message and builds a link_object
    // confirms the root hash and commit hash
    link_object ingestLinkMessage(meshtastic_StoreForwardPlusPlus *);

    // retrieves a link object from the canonical chain database given a message hash
    link_object getLink(uint8_t *, size_t);

    // puts the encrypted payload back into the queue as if it were just received
    void rebroadcastLinkObject(link_object &);

    // Check if an incoming link object's commit hash matches the calculated commit hash
    bool checkCommitHash(link_object &lo, uint8_t *commit_hash_bytes, size_t hash_len);

    // given a partial root hash, looks up the full 32-byte root hash
    // returns true if found
    bool lookUpFullRootHash(uint8_t *partial_root_hash, size_t partial_root_hash_len, uint8_t *full_root_hash);

    // update the mappings table to set the chain count for the given root hash
    void setChainCount(uint8_t *, size_t, uint32_t);

    // query the mappings table for the chain count for the given root hash
    uint32_t getChainCount(uint8_t *, size_t);

    link_object getLinkFromCount(uint32_t, uint8_t *, size_t);

    // Track if we have a scheduled runOnce pending
    // useful to not accudentally delay a scheduled runOnce
    bool pendingRun = false;

    // Once we have multiple chain types, we can extend this
    enum chain_types {
        channel_chain = 0,
    };

    uint32_t rebroadcastTimeout = 3600; // Messages older than this (in seconds) will not be rebroadcast
};
#endif