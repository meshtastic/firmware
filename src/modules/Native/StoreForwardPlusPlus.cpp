// ideas:
// Track the nodenums we've gotten SFPP traffic from, to build a network graph
// store nodes, packet counts, and hops away in a new peers table

// TODO: Include the counter in the commit hash?

// Eventual TODO: non-stratum0 nodes need to be pointed at their upstream source? Maybe
#if __has_include("sqlite3.h")

#include "StoreForwardPlusPlus.h"
#include "MeshService.h"
#include "RTC.h"
#include "SHA256.h"
#include "SfppFrame.h"
#include "UptimeClock.h"
#include "concurrency/LockGuard.h"
#include "mesh/CryptoEngine.h"
#include "mesh/Throttle.h"
#include "meshUtils.h"
#include "modules/RoutingModule.h"
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
#include "modules/StoreForwardModule.h"
#endif

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <utility>
#include <vector>

// Reads PRAGMA user_version, and whether the store holds any table at all (a new file holds none).
static int readSchemaVersion(sqlite3 *db, bool &hasTables)
{
    int version = 0;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        version = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    st = nullptr;
    hasTables = false;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' LIMIT 1;", -1, &st, NULL) == SQLITE_OK)
        hasTables = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return version;
}

// One enumeration exchange is retried by the next advert tick; this is how long it may stay in flight.
static constexpr uint32_t kEnumExchangeTimeoutMs = 15 * 60 * 1000;

// After a short-ID-collision probe, silence this long means the peer has served all it has.
static constexpr uint32_t kEnumProbeIdleMs = 30 * 1000;

// One item frame per drain keeps the radio's 16-entry TX queue from overflowing on a large request.
static constexpr uint32_t kEnumDrainMinMs = 100;
static constexpr size_t kEnumTxQueueMax = 256;
static constexpr size_t kEnumMaxRequesters = 16;
static constexpr uint32_t kEnumRetryMs = 30 * 1000;
static constexpr uint32_t kEnumRequesterTtlMs = 2 * kEnumExchangeTimeoutMs;
static constexpr uint32_t kEnumFullRootEvery = 10;
static constexpr size_t kEnumPendingMax = 8;
static constexpr uint32_t kEnumLateAcceptMs = 5 * 60 * 1000;

// D11's one-pacer budget, shared by every serving path: 30 frames in any rolling 10 min.
static constexpr uint32_t kServeWindowMs = 10 * 60 * 1000;
static constexpr uint8_t kServeWindowFrames = 30;

// D7's retention applies to canon rows as well as to the legacy read window.
static constexpr uint32_t kRetentionS = 48 * 60 * 60;

// D19 proposes W = 24 h as the service window; unconfirmed, so holes past it are accepted as lost.
static constexpr uint32_t kServiceWindowMs = 24 * 60 * 60 * 1000;
// Failed pulls back off, and the backoff and attempt count cap a busy advert stream's retry cost.
static constexpr uint32_t kFailedPullBackoffMs = 30 * 1000;
static constexpr uint32_t kFailedPullBackoffMaxMs = 5 * 60 * 1000;
static constexpr uint8_t kFailedPullMaxAttempts = 8;

// Stage 8e: the item retry must outlast a full round-robin delivery at the current pacing.
static uint32_t enumDrainIntervalMs();

static uint32_t itemRetryIntervalMs()
{
    const uint32_t scaled = 4 * enumDrainIntervalMs(); // 2 requesters x 2 intervals
    return scaled > kEnumRetryMs ? scaled : kEnumRetryMs;
}

// Stage 9: offer a scratch row once it is old enough that the authority would have announced it if
// it had heard it; re-offer at most hourly, and bound a sweep.
static constexpr uint32_t kScratchPushMinAgeS = 300;
static constexpr uint32_t kScratchReofferMs = 60 * 60 * 1000;
static constexpr size_t kEnumPushOfferedMax = 64;
static constexpr size_t kEnumMaxKnownPeers = 16;
static constexpr size_t kScratchPushPerSweep = 8;
static constexpr size_t kEnumMaxPushSenders = 16;
static constexpr uint32_t kPushBudgetRows = 8;

static uint32_t scratchSweepMs()
{
    const uint32_t minutes = portduino_config.sfpp_scratch_sweep < 1 ? 1 : portduino_config.sfpp_scratch_sweep;
    return minutes * 60 * 1000;
}

static uint32_t enumDrainIntervalMs()
{
    const uint32_t configured = portduino_config.sfpp_enum_drain_ms;
    return configured < kEnumDrainMinMs ? kEnumDrainMinMs : configured;
}

namespace
{
void formatRootHex(const uint8_t *root, size_t len, char out[9])
{
    const size_t n = len < 4 ? len : 4;
    for (size_t i = 0; i < n; i++)
        snprintf(out + 2 * i, 3, "%02x", (unsigned)root[i]);
    out[2 * n] = '\0';
}

void copyScopeRoot(const uint8_t *root, size_t rootLen, sfppsr::Scope &scope, uint32_t bucket, bool fullRoot)
{
    scope = sfppsr::Scope();
    size_t n = fullRoot ? rootLen : sfppsr::kRootPrefixBytes;
    if (n > sizeof(scope.rootPrefix))
        n = sizeof(scope.rootPrefix);
    if (n > rootLen)
        n = rootLen;
    scope.rootPrefixLen = n;
    memcpy(scope.rootPrefix, root, n);
    scope.bucket = bucket;
}
} // namespace

// A follower's chain rows carry the ciphertext but not the plaintext. Decrypt the primary channel's
// copy and accept it only when it decodes as a text Data.
static bool decodeStoredText(const uint8_t *encrypted, size_t encryptedLen, uint32_t to, uint32_t from, uint32_t id,
                             std::string &out)
{
    if (encrypted == nullptr || encryptedLen == 0 || encryptedLen > MAX_BLOCKSIZE)
        return false;

    CryptoKey key = channels.getKey(0);
    if (key.length <= 0)
        return false;

    const bool aead = channels.isAEADEnabled(0);
    size_t plainLen = encryptedLen;
    uint8_t plain[MAX_BLOCKSIZE];
    bool decrypted;
    {
        concurrency::LockGuard g(cryptLock);
        if (aead) {
            // The stored AEAD row ends with the 12-byte tag, which is not part of the Data.
            if (encryptedLen <= CryptoEngine::AEAD_TAG_SIZE)
                return false;
            plainLen = encryptedLen - CryptoEngine::AEAD_TAG_SIZE;
            decrypted = crypto->decryptPacketCCM(key, from, to, id, encryptedLen, encrypted, plain);
        } else {
            memcpy(plain, encrypted, encryptedLen);
            crypto->setKey(key);
            crypto->decrypt(from, id, encryptedLen, plain);
            decrypted = true;
        }
    }
    if (!decrypted)
        return false;

    meshtastic_Data data;
    if (!pb_decode_from_bytes(plain, plainLen, &meshtastic_Data_msg, &data))
        return false;
    if (data.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP || data.payload.size == 0)
        return false;

    out.assign((const char *)data.payload.bytes, data.payload.size);
    return true;
}
static_assert(sfppframe::kMessageHashBytes == SFPP_HASH_SIZE, "frame and store message-hash widths must agree");

// The shim speaks the legacy StoreAndForward protobuf, which is not this module's template type,
// so its packets are built by hand rather than with allocDataProtobuf().
static meshtastic_MeshPacket *allocStoreForwardPacket(const meshtastic_StoreAndForward &payload)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p)
        return nullptr;
    p->decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &payload);
    return p;
}

StoreForwardPlusPlusModule::StoreForwardPlusPlusModule()
    : ProtobufModule("StoreForwardpp",
                     portduino_config.sfpp_steal_port ? meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP
                                                      : meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP,
                     &meshtastic_StoreForwardPlusPlus_msg),
      concurrency::OSThread("StoreForwardpp")
{

    std::string db_path = portduino_config.sfpp_db_path + "storeforwardpp.db";
    LOG_INFO("Opening StoreForwardpp DB at %s", db_path.c_str());
    if (portduino_config.sfpp_stratum0)
        LOG_INFO("SF++ running as stratum0");
    int res = sqlite3_open(db_path.c_str(), &ppDb);
    if (res != SQLITE_OK) {
        LOG_ERROR("Cannot open database: %s", sqlite3_errmsg(ppDb));
        sqlite3_close(ppDb);
        ppDb = nullptr;
        exit(EXIT_FAILURE);
    }
    if (sqlite3_db_readonly(ppDb, "main")) {
        LOG_ERROR("Database opened read-only!");
        sqlite3_close(ppDb);
        ppDb = nullptr;
        exit(EXIT_FAILURE);
    }

    bool hasTables = false;
    const int foundVersion = readSchemaVersion(ppDb, hasTables);
    if (hasTables && foundVersion != kSchemaVersion) {
        const std::string aside = db_path + ".v" + std::to_string(foundVersion) + "-" + std::to_string(getTime());
        LOG_WARN("StoreForwardpp DB schema %d, expected %d: moving it to %s and starting empty", foundVersion, kSchemaVersion,
                 aside.c_str());
        sqlite3_close(ppDb);
        ppDb = nullptr;
        if (std::rename(db_path.c_str(), aside.c_str()) != 0) {
            LOG_ERROR("Cannot move the old StoreForwardpp DB aside");
            exit(EXIT_FAILURE);
        }
        std::rename((db_path + "-journal").c_str(), (aside + "-journal").c_str());
        res = sqlite3_open(db_path.c_str(), &ppDb);
        if (res != SQLITE_OK) {
            LOG_ERROR("Cannot open database: %s", sqlite3_errmsg(ppDb));
            sqlite3_close(ppDb);
            ppDb = nullptr;
            exit(EXIT_FAILURE);
        }
    }

    // A v2 store belongs to the channel root it was built under. If it has channel mappings and none
    // is our derived root, the channel name or key changed: move it aside and start a fresh store.
    {
        uint8_t derived[SFPP_HASH_SIZE] = {0};
        const bool haveDerived = deriveRootFromChannel(channels.getHash(0), derived);
        bool hasChannelMapping = false;
        bool matchesDerived = false;
        if (haveDerived) {
            sqlite3_stmt *stmt = nullptr;
            // A fresh file has no mappings table yet, so this simply finds nothing.
            if (sqlite3_prepare_v2(ppDb, "select root_hash from mappings where chain_type=?;", -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, chain_types::channel_chain);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    hasChannelMapping = true;
                    const uint8_t *root = (const uint8_t *)sqlite3_column_blob(stmt, 0);
                    const int len = sqlite3_column_bytes(stmt, 0);
                    if (root && len == (int)SFPP_HASH_SIZE && memcmp(root, derived, SFPP_HASH_SIZE) == 0)
                        matchesDerived = true;
                }
            }
            if (stmt)
                sqlite3_finalize(stmt);
        }
        if (haveDerived && hasChannelMapping && !matchesDerived) {
            const std::string aside = db_path + ".root-" + std::to_string(getTime());
            LOG_WARN("StoreForwardpp channel root changed: moving %s aside and starting empty", aside.c_str());
            sqlite3_close(ppDb);
            ppDb = nullptr;
            if (std::rename(db_path.c_str(), aside.c_str()) != 0) {
                LOG_ERROR("Cannot move the old StoreForwardpp DB aside");
                exit(EXIT_FAILURE);
            }
            std::rename((db_path + "-journal").c_str(), (aside + "-journal").c_str());
            res = sqlite3_open(db_path.c_str(), &ppDb);
            if (res != SQLITE_OK) {
                LOG_ERROR("Cannot open database: %s", sqlite3_errmsg(ppDb));
                sqlite3_close(ppDb);
                ppDb = nullptr;
                exit(EXIT_FAILURE);
            }
        }
    }
    char *err = nullptr;

    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        channel_messages(              \
        destination INT NOT NULL,      \
        sender INT NOT NULL,           \
        packet_id INT NOT NULL,        \
        rx_time INT NOT NULL,          \
        root_hash BLOB NOT NULL,       \
        encrypted_bytes BLOB NOT NULL, \
        message_hash BLOB NOT NULL,    \
        commit_hash BLOB NOT NULL,     \
        payload TEXT,                  \
        counter INT DEFAULT 0,         \
        short_id INT,                  \
        checksum INT,                  \
        PRIMARY KEY (message_hash)     \
        );",
                       NULL, NULL, &err);
    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        local_messages(                \
        destination INT NOT NULL,      \
        sender INT NOT NULL,           \
        packet_id INT NOT NULL,        \
        rx_time INT NOT NULL,          \
        root_hash BLOB NOT NULL,       \
        encrypted_bytes BLOB NOT NULL, \
        message_hash BLOB NOT NULL,    \
        payload TEXT,                  \
        PRIMARY KEY (message_hash)     \
        );",
                       NULL, NULL, &err);
    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    // create table DMs
    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        direct_messages(               \
        destination INT NOT NULL,      \
        sender INT NOT NULL,           \
        packet_id INT NOT NULL,        \
        rx_time INT NOT NULL,          \
        root_hash BLOB NOT NULL,       \
        commit_hash BLOB NOT NULL,     \
        encrypted_bytes BLOB NOT NULL, \
        message_hash BLOB NOT NULL,    \
        payload TEXT,                  \
        PRIMARY KEY (message_hash)     \
        );",
                       NULL, NULL, &err);

    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    // add canon_scratch table
    // where we move message out of scratch when we've been informed that message exists on the canon chain, but we're not ready
    // to commit it on our own chain

    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        canon_scratch(                 \
        destination INT NOT NULL,      \
        sender INT NOT NULL,           \
        packet_id INT NOT NULL,        \
        rx_time INT NOT NULL,          \
        root_hash BLOB NOT NULL,       \
        encrypted_bytes BLOB NOT NULL, \
        message_hash BLOB NOT NULL,    \
        commit_hash BLOB NOT NULL,     \
        payload TEXT,                  \
        counter INT NOT NULL,          \
        PRIMARY KEY (message_hash)     \
        );",
                       NULL, NULL, &err);
    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    // mappings table -- connects the root hashes to channel hashes and DM identifiers. Keyed by the
    // root, not the 1-byte identifier, so two channels that share a hash keep separate stores.
    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        mappings(                      \
        chain_type INT NOT NULL,       \
        identifier INT NOT NULL,       \
        root_hash BLOB NOT NULL,       \
        count INT DEFAULT 0,           \
        PRIMARY KEY (root_hash)        \
        );",
                       NULL, NULL, &err);

    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    // peers table, tracking nodes we've heard SFPP traffic from
    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        peers(                      \
        nodenum INT NOT NULL,       \
        announce_count INT DEFAULT 0,           \
        query_count INT DEFAULT 0,           \
        request_count INT DEFAULT 0,           \
        provide_count INT DEFAULT 0,           \
        split_count INT DEFAULT 0,           \
        total_count INT DEFAULT 0,           \
        average_hops REAL DEFAULT 0,           \
        PRIMARY KEY (nodenum)       \
        );",
                       NULL, NULL, &err);

    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    sqlite3_exec(ppDb, ("PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";").c_str(), NULL, NULL, NULL);

    // Old int binds stored node/ID values >= 2^31 as negatives; repair them in place (idempotent).
    static const char *repairSigned = "UPDATE channel_messages SET destination=destination+4294967296 WHERE destination<0;"
                                      "UPDATE channel_messages SET sender=sender+4294967296 WHERE sender<0;"
                                      "UPDATE channel_messages SET packet_id=packet_id+4294967296 WHERE packet_id<0;"
                                      "UPDATE local_messages SET destination=destination+4294967296 WHERE destination<0;"
                                      "UPDATE local_messages SET sender=sender+4294967296 WHERE sender<0;"
                                      "UPDATE local_messages SET packet_id=packet_id+4294967296 WHERE packet_id<0;"
                                      "UPDATE direct_messages SET destination=destination+4294967296 WHERE destination<0;"
                                      "UPDATE direct_messages SET sender=sender+4294967296 WHERE sender<0;"
                                      "UPDATE direct_messages SET packet_id=packet_id+4294967296 WHERE packet_id<0;"
                                      "UPDATE canon_scratch SET destination=destination+4294967296 WHERE destination<0;"
                                      "UPDATE canon_scratch SET sender=sender+4294967296 WHERE sender<0;"
                                      "UPDATE canon_scratch SET packet_id=packet_id+4294967296 WHERE packet_id<0;"
                                      "DELETE FROM peers WHERE nodenum<0 AND nodenum+4294967296 IN (SELECT nodenum FROM peers);"
                                      "UPDATE peers SET nodenum=nodenum+4294967296 WHERE nodenum<0;";
    if (sqlite3_exec(ppDb, repairSigned, NULL, NULL, NULL) != SQLITE_OK)
        LOG_WARN("StoreForwardpp signed-row repair failed: %s", sqlite3_errmsg(ppDb));

    // prepared statements *should* make this faster.
    sqlite3_prepare_v2(ppDb, "INSERT INTO channel_messages (destination, sender, packet_id, root_hash, \
        encrypted_bytes, message_hash, rx_time, commit_hash, payload, counter, short_id, checksum) \
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &chain_insert_stmt, NULL);

    // Both identifiers are stored beside the object, so summarising a bucket never rehashes and a
    // restart costs a query rather than a pass over the chain.
    sqlite3_prepare_v2(ppDb, "select short_id, checksum from channel_messages \
        where substr(root_hash,1,?)=? and counter>=? and counter<=? and short_id is not null;",
                       -1, &bucketSummaryStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select max(counter) from channel_messages where substr(root_hash,1,?)=?;", -1, &maxCounterStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "select message_hash from channel_messages where short_id is null limit 128;", -1,
                       &unindexedLinksStmt, NULL);

    sqlite3_prepare_v2(ppDb, "update channel_messages set short_id=?, checksum=? where message_hash=?;", -1, &setLinkSetIndexStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "select short_id from channel_messages \
        where substr(root_hash,1,?)=? and counter>=? and counter<=? and short_id is not null order by counter;",
                       -1, &bucketIdsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, counter \
        from channel_messages where substr(root_hash,1,?)=? and counter>=? and counter<=? and short_id=?;",
                       -1, &getLinkByShortIdStmt, NULL);

    sqlite3_prepare_v2(ppDb, "delete from channel_messages where message_hash=?;", -1, &deleteCanonStmt, NULL);

    // Legacy CLIENT_HISTORY reads the stored plaintext, or the ciphertext a follower holds instead.
    sqlite3_prepare_v2(ppDb, "select sender, packet_id, destination, rx_time, payload, encrypted_bytes from channel_messages \
        where substr(root_hash,1,?)=? and rx_time>=? and rx_time>? and sender!=? \
        and (destination=? or destination=?) order by rx_time asc limit ?;",
                       -1, &legacyHistoryStmt, NULL);

    sqlite3_prepare_v2(ppDb, "INSERT INTO local_messages (destination, sender, packet_id, root_hash, \
        encrypted_bytes, message_hash, rx_time, payload) VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &scratch_insert_stmt, NULL);

    sqlite3_prepare_v2(ppDb, "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, root_hash \
        from local_messages where root_hash=? order by rx_time asc;", // earliest first
                       -1, &fromScratchStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, root_hash, payload \
        from local_messages where substr(message_hash,1,?)=? order by rx_time asc LIMIT 1;", // earliest first
                       -1, &fromScratchByHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "SELECT COUNT(*) from channel_messages where substr(message_hash,1,?)=?", -1, &checkDupMessageHash,
                       NULL);

    sqlite3_prepare_v2(ppDb, "SELECT COUNT(*) from channel_messages where substr(commit_hash,1,?)=?", -1, &checkDupCommitHash,
                       NULL);

    sqlite3_prepare_v2(ppDb, "SELECT COUNT(*) from local_messages where substr(message_hash,1,?)=?", -1, &checkScratch, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE from local_messages where substr(message_hash,1,?)=?", -1, &removeScratch, NULL);

    sqlite3_prepare_v2(ppDb, "UPDATE channel_messages SET payload=? WHERE substr(message_hash,1,?)=?", -1, &updatePayloadStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "select commit_hash from channel_messages where substr(root_hash,1,?)=? order by rowid ASC;", -1,
                       &getNextHashStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "select commit_hash, message_hash, rx_time from channel_messages where substr(root_hash,1,?)=? order by "
                       "rowid desc;",
                       -1, &getChainEndStmt, NULL);

    sqlite3_prepare_v2(
        ppDb,
        "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, commit_hash, root_hash, counter, payload \
        from channel_messages where substr(commit_hash,1,?)=?;",
        -1, &getLinkStmt, NULL);

    sqlite3_prepare_v2(
        ppDb,
        "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, commit_hash, root_hash, counter, payload \
        from channel_messages where substr(message_hash,1,?)=?;",
        -1, &getLinkFromMessageHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select identifier from mappings where substr(root_hash,1,?)=?;", -1, &getHashFromRootStmt, NULL);

    sqlite3_prepare_v2(ppDb, "INSERT OR REPLACE INTO mappings (chain_type, identifier, root_hash) VALUES(?, ?, ?);", -1,
                       &addRootToMappingsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select root_hash from mappings where substr(root_hash,1,?)=?;", -1, &getFullRootHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "UPDATE mappings SET count=? WHERE substr(root_hash,1,?)=?;", -1, &setChainCountStmt, NULL);

    sqlite3_prepare_v2(ppDb, "SELECT count(*) FROM channel_messages WHERE substr(root_hash,1,?)=?;", -1, &getChainCountStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "SELECT count(*) FROM local_messages WHERE substr(root_hash,1,?)=?;", -1, &getScratchCountStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "SELECT count(*) FROM canon_scratch WHERE substr(root_hash,1,?)=?;", -1,
                       &getRootCanonScratchCountStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM local_messages WHERE rx_time < ?;", -1, &pruneScratchQueueStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "DELETE FROM channel_messages WHERE commit_hash in ( select commit_hash from channel_messages where "
                       "substr(root_hash,1,?)=? ORDER BY rowid ASC LIMIT 1);",
                       -1, &trimOldestLinkStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM channel_messages WHERE substr(root_hash,1,?)=? AND rx_time < ?;", -1,
                       &trimOldByTimeStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "INSERT INTO peers (nodenum, total_count, average_hops) VALUES(?, ?, ?) ON CONFLICT(nodenum) DO NOTHING;",
                       -1, &maybeAddPeerStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "SELECT announce_count, query_count, request_count, provide_count, split_count, total_count, average_hops "
                       "FROM peers WHERE nodenum=?;",
                       -1, &getPeerStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "UPDATE peers SET announce_count=?, query_count=?, request_count=?, provide_count=?, split_count=?, "
                       "total_count=?, average_hops=? WHERE nodenum=?;",
                       -1, &updatePeerStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM channel_messages WHERE substr(root_hash,1,?)=?;", -1, &clearChainStmt, NULL);

    sqlite3_prepare_v2(ppDb, "INSERT INTO canon_scratch (destination, sender, packet_id, root_hash, \
        encrypted_bytes, message_hash, rx_time, commit_hash, payload, counter) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &canon_scratch_insert_stmt, NULL);

    sqlite3_prepare_v2(ppDb, "SELECT count(*) FROM canon_scratch WHERE substr(message_hash,1,?)=?;", -1,
                       &getCanonScratchCountStmt, NULL);

    sqlite3_prepare_v2(
        ppDb,
        "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, commit_hash, root_hash, counter, payload \
        from canon_scratch where substr(root_hash,1,?)=? ORDER BY counter ASC;",
        -1, &getCanonScratchStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE from canon_scratch where substr(message_hash,1,?)=?", -1, &removeCanonScratch, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE from canon_scratch where substr(root_hash,1,?)=? AND counter < ?;", -1,
                       &clearCanonScratchStmt, NULL);

    encryptedOk = true;

    backfillSetIndex();

    this->setInterval(15 * 1000);
}

StoreForwardPlusPlusModule::~StoreForwardPlusPlusModule()
{
    sqlite3_stmt *statements[] = {
        chain_insert_stmt,
        scratch_insert_stmt,
        checkDupMessageHash,
        checkDupCommitHash,
        checkScratch,
        removeScratch,
        updatePayloadStmt,
        fromScratchStmt,
        fromScratchByHashStmt,
        getNextHashStmt,
        getChainEndStmt,
        getLinkStmt,
        getLinkFromMessageHashStmt,
        getHashFromRootStmt,
        addRootToMappingsStmt,
        getFullRootHashStmt,
        setChainCountStmt,
        getChainCountStmt,
        getScratchCountStmt,
        getRootCanonScratchCountStmt,
        pruneScratchQueueStmt,
        trimOldestLinkStmt,
        trimOldByTimeStmt,
        maybeAddPeerStmt,
        getPeerStmt,
        updatePeerStmt,
        clearChainStmt,
        canon_scratch_insert_stmt,
        getCanonScratchCountStmt,
        getCanonScratchStmt,
        removeCanonScratch,
        clearCanonScratchStmt,
        bucketSummaryStmt,
        maxCounterStmt,
        bucketIdsStmt,
        getLinkByShortIdStmt,
        deleteCanonStmt,
        unindexedLinksStmt,
        setLinkSetIndexStmt,
        legacyHistoryStmt,
    };
    for (sqlite3_stmt *stmt : statements) {
        if (stmt)
            sqlite3_finalize(stmt);
    }
    if (ppDb)
        sqlite3_close(ppDb);
}

void StoreForwardPlusPlusModule::backfillSetIndex()
{
    uint32_t indexed = 0;

    // One batch per pass: the select finds rows the update then fills, so re-running it is how the
    // loop advances. A store written before these columns existed converges in chain_length/128 passes.
    while (true) {
        std::vector<std::pair<uint32_t, uint64_t>> derived;
        std::vector<std::vector<uint8_t>> hashes;

        sqlite3_reset(unindexedLinksStmt);
        while (sqlite3_step(unindexedLinksStmt) == SQLITE_ROW) {
            const uint8_t *hash = (const uint8_t *)sqlite3_column_blob(unindexedLinksStmt, 0);
            const int len = sqlite3_column_bytes(unindexedLinksStmt, 0);
            if (hash == nullptr || len <= 0)
                continue;
            hashes.emplace_back(hash, hash + len);
            derived.emplace_back(sfppsr::shortId(hash, len), sfppsr::checksumContribution(hash, len));
        }
        sqlite3_reset(unindexedLinksStmt);

        if (hashes.empty())
            break;

        for (size_t i = 0; i < hashes.size(); i++) {
            sqlite3_reset(setLinkSetIndexStmt);
            sqlite3_bind_int64(setLinkSetIndexStmt, 1, derived[i].first);
            sqlite3_bind_int64(setLinkSetIndexStmt, 2, (sqlite3_int64)derived[i].second);
            sqlite3_bind_blob(setLinkSetIndexStmt, 3, hashes[i].data(), hashes[i].size(), NULL);
            if (sqlite3_step(setLinkSetIndexStmt) != SQLITE_DONE) {
                LOG_ERROR("StoreForwardpp cannot index link: %s", sqlite3_errmsg(ppDb));
                sqlite3_reset(setLinkSetIndexStmt);
                return; // an update that does not stick would loop the select forever
            }
            indexed++;
        }
        sqlite3_reset(setLinkSetIndexStmt);
    }

    if (indexed > 0)
        LOG_INFO("StoreForwardpp indexed %u existing links for set reconciliation", indexed);
}

bool StoreForwardPlusPlusModule::buildBucketSummary(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket,
                                                    sfppsr::BucketSummary &out)
{
    out.clear();

    // Counters are 1-based, so bucket b covers counters [b*n + 1, (b+1)*n].
    const uint32_t first = bucket * sfppsr::kBucketObjects + 1;
    const uint32_t last = first + sfppsr::kBucketObjects - 1;

    sqlite3_reset(bucketSummaryStmt);
    sqlite3_bind_int(bucketSummaryStmt, 1, root_hash_len);
    sqlite3_bind_blob(bucketSummaryStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(bucketSummaryStmt, 3, first);
    sqlite3_bind_int(bucketSummaryStmt, 4, last);

    while (sqlite3_step(bucketSummaryStmt) == SQLITE_ROW) {
        out.add((pinsketch::Element)sqlite3_column_int64(bucketSummaryStmt, 0),
                (uint64_t)sqlite3_column_int64(bucketSummaryStmt, 1));
    }
    sqlite3_reset(bucketSummaryStmt);

    return out.count() > 0;
}

uint32_t StoreForwardPlusPlusModule::highestCounter(const uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_reset(maxCounterStmt);
    sqlite3_bind_int(maxCounterStmt, 1, root_hash_len);
    sqlite3_bind_blob(maxCounterStmt, 2, root_hash, root_hash_len, NULL);
    uint32_t highest = 0;
    if (sqlite3_step(maxCounterStmt) == SQLITE_ROW && sqlite3_column_type(maxCounterStmt, 0) != SQLITE_NULL)
        highest = (uint32_t)sqlite3_column_int64(maxCounterStmt, 0);
    sqlite3_reset(maxCounterStmt);
    return highest;
}

bool StoreForwardPlusPlusModule::collectBucketIds(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket,
                                                  std::vector<pinsketch::Element> &out)
{
    out.clear();
    const uint32_t first = bucket * sfppsr::kBucketObjects + 1;
    const uint32_t last = first + sfppsr::kBucketObjects - 1;

    sqlite3_reset(bucketIdsStmt);
    sqlite3_bind_int(bucketIdsStmt, 1, root_hash_len);
    sqlite3_bind_blob(bucketIdsStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(bucketIdsStmt, 3, first);
    sqlite3_bind_int(bucketIdsStmt, 4, last);

    while (sqlite3_step(bucketIdsStmt) == SQLITE_ROW)
        out.push_back((pinsketch::Element)sqlite3_column_int64(bucketIdsStmt, 0));
    sqlite3_reset(bucketIdsStmt);

    return !out.empty();
}

int32_t StoreForwardPlusPlusModule::runOnce()
{
    if (legacyHistoryBusy) {
        sendNextLegacyHistory();
        return legacyHistoryBusy ? 5 * 1000 : portduino_config.sfpp_announce_interval * 60 * 1000;
    }
    if (pendingRun) {
        pendingRun = false;
        if (portduino_config.sfpp_repair == SFPP_REPAIR_CHAIN)
            setIntervalFromNow(portduino_config.sfpp_announce_interval * 60 * 1000 - 30 * 1000);
    }
    if (getRTCQuality() < RTCQualityNTP) {
        RTCQuality ourQuality = RTCQualityDevice;

        std::string timeCommandResult = exec("timedatectl status | grep synchronized | grep yes -c");
        if (timeCommandResult[0] == '1') {
            ourQuality = RTCQualityNTP;

            struct timeval tv;
            tv.tv_sec = time(NULL);
            tv.tv_usec = 0;
            perhapsSetRTC(ourQuality, &tv);
        } else {
            LOG_WARN("StoreForward++ deferred due to time quality %u result:%s", getRTCQuality(), timeCommandResult.c_str());
            return portduino_config.sfpp_announce_interval * 60 * 1000;
        }
    }

    uint8_t root_hash_bytes[SFPP_HASH_SIZE] = {0};
    ChannelHash hash = channels.getHash(0);

    if (getOrDeriveRootFromChannel(hash, root_hash_bytes) == 0) {
        LOG_WARN("No root hash found, not sending");
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    uint32_t chain_count = getChainCount(root_hash_bytes, SFPP_HASH_SIZE);
    uint32_t scratch_count = getScratchCount(root_hash_bytes, SFPP_HASH_SIZE);
    uint32_t canon_scratch_count = getCanonScratchCount(root_hash_bytes, SFPP_HASH_SIZE);

    // add links in scratch and links in canon_scratch

    LOG_DEBUG("chain_count=%u, scratch_count=%u, canon_scratch_count=%u, texts_rebroadcast=%u, links_speculated=%u, "
              "canon_announces=%u,",
              chain_count, scratch_count, canon_scratch_count, texts_rebroadcast, links_speculated, canon_announces);

    LOG_DEBUG("links_requested=%u, links_provided=%u, links_added=%u, links_from_canon_scratch=%u, links_from_scratch=%u, "
              "split_links_sent=%u",
              links_requested, links_provided, links_added, links_from_canon_scratch, links_from_scratch, split_links_sent);
    LOG_DEBUG(
        "split_links_received=%u, links_pruned = %u, scratch_timed_out = %u, sent_from_scratch = %u, received_from_scratch = %u",
        split_links_received, links_pruned, scratch_timed_out, sent_from_scratch, received_from_scratch);

    while (chain_count > portduino_config.sfpp_max_chain) {
        LOG_DEBUG("Chain length %u exceeds max %u, evicting oldest", chain_count, portduino_config.sfpp_max_chain);
        trimOldestLink(root_hash_bytes, SFPP_HASH_SIZE);
        chain_count--;
    }

    // D7: canon rows outside the service window are gone even when the row cap is not reached.
    const uint32_t nowS = getValidTime(RTCQuality::RTCQualityNTP, true);
    if (nowS > kRetentionS) {
        sqlite3_reset(trimOldByTimeStmt);
        sqlite3_bind_int(trimOldByTimeStmt, 1, SFPP_HASH_SIZE);
        sqlite3_bind_blob(trimOldByTimeStmt, 2, root_hash_bytes, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_int64(trimOldByTimeStmt, 3, (sqlite3_int64)(nowS - kRetentionS));
        sqlite3_step(trimOldByTimeStmt);
        sqlite3_reset(trimOldByTimeStmt);
    }
    // evict old messages from scratch
    pruneScratchQueue();

    if (memfll(root_hash_bytes, '\0', SFPP_HASH_SIZE)) {
        LOG_WARN("No root hash found, not sending");
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    // Enumeration mode: the chain announce, scratch push and re-injection are off. The authority
    // advertises one bucket per tick; the next advert is the retry for any unfinished exchange.
    if (portduino_config.sfpp_repair != SFPP_REPAIR_CHAIN) {
        expireEnumExchange();
        expireEnumLate();
        if (portduino_config.sfpp_enum_listen_only)
            return (int32_t)portduino_config.sfpp_enum_interval * 60 * 1000;
        pumpEnumRetries();
        // F3.5: the writer-missed sweep has its own timer, independent of the advert cadence.
        if (enumLastScratchSweepMs == 0 || Throttle::hasElapsed(enumLastScratchSweepMs, scratchSweepMs())) {
            enumLastScratchSweepMs = Time::getMillis();
            pushScratchRows();
        }
        if (drainOneEnumFrame())
            return enumDrainIntervalMs(); // serving a request takes priority over the advert cadence
        // Every archive advertises, but not in the middle of an exchange or with a pull queued.
        if (!enumExchangeActive && enumPending.empty() && !enumLateActive)
            advertiseOneBucket(root_hash_bytes, SFPP_HASH_SIZE);
        startPendingPull();
        if (enumExchangeActive)
            return 5 * 1000; // pump the probe idle and overall timeout while an exchange is in flight
        if (!enumPending.empty())
            return enumDrainIntervalMs(); // a paced pending pull is waiting
        if (enumLateActive)
            return 5 * 1000; // tighten the late-frame window expiry
        // Every archive advertises; the scratch sweep keeps its own, shorter cadence.
        const uint32_t advertMs = (uint32_t)portduino_config.sfpp_enum_interval * 60 * 1000;
        return (int32_t)(scratchSweepMs() < advertMs ? scratchSweepMs() : advertMs);
    }

    if (doing_split_send) {
        LOG_DEBUG("Sending split second half");
        broadcastLink(split_link_out, true, true);
        split_link_out = link_object();
        split_link_out.validObject = false;
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    if (!portduino_config.sfpp_stratum0 && did_announce_last && sendFromScratch(root_hash_bytes)) {
        LOG_DEBUG("Send from scratch queue");
        did_announce_last = false;
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    // get tip of chain for this channel
    link_object chain_end = getLinkFromPositionFromTip(0, root_hash_bytes, SFPP_HASH_SIZE);

    if (chain_end.rx_time == 0) {
        if (portduino_config.sfpp_stratum0) {
            LOG_DEBUG("Stratum0 with no messages on chain, sending empty announce");
        } else {
            LOG_DEBUG("Non-stratum0 with no chain, not sending");
            return portduino_config.sfpp_announce_interval * 60 * 1000;
        }

        // first attempt at a chain-only announce with no messages
        meshtastic_StoreForwardPlusPlus storeforward = meshtastic_StoreForwardPlusPlus_init_zero;
        storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_CANON_ANNOUNCE;
        storeforward.root_hash.size = SFPP_HASH_SIZE;
        memcpy(storeforward.root_hash.bytes, root_hash_bytes, SFPP_HASH_SIZE);

        storeforward.encapsulated_rxtime = 0;
        // storeforward.
        meshtastic_MeshPacket *p = allocDataProtobuf(storeforward);
        p->to = NODENUM_BROADCAST;
        p->decoded.want_response = false;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->channel = 0;
        p->hop_limit = portduino_config.sfpp_hops;
        p->hop_start = portduino_config.sfpp_hops;
        LOG_INFO("Send packet to mesh payload size %u", p->decoded.payload.size);
        sendSfppPacket(p, RX_SRC_LOCAL, true);

        did_announce_last = true;
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    // broadcast the tip of the chain
    // todo just send the link object
    canonAnnounce(chain_end);
    did_announce_last = true;
    // eventually timeout things on the scratch queue
    return portduino_config.sfpp_announce_interval * 60 * 1000;
}

void StoreForwardPlusPlusModule::handleEncrypted(const meshtastic_MeshPacket *mp, const meshtastic_MeshPacket *p_encrypted)
{
    // This is intended to handle text messages from the local node
    if (mp->from != nodeDB->getNodeNum() && mp->from != 0) {
        return;
    }

    link_object lo = ingestTextPacket(*mp, p_encrypted);

    if (lo.from == 0)
        lo.from = nodeDB->getNodeNum();

    if (isInDB(lo.message_hash, lo.message_hash_len) && isInScratch(lo.message_hash, lo.message_hash_len) &&
        isInCanonScratch(lo.message_hash, lo.message_hash_len)) {
        return;
    }

    if (!portduino_config.sfpp_stratum0) {
        if (lo.root_hash_len == 0) {
            LOG_DEBUG("StoreForwardpp Received text message, but no chain. Possibly no Stratum0 on local mesh.");
            return;
        }
        addToScratch(lo);
        LOG_DEBUG("StoreForwardpp added message to scratch db");
        // send link to upstream?

        return;
    }
    addToChain(lo);

    if (!pendingRun) {
        setIntervalFromNow(10 * 1000); // run again in 10 seconds to announce the new tip of chain
        pendingRun = true;
    }
}

ProcessMessage StoreForwardPlusPlusModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // To avoid terrible time problems, require NTP or GPS time
    if (getRTCQuality() < RTCQualityNTP) {
        return ProcessMessage::CONTINUE;
    }

    // A legacy CLIENT_HISTORY from a phone connected to this archive. Checked before the from==0
    // guard below, because the phone sends the request as the local node.
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP && !legacyStoreForwardServes()) {
        meshtastic_StoreAndForward req;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_StoreAndForward_msg, &req) &&
            req.rr == meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY) {
            handleLegacyClientHistory(mp, &req);
            return ProcessMessage::STOP;
        }
        // Heartbeats and another server's ROUTER_TEXT_* belong to the legacy client module.
        return ProcessMessage::CONTINUE;
    }

    if (mp.from == nodeDB->getNodeNum() || mp.from == 0) {
        return ProcessMessage::CONTINUE; // don't process our own packets here
    }

    // Allow only LoRa, Multicast UDP, and API packets
    // maybe in the future, only disallow MQTT
    if (mp.transport_mechanism != meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA &&
        mp.transport_mechanism != meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MULTICAST_UDP &&
        mp.transport_mechanism != meshtastic_MeshPacket_TransportMechanism_TRANSPORT_API) {
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want
    }

    // will eventually host DMs and other undecodable messages
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        LOG_WARN("StoreForwardpp Saw an encoded message, dropping");
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want
    }

    if (currentEncrypted == nullptr) {
        LOG_WARN("StoreForwardpp cannot process text message, due to null pointer");
        return ProcessMessage::CONTINUE;
    }

    if (mp.id != currentEncrypted->id) {
        LOG_WARN("Wrong packet in currentEncrypted, dropping text message");
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want
    }

    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && mp.to == NODENUM_BROADCAST) {
        link_object lo = ingestTextPacket(mp, currentEncrypted);

        if (isInDB(lo.message_hash, lo.message_hash_len)) {
            LOG_DEBUG("StoreForwardpp Found text message in chain DB");
            // We may have this message already, but we may not have the payload
            // if we do, we can update the payload in the database
            if (lo.payload != "")
                updatePayload(lo.message_hash, lo.message_hash_len, lo.payload);
            return ProcessMessage::CONTINUE;
        }
        if (isInDB(lo.message_hash, lo.message_hash_len) && isInScratch(lo.message_hash, lo.message_hash_len) &&
            isInCanonScratch(lo.message_hash, lo.message_hash_len)) {
            return ProcessMessage::CONTINUE;
        }

        if (!portduino_config.sfpp_stratum0) {
            if (lo.root_hash_len == 0) {
                LOG_DEBUG("StoreForwardpp Received text message, but no chain. Possibly no Stratum0 on local mesh.");
                return ProcessMessage::CONTINUE;
            }
            addToScratch(lo);
            LOG_DEBUG("StoreForwardpp added message to scratch db");
            // send link to upstream?

            return ProcessMessage::CONTINUE;
        }
        addToChain(lo);

        if (!pendingRun) {
            setIntervalFromNow(10 * 1000); // run again in 30 seconds to announce the new tip of chain
            pendingRun = true;
        }
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want

    } else if (mp.decoded.portnum == portduino_config.sfpp_steal_port ? meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP
                                                                      : meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP) {
        meshtastic_StoreForwardPlusPlus scratch;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_StoreForwardPlusPlus_fields,
                                 &scratch)) {
            handleReceivedProtobuf(mp, &scratch);
        }
        return ProcessMessage::CONTINUE;
    }
    return ProcessMessage::CONTINUE;
}

bool StoreForwardPlusPlusModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    if (portduino_config.sfpp_repair != SFPP_REPAIR_CHAIN) {
        expireEnumExchange();
        expireEnumLate();
        // ITEM_FRAME (13) is newer than the ADVERT..GONE helper range, so it is named explicitly.
        const bool srType = sfppsr::isSetReconciliationType(t->sfpp_message_type) ||
                            t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME;
        if (srType) {
            switch (t->sfpp_message_type) {
            case meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT:
                handleEnumAdvert(mp, t);
                break;
            case meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST:
                handleEnumRequest(mp, t);
                break;
            case meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE:
                handleEnumProvide(mp, t);
                break;
            case meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST:
                handleItemRequest(mp, t);
                break;
            case meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME:
                handleItemFrame(mp, t);
                break;
            default:
                break; // ITEM_PROVIDE and GONE are unused by arm C
            }
        }
        // Chain types are off in enum mode: enumeration is the only repair path.
        return true;
    }

    // Set-reconciliation traffic is inert while the chain is the repair path, and a chain message
    // carrying its fields is the overloading the wire format refuses.
    if (sfppsr::isSetReconciliationType(t->sfpp_message_type) ||
        t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
        return true;
    if (sfppsr::hasSetReconciliationFields(*t)) {
        LOG_WARN("StoreForwardpp chain message carries set-reconciliation fields, dropping");
        return true;
    }

    LOG_DEBUG("StoreForwardpp node %u sent us sf++ packet", mp.from);
    printBytes("StoreForwardpp commit_hash ", t->commit_hash.bytes, t->commit_hash.size);
    printBytes("StoreForwardpp root_hash ", t->root_hash.bytes, t->root_hash.size);
    printBytes("StoreForwardpp message_hash ", t->message_hash.bytes, t->message_hash.size);

    link_object incoming_link;
    incoming_link.validObject = false;
    link_object chain_end;

    // get tip of chain for this channel
    if (t->root_hash.size >= SFPP_SHORT_HASH_SIZE)
        chain_end = getLinkFromPositionFromTip(0, t->root_hash.bytes, t->root_hash.size);

    updatePeers(mp, t->sfpp_message_type);

    if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_CANON_ANNOUNCE) {

        if (portduino_config.sfpp_stratum0) {
            uint8_t next_commit_hash[SFPP_HASH_SIZE] = {0};
            if (getNextHash(t->root_hash.bytes, t->root_hash.size, t->commit_hash.bytes, t->commit_hash.size, next_commit_hash)) {
                printBytes("StoreForwardpp next chain hash: ", next_commit_hash, SFPP_HASH_SIZE);
                if (airTime->isTxAllowedChannelUtil(true)) {
                    LOG_INFO("StoreForwardpp Received a CANON_ANNOUNCE while stratum 0, sending next link.");
                    broadcastLink(next_commit_hash, SFPP_HASH_SIZE);
                }
            }
            return true;
        }

        uint8_t tmp_root_hash_bytes[SFPP_HASH_SIZE] = {0};

        LOG_DEBUG("StoreForwardpp Received a CANON_ANNOUNCE");
        // The pre-decode channel hash only exists on the encrypted copy; allocCopy can fail.
        if (currentEncrypted == nullptr) {
            LOG_WARN("StoreForwardpp CANON_ANNOUNCE without encrypted packet, dropping");
            return true;
        }
        if (!deriveRootFromChannel(currentEncrypted->channel, tmp_root_hash_bytes) || t->root_hash.size == 0 ||
            t->root_hash.size > SFPP_HASH_SIZE || memcmp(tmp_root_hash_bytes, t->root_hash.bytes, t->root_hash.size) != 0) {
            LOG_INFO("StoreForwardpp Root hash does not match the channel, dropping");
            return true;
        }
        addRootToMappings(currentEncrypted->channel, tmp_root_hash_bytes);

        if (t->encapsulated_rxtime == 0) {
            LOG_DEBUG("StoreForwardpp No encapsulated time, conclude the chain is empty");
            return true;
        }

        // if we have no messages, just send out a request for a new starter message
        if (chain_end.rx_time == 0 && airTime->isTxAllowedChannelUtil(true)) {
            LOG_DEBUG("StoreForwardpp New chain, requesting last %u messages", portduino_config.sfpp_initial_sync);
            requestMessageCount(t->root_hash.bytes, t->root_hash.size, portduino_config.sfpp_initial_sync);
            return true;
        }

        // If we got an announce that matches, maybe send a message from scratch, but no other actions needed.
        if (t->commit_hash.size >= SFPP_SHORT_HASH_SIZE &&
            memcmp(chain_end.commit_hash, t->commit_hash.bytes, t->commit_hash.size) == 0) {
            LOG_DEBUG("StoreForwardpp End of chain matches!");
            sendFromScratch(chain_end.root_hash);
            return true;
        }

        LOG_DEBUG("StoreForwardpp End of chain does not match! Checking distance behind.");
        int64_t links_behind = 0;
        if (t->chain_count != 0 && t->chain_count > chain_end.counter) {
            links_behind = t->chain_count - chain_end.counter;

            LOG_DEBUG("StoreForwardpp Links behind: %ld", links_behind);
            if (links_behind > portduino_config.sfpp_backlog_limit) {
                LOG_INFO("StoreForwardpp Chain behind limit, dumping DB");
                clearChain(t->root_hash.bytes, t->root_hash.size);
                clearCanonScratch(t->root_hash.bytes, t->root_hash.size, t->chain_count - portduino_config.sfpp_backlog_limit);
                return true;
            }
        }

        // We just got an end of chain announce, checking if we have seen this message and have it in scratch.
        LOG_DEBUG("Checking if in scratch");
        if (isInScratch(t->message_hash.bytes, t->message_hash.size)) {

            LOG_DEBUG("Found in scratch");
            link_object scratch_object = getFromScratch(t->message_hash.bytes, t->message_hash.size);
            // if this matches, we don't need to request the message
            // we know exactly what it is
            if (t->message_hash.size >= 8 && t->commit_hash.size >= 8 &&
                checkCommitHash(scratch_object, t->commit_hash.bytes, t->message_hash.size)) {
                LOG_INFO("StoreForwardpp Found announced message in scratch, adding to chain");
                scratch_object.rx_time = t->encapsulated_rxtime;
                if (addToChain(scratch_object))
                    links_from_scratch++;
                removeFromScratch(scratch_object.message_hash, scratch_object.message_hash_len);
                maybeMoveFromCanonScratch(scratch_object.root_hash, scratch_object.root_hash_len);

                return true;
            }

            LOG_INFO("StoreForwardpp Earliest scratch message commit hash does not match announced commit hash, "
                     "speculating chain");
            if (!speculateScratchChain(t->commit_hash.bytes, t->commit_hash.size, scratch_object.root_hash,
                                       chain_end.commit_hash)) {
                LOG_INFO("StoreForwardpp Could not speculate chain to announced commit hash. Moving from "
                         "local_messages to canon_scratch");

                scratch_object.rx_time = t->encapsulated_rxtime;
                scratch_object.counter = t->chain_count;
                scratch_object.commit_hash_len = t->commit_hash.size;
                memcpy(scratch_object.commit_hash, t->commit_hash.bytes, t->commit_hash.size);
                addToCanonScratch(scratch_object);
                removeFromScratch(scratch_object.message_hash, scratch_object.message_hash_len);
            }

        } else {
            LOG_DEBUG("StoreForwardpp Not in scratch");
        }
        if (airTime->isTxAllowedChannelUtil(true)) {
            requestNextMessage(t->root_hash.bytes, t->root_hash.size, chain_end.commit_hash, SFPP_HASH_SIZE);
        }

    } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_REQUEST) {
        uint8_t next_commit_hash[SFPP_HASH_SIZE] = {0};

        LOG_DEBUG("StoreForwardpp Received link request");

        // If chain_count is set, this is a request for x messages up the chain.
        if (t->chain_count != 0 && t->root_hash.size >= 8) {
            link_object link_from_count = getLinkFromPositionFromTip(t->chain_count, t->root_hash.bytes, t->root_hash.size);
            LOG_DEBUG("StoreForwardpp Count requested %d", t->chain_count);
            if (link_from_count.validObject)
                broadcastLink(link_from_count, true);

        } else if (getNextHash(t->root_hash.bytes, t->root_hash.size, t->commit_hash.bytes, t->commit_hash.size,
                               next_commit_hash)) {
            printBytes("StoreForwardpp sending next chain hash: ", next_commit_hash, SFPP_HASH_SIZE);

            broadcastLink(next_commit_hash, SFPP_HASH_SIZE);
        } else {
            LOG_WARN("Could not find requested link on the chain.");
        }

        // if root and chain hashes are the same, grab the first message on the chain
        // if different, get the message directly after.
    } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE ||
               t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_FIRSTHALF ||
               t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_SECONDHALF) {

        if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE) {
            LOG_DEBUG("StoreForwardpp Link Provide received!");

            // Is this already in our canon DB? If so, log and drop
            if (t->commit_hash.size >= 8 && t->message_hash.size >= 8 && isInDB(t->message_hash.bytes, t->message_hash.size)) {
                LOG_INFO("StoreForwardpp Received link already in chain1");
                return true;
            }

            incoming_link = ingestLinkMessage(t);
        } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_FIRSTHALF) {
            LOG_DEBUG("StoreForwardpp Link Provide First Half received!");
            split_link_in = ingestLinkMessage(t);
            doing_split_receive = true;
            split_link_in.validObject = true;
            return true;

        } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_SECONDHALF) {
            LOG_DEBUG("StoreForwardpp Link Provide Second Half received!");
            if (!doing_split_receive) {
                LOG_DEBUG("StoreForwardpp Received second half without first half, ignoring");
                return true;
            }
            if (!split_link_in.validObject) {
                LOG_WARN("StoreForwardpp No first half stored, cannot combine");
                doing_split_receive = false;
                return true;
            }
            link_object second_half = ingestLinkMessage(t);
            if (split_link_in.encrypted_len + second_half.encrypted_len > 256) {
                LOG_WARN("StoreForwardpp Combined link too large");
                return true;
            }

            if (split_link_in.from == second_half.from && split_link_in.to == second_half.to &&
                split_link_in.root_hash_len == second_half.root_hash_len &&
                memcmp(split_link_in.root_hash, second_half.root_hash, split_link_in.root_hash_len) == 0 &&
                split_link_in.message_hash_len == second_half.message_hash_len &&
                memcmp(split_link_in.message_hash, second_half.message_hash, split_link_in.message_hash_len) == 0) {
                incoming_link = split_link_in;
                memcpy(&incoming_link.encrypted_bytes[split_link_in.encrypted_len], second_half.encrypted_bytes,
                       second_half.encrypted_len);
                incoming_link.encrypted_len = split_link_in.encrypted_len + second_half.encrypted_len;
                split_links_received++;

                // append the encrypted bytes

                // clear first half
                split_link_in = link_object();
                split_link_in.validObject = false;
                doing_split_receive = false;
            } else {
                LOG_WARN("StoreForwardpp No first half stored, cannot combine");
                return true;
            }
        }

        recalculateMessageHash(incoming_link);

        // Incoming hash. First check if we already have it
        if (isCommitInDB(incoming_link.commit_hash, incoming_link.commit_hash_len) ||
            isInDB(incoming_link.message_hash, incoming_link.message_hash_len)) {
            // This was sent to us as a scratch message, but we already have it in the chain.
            // Respond by announcing this
            if (t->commit_hash.size == 0) {
                link_object link_to_announce = getLinkFromMessageHash(incoming_link.message_hash, incoming_link.message_hash_len);
                LOG_INFO("StoreForwardpp Received link already in chain #%u, announcing that commit hash",
                         link_to_announce.counter);
                canonAnnounce(link_to_announce);

            } else {
                LOG_INFO("StoreForwardpp Received link already in chain");
            }
            return true;
        }
        // do we want to explicitly check for blank comm hash here?

        // We have a link. Recalculate the message hash and check if the commit hash matches
        if (recalculateHash(incoming_link, t->root_hash.bytes, t->root_hash.size, t->commit_hash.bytes, t->commit_hash.size)) {
            if (incoming_link.root_hash_len == 0) {
                LOG_WARN("StoreForwardpp Hash bytes not found for incoming link");
                return true;
            }

            // This is a scratch provide that we haven't seen
            if (portduino_config.sfpp_stratum0) {

                if (t->commit_hash.size != 0) {
                    LOG_WARN("Got a link with commit hash, that we don't have on the canon chain");
                    return true;
                }

                LOG_DEBUG("StoreForwardpp Adding message to canon, received as a link provide");
                addToChain(incoming_link);
                if (!pendingRun) {
                    setIntervalFromNow(10 * 1000); // run again in 30 seconds to announce the new tip of chain
                    pendingRun = true;
                }
                // timebox to no more than an hour old
                if (incoming_link.rx_time > getValidTime(RTCQuality::RTCQualityNTP, true) - rebroadcastTimeout) {
                    // if this packet is new to us, we rebroadcast it
                    rebroadcastLinkObject(incoming_link);
                } else {
                    LOG_DEBUG("StoreForwardpp Got previously unseen text, but not rebroadcasting because rxtime was %u",
                              incoming_link.rx_time);
                }
                return true;
            }

            // Valid hash, and we're not stratum0

            // has a commit hash, so on the canon chain
            if (incoming_link.commit_hash_len == SFPP_HASH_SIZE) {
                addToChain(incoming_link);
                if (isInScratch(incoming_link.message_hash, incoming_link.message_hash_len)) {
                    link_object scratch_object = getFromScratch(incoming_link.message_hash, incoming_link.message_hash_len);
                    if (scratch_object.payload != "") {
                        updatePayload(incoming_link.message_hash, incoming_link.message_hash_len, scratch_object.payload);
                    }
                    removeFromScratch(incoming_link.message_hash, incoming_link.message_hash_len);
                } else {
                    // if this packet is new to us, we rebroadcast it, but only up to an hour old
                    if (incoming_link.rx_time > getValidTime(RTCQuality::RTCQualityNTP, true) - rebroadcastTimeout) {
                        rebroadcastLinkObject(incoming_link);
                    } else {
                        LOG_DEBUG("StoreForwardpp Got previously unseen text, but not rebroadcasting because rxtime was %u",
                                  incoming_link.rx_time);
                    }
                }
                maybeMoveFromCanonScratch(incoming_link.root_hash, incoming_link.root_hash_len);

                if (chain_end.rx_time != 0) {
                    int64_t links_behind = 0;
                    if (t->chain_count != 0 && t->chain_count > chain_end.counter) {
                        links_behind = t->chain_count - chain_end.counter;

                        if (links_behind > 1)
                            LOG_DEBUG("StoreForwardpp observed link that is links ahead of us: %ld", links_behind);
                        if (links_behind > portduino_config.sfpp_backlog_limit) {
                            LOG_INFO("StoreForwardpp Chain behind limit, dumping DB");
                            clearChain(t->root_hash.bytes, t->root_hash.size);
                            clearCanonScratch(t->root_hash.bytes, t->root_hash.size,
                                              t->chain_count - portduino_config.sfpp_backlog_limit);
                            return true;
                        }
                    }
                }
                requestNextMessage(incoming_link.root_hash, incoming_link.root_hash_len, incoming_link.commit_hash,
                                   incoming_link.commit_hash_len);

                // possibly not on the canon hash
            } else {
                if (!isInScratch(incoming_link.message_hash, incoming_link.message_hash_len) &&
                    !isInDB(incoming_link.message_hash, incoming_link.message_hash_len) &&
                    !isInCanonScratch(incoming_link.message_hash, incoming_link.message_hash_len)) {
                    addToScratch(incoming_link);
                    LOG_INFO("StoreForwardpp added incoming non-canon message to scratch");
                    if (incoming_link.rx_time > getValidTime(RTCQuality::RTCQualityNTP, true) - rebroadcastTimeout) {
                        rebroadcastLinkObject(incoming_link);
                    } else {
                        LOG_DEBUG("StoreForwardpp Got previously unseen text, but not rebroadcasting because rxtime was &u",
                                  incoming_link.rx_time);
                    }
                }
            }

        } else {
            LOG_INFO("StoreForwardpp Recalculated hash does not match.");
            if (portduino_config.sfpp_stratum0)
                return true;

            if (incoming_link.commit_hash_len == 0) {
                if (!isInScratch(incoming_link.message_hash, incoming_link.message_hash_len))
                    addToScratch(incoming_link);
            } else if (incoming_link.commit_hash_len == SFPP_HASH_SIZE && chain_end.counter == 0) {
                addToChain(incoming_link);
            } else if (incoming_link.counter != 0 &&
                       !isInCanonScratch(incoming_link.message_hash, incoming_link.message_hash_len)) {

                // We've received a link provide, and it doesn't fit. But it may be legit. Add it to canon_scratch
                addToCanonScratch(incoming_link);
            }
        }
    }

    return true;
}

bool StoreForwardPlusPlusModule::legacyStoreForwardServes()
{
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
    return storeForwardModule && storeForwardModule->isServer();
#else
    return false;
#endif
}

bool StoreForwardPlusPlusModule::consumeServeBudget()
{
    const uint32_t now = Time::getMillis();
    while (!serveWindowSends.empty() && Throttle::hasElapsed(serveWindowSends.front(), kServeWindowMs))
        serveWindowSends.pop_front();
    if (serveWindowSends.size() >= kServeWindowFrames)
        return false;
    serveWindowSends.push_back(now);
    return true;
}

ErrorCode StoreForwardPlusPlusModule::sendSfppPacket(meshtastic_MeshPacket *p, RxSource src, bool ccToPhone)
{
    if (portduino_config.sfpp_enum_listen_only) {
        if (p)
            packetPool.release(p);
        return ERRNO_DISABLED;
    }
    return service->sendToMesh(p, src, ccToPhone);
}

void StoreForwardPlusPlusModule::sendLegacyReply(NodeNum to, const meshtastic_StoreAndForward &payload)
{
    meshtastic_MeshPacket *p = allocStoreForwardPacket(payload);
    if (!p)
        return;

    p->to = to;
    p->channel = 0;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = false;
    p->decoded.want_response = false;
    sendSfppPacket(p, RX_SRC_LOCAL, true);
}

void StoreForwardPlusPlusModule::handleLegacyClientHistory(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *req)
{
    constexpr uint32_t retentionS = 48 * 60 * 60; // D7
    constexpr uint32_t pageMax = 50;

    if (req->rr != meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY)
        return;

    NodeNum requester = getFrom(&mp);

    if (legacyHistoryBusy) {
        meshtastic_StoreAndForward busy = meshtastic_StoreAndForward_init_zero;
        busy.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY;
        sendLegacyReply(requester, busy);
        return;
    }

    uint32_t windowS = retentionS;
    if (req->which_variant == meshtastic_StoreAndForward_history_tag && req->variant.history.window > 0) {
        uint32_t requested = req->variant.history.window * 60; // the legacy request carries minutes
        if (requested < windowS)
            windowS = requested;
    }

    uint32_t now = getValidTime(RTCQualityNTP, true);
    uint32_t since = now > windowS ? now - windowS : 0;
    uint32_t cursor = 0;
    if (req->which_variant == meshtastic_StoreAndForward_history_tag)
        cursor = req->variant.history.last_request;
    uint32_t lastServedRxTime = cursor;
    uint32_t count = 0;

    uint8_t root_hash[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root_hash) != 0) {
        sqlite3_reset(legacyHistoryStmt);
        sqlite3_bind_int(legacyHistoryStmt, 1, SFPP_HASH_SIZE);
        sqlite3_bind_blob(legacyHistoryStmt, 2, root_hash, SFPP_HASH_SIZE, NULL);
        sqlite3_bind_int64(legacyHistoryStmt, 3, (sqlite3_int64)since);
        sqlite3_bind_int64(legacyHistoryStmt, 4, (sqlite3_int64)cursor);
        sqlite3_bind_int64(legacyHistoryStmt, 5, (sqlite3_int64)requester);
        sqlite3_bind_int64(legacyHistoryStmt, 6, (sqlite3_int64)NODENUM_BROADCAST);
        sqlite3_bind_int64(legacyHistoryStmt, 7, (sqlite3_int64)requester);
        sqlite3_bind_int(legacyHistoryStmt, 8, pageMax);

        while (sqlite3_step(legacyHistoryStmt) == SQLITE_ROW) {
            legacy_history_item item;
            item.from = (NodeNum)sqlite3_column_int64(legacyHistoryStmt, 0);
            item.id = (PacketId)sqlite3_column_int64(legacyHistoryStmt, 1);
            uint32_t destination = (uint32_t)sqlite3_column_int64(legacyHistoryStmt, 2);
            item.rx_time = (uint32_t)sqlite3_column_int64(legacyHistoryStmt, 3);
            if (item.rx_time > lastServedRxTime)
                lastServedRxTime = item.rx_time;
            const unsigned char *text = sqlite3_column_text(legacyHistoryStmt, 4);
            if (text)
                item.payload.assign((const char *)text, sqlite3_column_bytes(legacyHistoryStmt, 4));
            if (item.payload.empty()) {
                const uint8_t *encrypted = (const uint8_t *)sqlite3_column_blob(legacyHistoryStmt, 5);
                const int encryptedLen = sqlite3_column_bytes(legacyHistoryStmt, 5);
                if (!decodeStoredText(encrypted, (size_t)encryptedLen, destination, item.from, item.id, item.payload))
                    continue;
            }
            item.to = requester;
            item.broadcast = destination == NODENUM_BROADCAST;
            legacyHistory.push_back(std::move(item));
            count++;
        }
        sqlite3_reset(legacyHistoryStmt);
    }

    meshtastic_StoreAndForward response = meshtastic_StoreAndForward_init_zero;
    response.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY;
    response.which_variant = meshtastic_StoreAndForward_history_tag;
    response.variant.history.history_messages = count;
    response.variant.history.window = windowS * 1000;
    response.variant.history.last_request = lastServedRxTime;
    sendLegacyReply(requester, response);

    if (count > 0) {
        legacyHistoryBusy = true;
        setIntervalFromNow(5 * 1000);
    }
}

bool StoreForwardPlusPlusModule::sendNextLegacyHistory()
{
    if (legacyHistory.empty()) {
        legacyHistoryBusy = false;
        return false;
    }
    if (!consumeServeBudget())
        return false;

    legacy_history_item item = std::move(legacyHistory.front());
    legacyHistory.pop_front();

    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = item.broadcast ? meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST
                           : meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT;
    sf.which_variant = meshtastic_StoreAndForward_text_tag;
    sf.original_id = item.id;
    size_t len = item.payload.size();
    if (len > sizeof(sf.variant.text.bytes))
        len = sizeof(sf.variant.text.bytes);
    memcpy(sf.variant.text.bytes, item.payload.data(), len);
    sf.variant.text.size = len;

    meshtastic_MeshPacket *p = allocStoreForwardPacket(sf);
    if (p) {
        p->to = item.to;
        p->from = item.from;
        p->channel = 0;
        p->rx_time = item.rx_time;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->want_ack = false;
        p->decoded.want_response = false;
        sendSfppPacket(p, RX_SRC_LOCAL, true);
    }

    if (legacyHistory.empty())
        legacyHistoryBusy = false;
    return true;
}

ChannelHash StoreForwardPlusPlusModule::getChannelHashFromRoot(uint8_t *_root_hash, size_t _root_hash_len)
{
    sqlite3_bind_int(getHashFromRootStmt, 1, _root_hash_len);
    sqlite3_bind_blob(getHashFromRootStmt, 2, _root_hash, _root_hash_len, NULL);
    sqlite3_step(getHashFromRootStmt);
    ChannelHash tmp_hash = (ChannelHash)sqlite3_column_int(getHashFromRootStmt, 0);
    sqlite3_reset(getHashFromRootStmt);
    return tmp_hash;
}

// A root every archive on a channel derives identically: channel name and key, no node number and no
// clock. The version tag makes a future rule change a clean break.
bool StoreForwardPlusPlusModule::deriveRootFromChannel(ChannelHash _ch_hash, uint8_t *_root_hash)
{
    // The module is channel-0 only; a packet on another channel must not resolve to this root.
    const int16_t primaryHash = channels.getHash(0);
    if (primaryHash < 0 || static_cast<uint8_t>(primaryHash) != _ch_hash)
        return false;

    CryptoKey key = channels.getKey(0);
    if (key.length < 0)
        return false;

    const char *name = channels.getName(0);
    const std::string channelName = name ? name : "";
    const uint32_t keyLength = static_cast<uint32_t>(key.length);

    static constexpr char kRootDomain[] = "msh-sfpp-root-v1";
    SHA256 root;
    root.update(kRootDomain, sizeof(kRootDomain) - 1);

    const uint8_t hashByte = static_cast<uint8_t>(_ch_hash);
    root.update(&hashByte, sizeof(hashByte));

    uint8_t lengthBytes[4];
    auto updateLength = [&](uint32_t length) {
        lengthBytes[0] = length & 0xff;
        lengthBytes[1] = (length >> 8) & 0xff;
        lengthBytes[2] = (length >> 16) & 0xff;
        lengthBytes[3] = (length >> 24) & 0xff;
        root.update(lengthBytes, sizeof(lengthBytes));
    };

    updateLength(static_cast<uint32_t>(channelName.size()));
    if (!channelName.empty())
        root.update(channelName.data(), channelName.size());
    updateLength(keyLength);
    if (keyLength > 0)
        root.update(key.bytes, keyLength);

    root.finalize(_root_hash, SFPP_HASH_SIZE);
    return true;
}

// Returns the derived root, adding it on first use. The mapping is keyed by the root, so a second
// channel that happens to share the 1-byte hash keeps its own row instead of replacing this one.
size_t StoreForwardPlusPlusModule::getOrDeriveRootFromChannel(ChannelHash _ch_hash, uint8_t *_root_hash)
{
    uint8_t derived[SFPP_HASH_SIZE] = {0};
    if (!deriveRootFromChannel(_ch_hash, derived))
        return 0;

    uint8_t mapped[SFPP_HASH_SIZE] = {0};
    if (!lookUpFullRootHash(derived, SFPP_HASH_SIZE, mapped)) {
        LOG_INFO("StoreForwardpp Deriving root hash from the channel");
        addRootToMappings(_ch_hash, derived);
    }

    memcpy(_root_hash, derived, SFPP_HASH_SIZE);
    return SFPP_HASH_SIZE;
}

void StoreForwardPlusPlusModule::addRootToMappings(ChannelHash _ch_hash, uint8_t *_root_hash)
{
    int type = chain_types::channel_chain;
    sqlite3_bind_int(addRootToMappingsStmt, 1, type);
    sqlite3_bind_int(addRootToMappingsStmt, 2, _ch_hash);
    sqlite3_bind_blob(addRootToMappingsStmt, 3, _root_hash, SFPP_HASH_SIZE, NULL);
    auto rc = sqlite3_step(addRootToMappingsStmt);
    LOG_WARN("StoreForwardpp result %u, %s", rc, sqlite3_errmsg(ppDb));
    sqlite3_reset(addRootToMappingsStmt);
}

// TODO: make DM?
void StoreForwardPlusPlusModule::requestNextMessage(uint8_t *_root_hash, size_t _root_hash_len, uint8_t *_commit_hash,
                                                    size_t _commit_hash_len)
{
    links_requested++;

    meshtastic_StoreForwardPlusPlus storeforward = meshtastic_StoreForwardPlusPlus_init_zero;
    storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_REQUEST;
    // set root hash

    // set chain hash
    storeforward.commit_hash.size = _commit_hash_len;
    memcpy(storeforward.commit_hash.bytes, _commit_hash, _commit_hash_len);

    // set root hash
    storeforward.root_hash.size = _root_hash_len;
    memcpy(storeforward.root_hash.bytes, _root_hash, _root_hash_len);

    // storeforward.
    meshtastic_MeshPacket *p = allocDataProtobuf(storeforward);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->channel = 0;
    p->hop_limit = portduino_config.sfpp_hops;
    p->hop_start = portduino_config.sfpp_hops;
    sendSfppPacket(p, RX_SRC_LOCAL, true);
}

void StoreForwardPlusPlusModule::requestMessageCount(uint8_t *_root_hash, size_t _root_hash_len, uint32_t count)
{
    links_requested++;

    meshtastic_StoreForwardPlusPlus storeforward = meshtastic_StoreForwardPlusPlus_init_zero;
    storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_REQUEST;
    // set root hash

    storeforward.chain_count = count;

    // set root hash
    storeforward.root_hash.size = _root_hash_len;
    memcpy(storeforward.root_hash.bytes, _root_hash, _root_hash_len);

    // storeforward.
    meshtastic_MeshPacket *p = allocDataProtobuf(storeforward);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->channel = 0;
    p->hop_limit = portduino_config.sfpp_hops;
    p->hop_start = portduino_config.sfpp_hops;
    sendSfppPacket(p, RX_SRC_LOCAL, true);
}

bool StoreForwardPlusPlusModule::getNextHash(uint8_t *_root_hash, size_t _root_hash_len, uint8_t *_commit_hash,
                                             size_t _commit_hash_len, uint8_t *next_commit_hash)
{
    int rc;
    sqlite3_bind_int(getNextHashStmt, 1, _root_hash_len);
    sqlite3_bind_blob(getNextHashStmt, 2, _root_hash, _root_hash_len, NULL);
    bool next_hash = false;

    // asking for the first entry on the chain
    if (memcmp(_root_hash, _commit_hash, _commit_hash_len) == 0) {
        rc = sqlite3_step(getNextHashStmt);
        if (rc != SQLITE_OK) {
            LOG_WARN("StoreForwardpp Get Hash error %u, %s", rc, sqlite3_errmsg(ppDb));
        }
        uint8_t *tmp_commit_hash = (uint8_t *)sqlite3_column_blob(getNextHashStmt, 0);
        if (tmp_commit_hash == nullptr) {
            sqlite3_reset(getNextHashStmt);
            return false;
        }
        printBytes("StoreForwardpp commit_hash", tmp_commit_hash, SFPP_HASH_SIZE);
        memcpy(next_commit_hash, tmp_commit_hash, SFPP_HASH_SIZE);
        next_hash = true;
    } else {
        bool found_hash = false;

        uint8_t *tmp_commit_hash;
        while (sqlite3_step(getNextHashStmt) != SQLITE_DONE) {
            tmp_commit_hash = (uint8_t *)sqlite3_column_blob(getNextHashStmt, 0);

            if (found_hash) {
                memcpy(next_commit_hash, tmp_commit_hash, SFPP_HASH_SIZE);
                next_hash = true;
                break;
            }
            if (memcmp(tmp_commit_hash, _commit_hash, _commit_hash_len) == 0)
                found_hash = true;
        }
    }

    sqlite3_reset(getNextHashStmt);
    return next_hash;
}

void StoreForwardPlusPlusModule::broadcastLink(uint8_t *_commit_hash, size_t _commit_hash_len)
{
    sqlite3_bind_int(getLinkStmt, 1, _commit_hash_len);
    sqlite3_bind_blob(getLinkStmt, 2, _commit_hash, _commit_hash_len, NULL);
    int res = sqlite3_step(getLinkStmt);
    link_object lo;

    lo.to = sqlite3_column_int(getLinkStmt, 0);
    lo.from = sqlite3_column_int(getLinkStmt, 1);
    lo.id = sqlite3_column_int(getLinkStmt, 2);

    uint8_t *_payload = (uint8_t *)sqlite3_column_blob(getLinkStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(getLinkStmt, 3);
    memcpy(lo.encrypted_bytes, _payload, lo.encrypted_len);

    uint8_t *_message_hash = (uint8_t *)sqlite3_column_blob(getLinkStmt, 4);
    lo.message_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.message_hash, _message_hash, lo.message_hash_len);

    lo.rx_time = sqlite3_column_int(getLinkStmt, 5);

    uint8_t *_tmp_commit_hash = (uint8_t *)sqlite3_column_blob(getLinkStmt, 6);

    lo.commit_hash_len = 8;
    memcpy(lo.commit_hash, _tmp_commit_hash, lo.commit_hash_len);

    uint8_t *_root_hash = (uint8_t *)sqlite3_column_blob(getLinkStmt, 7);

    lo.root_hash_len = 8;
    memcpy(lo.root_hash, _root_hash, lo.root_hash_len);
    lo.counter = sqlite3_column_int(getLinkStmt, 8);

    sqlite3_reset(getLinkStmt);

    LOG_INFO("StoreForwardpp Send link to mesh");
    broadcastLink(lo, false);
}

void StoreForwardPlusPlusModule::broadcastLink(link_object &lo, bool full_commit_hash, bool is_split_second_half)
{
    links_provided++;
    LOG_DEBUG("Sending link #%u", lo.counter);
    meshtastic_StoreForwardPlusPlus storeforward = meshtastic_StoreForwardPlusPlus_init_zero;
    storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE;
    if (lo.encrypted_len > 180) {
        LOG_WARN("StoreForwardpp Link too large to send (%u bytes)", lo.encrypted_len);
        doing_split_send = true;
        storeforward.message_hash.size = SFPP_SHORT_HASH_SIZE;
        memcpy(storeforward.message_hash.bytes, lo.message_hash, storeforward.message_hash.size);
        link_object full_link = lo;
        split_link_out = lo;
        size_t half_size = lo.encrypted_len / 2;
        storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_FIRSTHALF;
        lo.encrypted_len = half_size;
        split_link_out.encrypted_len = full_link.encrypted_len - half_size;
        memcpy(split_link_out.encrypted_bytes, &full_link.encrypted_bytes[half_size], split_link_out.encrypted_len);
        setIntervalFromNow(30 * 1000); // send second half in 30 seconds

    } else if (is_split_second_half) {
        storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_SECONDHALF;
        storeforward.message_hash.size = SFPP_SHORT_HASH_SIZE;
        memcpy(storeforward.message_hash.bytes, lo.message_hash, storeforward.message_hash.size);
        doing_split_send = false;
        split_links_sent++;
    }

    storeforward.encapsulated_to = lo.to;
    if (storeforward.encapsulated_to == NODENUM_BROADCAST) {
        storeforward.encapsulated_to = 0;
    }
    storeforward.encapsulated_from = lo.from;
    storeforward.encapsulated_id = lo.id;

    storeforward.message.size = lo.encrypted_len;
    memcpy(storeforward.message.bytes, lo.encrypted_bytes, storeforward.message.size);

    storeforward.encapsulated_rxtime = lo.rx_time;
    storeforward.chain_count = lo.counter;

    if (lo.commit_hash_len >= 8) {
        // If we're sending a first link to a remote, that isn't actually the first on the chain
        // it needs the full commit hash, as it can't regenerate it.
        if (full_commit_hash)
            storeforward.commit_hash.size = lo.commit_hash_len;
        else
            storeforward.commit_hash.size = SFPP_SHORT_HASH_SIZE;
        memcpy(storeforward.commit_hash.bytes, lo.commit_hash, storeforward.commit_hash.size);
    }

    storeforward.root_hash.size = SFPP_SHORT_HASH_SIZE;
    memcpy(storeforward.root_hash.bytes, lo.root_hash, storeforward.root_hash.size);

    sqlite3_reset(getLinkStmt);

    meshtastic_MeshPacket *p = allocDataProtobuf(storeforward);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->channel = 0;
    p->hop_limit = portduino_config.sfpp_hops;
    p->hop_start = portduino_config.sfpp_hops;
    LOG_INFO("StoreForwardpp Send link to mesh");
    sendSfppPacket(p, RX_SRC_LOCAL, true);
}

//
StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::getLink(uint8_t *_commit_hash, size_t _commit_hash_len)
{
    link_object lo;
    sqlite3_bind_int(getLinkStmt, 1, _commit_hash_len);
    sqlite3_bind_blob(getLinkStmt, 2, _commit_hash, _commit_hash_len, NULL);
    int res = sqlite3_step(getLinkStmt);

    lo.to = sqlite3_column_int(getLinkStmt, 0);
    lo.from = sqlite3_column_int(getLinkStmt, 1);
    lo.id = sqlite3_column_int(getLinkStmt, 2);

    uint8_t *_payload = (uint8_t *)sqlite3_column_blob(getLinkStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(getLinkStmt, 3);
    memcpy(lo.encrypted_bytes, _payload, lo.encrypted_len);

    uint8_t *_message_hash = (uint8_t *)sqlite3_column_blob(getLinkStmt, 4);
    lo.message_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.message_hash, _message_hash, lo.message_hash_len);

    lo.rx_time = sqlite3_column_int(getLinkStmt, 5);

    uint8_t *_tmp_commit_hash = (uint8_t *)sqlite3_column_blob(getLinkStmt, 6);
    lo.commit_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.commit_hash, _tmp_commit_hash, lo.commit_hash_len);

    uint8_t *_root_hash = (uint8_t *)sqlite3_column_blob(getLinkStmt, 7);
    lo.root_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.root_hash, _root_hash, lo.root_hash_len);

    lo.counter = sqlite3_column_int(getLinkStmt, 8);

    lo.payload = std::string((char *)sqlite3_column_text(getLinkStmt, 9));

    lo.channel_hash = getChannelHashFromRoot(lo.root_hash, lo.root_hash_len);

    sqlite3_reset(getLinkStmt);

    return lo;
}

StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::getLinkFromMessageHash(uint8_t *_message_hash,
                                                                                           size_t _message_hash_len)
{
    link_object lo;
    sqlite3_bind_int(getLinkFromMessageHashStmt, 1, _message_hash_len);
    sqlite3_bind_blob(getLinkFromMessageHashStmt, 2, _message_hash, _message_hash_len, NULL);
    int res = sqlite3_step(getLinkFromMessageHashStmt);

    lo.to = sqlite3_column_int(getLinkFromMessageHashStmt, 0);
    lo.from = sqlite3_column_int(getLinkFromMessageHashStmt, 1);
    lo.id = sqlite3_column_int(getLinkFromMessageHashStmt, 2);

    uint8_t *_payload = (uint8_t *)sqlite3_column_blob(getLinkFromMessageHashStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(getLinkFromMessageHashStmt, 3);
    memcpy(lo.encrypted_bytes, _payload, lo.encrypted_len);

    uint8_t *link_message_hash = (uint8_t *)sqlite3_column_blob(getLinkFromMessageHashStmt, 4);
    lo.message_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.message_hash, link_message_hash, lo.message_hash_len);

    lo.rx_time = sqlite3_column_int(getLinkFromMessageHashStmt, 5);

    uint8_t *_tmp_commit_hash = (uint8_t *)sqlite3_column_blob(getLinkFromMessageHashStmt, 6);
    lo.commit_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.commit_hash, _tmp_commit_hash, lo.commit_hash_len);

    uint8_t *_root_hash = (uint8_t *)sqlite3_column_blob(getLinkFromMessageHashStmt, 7);
    lo.root_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.root_hash, _root_hash, lo.root_hash_len);

    lo.counter = sqlite3_column_int(getLinkFromMessageHashStmt, 8);

    lo.payload = std::string((char *)sqlite3_column_text(getLinkFromMessageHashStmt, 9));

    lo.channel_hash = getChannelHashFromRoot(lo.root_hash, lo.root_hash_len);

    sqlite3_reset(getLinkFromMessageHashStmt);

    return lo;
}

bool StoreForwardPlusPlusModule::sendFromScratch(uint8_t *root_hash)
{
    link_object lo = getNextScratchObject(root_hash);
    if (lo.validObject) {
        printBytes("StoreForwardpp Send link to mesh ", lo.message_hash, 8);
        LOG_WARN("StoreForwardpp Size: %d", lo.encrypted_len);
        printBytes("StoreForwardpp encrypted ", lo.encrypted_bytes, lo.encrypted_len);
        sent_from_scratch++;
        broadcastLink(lo, false);

        return true;
    } else {
        LOG_INFO("StoreForwardpp No messages in scratch for this root hash");
        return false;
    }
}

bool StoreForwardPlusPlusModule::addToChain(link_object &lo)
{
    uint8_t tmp_commit_hash[SFPP_HASH_SIZE];
    link_object chain_end = getLinkFromPositionFromTip(0, lo.root_hash, lo.root_hash_len);

    // we may need to calculate the full commit hash at this point
    SHA256 commit_hash;

    commit_hash.reset();

    if (chain_end.commit_hash_len == SFPP_HASH_SIZE) {
        printBytes("StoreForwardpp last message: 0x", chain_end.commit_hash, SFPP_HASH_SIZE);
        commit_hash.update(chain_end.commit_hash, SFPP_HASH_SIZE);
    } else {
        printBytes("StoreForwardpp new chain root: 0x", lo.root_hash, SFPP_HASH_SIZE);
        commit_hash.update(lo.root_hash, SFPP_HASH_SIZE);
    }

    commit_hash.update(lo.message_hash, SFPP_HASH_SIZE);
    commit_hash.finalize(tmp_commit_hash, SFPP_HASH_SIZE);

    if (chain_end.validObject && lo.commit_hash_len >= SFPP_SHORT_HASH_SIZE &&
        memcmp(tmp_commit_hash, lo.commit_hash, lo.commit_hash_len) != 0) {

        LOG_WARN("StoreForwardpp Commit hash mismatch");
        logLinkObject(lo);
        if (chain_end.validObject)
            return false;
        LOG_INFO("Commiting anyway due to empty chain");
    }
    if (lo.commit_hash_len < SFPP_HASH_SIZE) {
        memcpy(lo.commit_hash, tmp_commit_hash, SFPP_HASH_SIZE);
        lo.commit_hash_len = SFPP_HASH_SIZE;
    }

    // if we get an official counter, use it. Otherwise, just increment.
    if (lo.counter == 0) {
        lo.counter = chain_end.counter + 1;
    }
    LOG_DEBUG("Adding link %u to Canon Chain", lo.counter);

    // push a message into the local chain DB
    // destination
    sqlite3_bind_int64(chain_insert_stmt, 1, (sqlite3_int64)lo.to);
    // sender
    sqlite3_bind_int64(chain_insert_stmt, 2, (sqlite3_int64)lo.from);
    // packet_id
    sqlite3_bind_int64(chain_insert_stmt, 3, (sqlite3_int64)lo.id);
    // root_hash
    sqlite3_bind_blob(chain_insert_stmt, 4, lo.root_hash, SFPP_HASH_SIZE, NULL);
    // encrypted_bytes
    sqlite3_bind_blob(chain_insert_stmt, 5, lo.encrypted_bytes, lo.encrypted_len, NULL);
    // message_hash
    sqlite3_bind_blob(chain_insert_stmt, 6, lo.message_hash, SFPP_HASH_SIZE, NULL);
    // rx_time
    sqlite3_bind_int(chain_insert_stmt, 7, lo.rx_time);
    // commit_hash
    sqlite3_bind_blob(chain_insert_stmt, 8, lo.commit_hash, SFPP_HASH_SIZE, NULL);
    // payload
    sqlite3_bind_text(chain_insert_stmt, 9, lo.payload.c_str(), lo.payload.length(), NULL);

    sqlite3_bind_int(chain_insert_stmt, 10, lo.counter);
    // short_id and checksum, derived once here rather than on every advert
    sqlite3_bind_int64(chain_insert_stmt, 11, sfppsr::shortId(lo.message_hash, lo.message_hash_len));
    sqlite3_bind_int64(chain_insert_stmt, 12, (sqlite3_int64)sfppsr::checksumContribution(lo.message_hash, lo.message_hash_len));
    int res = sqlite3_step(chain_insert_stmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp Cannot step: %s", sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(chain_insert_stmt);
    setChainCount(lo.root_hash, SFPP_HASH_SIZE, lo.counter);
    links_added++;
    removeFromCanonScratch(lo.message_hash, SFPP_SHORT_HASH_SIZE);
    return true;
}

bool StoreForwardPlusPlusModule::addToScratch(link_object &lo)
{
    // push a message into the local chain DB
    // destination
    sqlite3_bind_int64(scratch_insert_stmt, 1, (sqlite3_int64)lo.to);
    // sender
    sqlite3_bind_int64(scratch_insert_stmt, 2, (sqlite3_int64)lo.from);
    // packet_id
    sqlite3_bind_int64(scratch_insert_stmt, 3, (sqlite3_int64)lo.id);
    // root_hash
    sqlite3_bind_blob(scratch_insert_stmt, 4, lo.root_hash, SFPP_HASH_SIZE, NULL);
    // encrypted_bytes
    sqlite3_bind_blob(scratch_insert_stmt, 5, lo.encrypted_bytes, lo.encrypted_len, NULL);
    // message_hash
    sqlite3_bind_blob(scratch_insert_stmt, 6, lo.message_hash, SFPP_HASH_SIZE, NULL);
    // rx_time
    sqlite3_bind_int(scratch_insert_stmt, 7, lo.rx_time);
    // payload
    sqlite3_bind_text(scratch_insert_stmt, 8, lo.payload.c_str(), lo.payload.length(), NULL);

    int res = sqlite3_step(scratch_insert_stmt);
    if (res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(ppDb);
        LOG_WARN("StoreForwardpp step %u, %s", res, _error_mesg);
    }
    sqlite3_reset(scratch_insert_stmt);
    return true;
}

void StoreForwardPlusPlusModule::canonAnnounce(link_object &lo)
{
    meshtastic_StoreForwardPlusPlus storeforward = meshtastic_StoreForwardPlusPlus_init_zero;
    storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_CANON_ANNOUNCE;
    // set root hash

    // set message hash
    if (lo.message_hash_len < 8) {
        LOG_WARN("Attempt canonAnnounce without message hash");
        return;
    }
    storeforward.message_hash.size = 8;
    memcpy(storeforward.message_hash.bytes, lo.message_hash, 8);

    // set chain hash
    if (lo.commit_hash_len < 8) {
        LOG_WARN("Attempt canonAnnounce without commit hash");
        return;
    }
    storeforward.commit_hash.size = 8;
    memcpy(storeforward.commit_hash.bytes, lo.commit_hash, 8);

    // set root hash
    // needs to be the full hash to bootstrap
    if (lo.root_hash_len < SFPP_HASH_SIZE) {
        LOG_WARN("Attempt canonAnnounce without root hash");
        return;
    }
    storeforward.root_hash.size = SFPP_HASH_SIZE;
    memcpy(storeforward.root_hash.bytes, lo.root_hash, SFPP_HASH_SIZE);

    storeforward.encapsulated_rxtime = lo.rx_time;
    storeforward.chain_count = lo.counter;
    // storeforward.
    meshtastic_MeshPacket *p = allocDataProtobuf(storeforward);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->channel = 0;
    p->hop_limit = portduino_config.sfpp_hops;
    p->hop_start = portduino_config.sfpp_hops;
    LOG_INFO("StoreForwardpp Send packet to mesh payload size %u", p->decoded.payload.size);
    canon_announces++;
    sendSfppPacket(p, RX_SRC_LOCAL, true);
}

bool StoreForwardPlusPlusModule::isInDB(const uint8_t *message_hash_bytes, size_t message_hash_len)
{
    if (message_hash_len < SFPP_SHORT_HASH_SIZE)
        return false;
    sqlite3_bind_int(checkDupMessageHash, 1, message_hash_len);
    sqlite3_bind_blob(checkDupMessageHash, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(checkDupMessageHash);
    int numberFound = sqlite3_column_int(checkDupMessageHash, 0);
    sqlite3_reset(checkDupMessageHash);
    if (numberFound > 0)
        return true;
    return false;
}

bool StoreForwardPlusPlusModule::isCommitInDB(uint8_t *commit_hash_bytes, size_t commit_hash_len)
{
    if (commit_hash_len < SFPP_SHORT_HASH_SIZE)
        return false;
    sqlite3_bind_int(checkDupCommitHash, 1, commit_hash_len);
    sqlite3_bind_blob(checkDupCommitHash, 2, commit_hash_bytes, commit_hash_len, NULL);
    sqlite3_step(checkDupCommitHash);
    int numberFound = sqlite3_column_int(checkDupCommitHash, 0);
    sqlite3_reset(checkDupCommitHash);
    if (numberFound > 0)
        return true;
    return false;
}

bool StoreForwardPlusPlusModule::isInScratch(uint8_t *message_hash_bytes, size_t message_hash_len)
{
    if (message_hash_len < SFPP_SHORT_HASH_SIZE)
        return false;
    sqlite3_bind_int(checkScratch, 1, message_hash_len);
    sqlite3_bind_blob(checkScratch, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(checkScratch);
    int numberFound = sqlite3_column_int(checkScratch, 0);
    sqlite3_reset(checkScratch);
    if (numberFound > 0)
        return true;
    return false;
}

void StoreForwardPlusPlusModule::removeFromScratch(const uint8_t *message_hash_bytes, size_t message_hash_len)
{
    printBytes("StoreForwardpp removing from scratch: ", message_hash_bytes, message_hash_len);
    sqlite3_bind_int(removeScratch, 1, message_hash_len);
    sqlite3_bind_blob(removeScratch, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(removeScratch);
    sqlite3_reset(removeScratch);
}

bool StoreForwardPlusPlusModule::speculateScratchChain(uint8_t *commit_hash_bytes, size_t commit_hash_len, uint8_t *root_hash,
                                                       uint8_t *current_commit_hash)
{
    bool found = false;
    int count = 0;
    uint32_t tmp_to = 0;
    u_int32_t tmp_from = 0;
    uint32_t tmp_id = 0;
    uint8_t *tmp_encrypted = nullptr;
    uint8_t *tmp_message_hash = nullptr;
    uint32_t tmp_rx_time = 0;
    SHA256 commit_hash_obj;
    uint8_t tmp_commit_hash[SFPP_HASH_SIZE];

    if (commit_hash_len < SFPP_SHORT_HASH_SIZE) {
        return false;
    }

    memcpy(tmp_commit_hash, current_commit_hash, SFPP_HASH_SIZE);

    sqlite3_bind_blob(fromScratchStmt, 1, root_hash, SFPP_HASH_SIZE, NULL);
    while (sqlite3_step(fromScratchStmt) == SQLITE_ROW) {
        count++;
        tmp_to = sqlite3_column_int(fromScratchStmt, 0);
        tmp_from = sqlite3_column_int(fromScratchStmt, 1);
        tmp_id = sqlite3_column_int(fromScratchStmt, 2);
        tmp_encrypted = (uint8_t *)sqlite3_column_blob(fromScratchStmt, 3);
        size_t encrypted_len = sqlite3_column_bytes(fromScratchStmt, 3);
        tmp_message_hash = (uint8_t *)sqlite3_column_blob(fromScratchStmt, 4);
        tmp_rx_time = sqlite3_column_int(fromScratchStmt, 5);
        // calculate the commit hash for that object
        commit_hash_obj.reset();
        commit_hash_obj.update(tmp_commit_hash, SFPP_HASH_SIZE);
        commit_hash_obj.update(tmp_message_hash, SFPP_HASH_SIZE);
        commit_hash_obj.finalize(tmp_commit_hash, SFPP_HASH_SIZE);
        if (memcmp(tmp_commit_hash, commit_hash_bytes, commit_hash_len) == 0) {
            // found it
            LOG_INFO("StoreForwardpp found next scratch object in chain after %d tries", count);
            sqlite3_reset(fromScratchStmt);
            found = true;
            break;
        }
    }
    // loop
    // get the oldest scratch object with the given root hash
    // calculate the commit hash for that object
    sqlite3_reset(fromScratchStmt);
    if (!found)
        return false;

    int second_count = 0;
    link_object chain_end;

    do {
        second_count++;
        link_object next_scratch_object = getNextScratchObject(root_hash);
        if (!next_scratch_object.validObject) {
            LOG_ERROR("StoreForwardpp Speculation commit possibly failed, no next scratch object");
            break;
        }
        if (addToChain(next_scratch_object))
            links_speculated++;
        removeFromScratch(next_scratch_object.message_hash, next_scratch_object.message_hash_len);
        chain_end = getLinkFromPositionFromTip(0, root_hash, SFPP_HASH_SIZE);
        printBytes("StoreForwardpp local final commit hash: ", chain_end.commit_hash, SFPP_HASH_SIZE);
        printBytes("StoreForwardpp target commit hash:  ", commit_hash_bytes, commit_hash_len);
    } while (memcmp(chain_end.commit_hash, commit_hash_bytes, commit_hash_len) != 0);

    LOG_INFO("StoreForwardpp added %d links from scratch", second_count);
    maybeMoveFromCanonScratch(root_hash, SFPP_HASH_SIZE);
    return true;
}

StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::getNextScratchObject(uint8_t *root_hash)
{
    link_object lo;
    lo.validObject = true;
    sqlite3_bind_blob(fromScratchStmt, 1, root_hash, SFPP_HASH_SIZE, NULL);
    if (sqlite3_step(fromScratchStmt) == SQLITE_DONE) {
        lo.validObject = false;
        return lo;
    }
    lo.to = sqlite3_column_int(fromScratchStmt, 0);
    lo.from = sqlite3_column_int(fromScratchStmt, 1);
    lo.id = sqlite3_column_int(fromScratchStmt, 2);

    uint8_t *_encrypted = (uint8_t *)sqlite3_column_blob(fromScratchStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(fromScratchStmt, 3);
    memcpy(lo.encrypted_bytes, _encrypted, lo.encrypted_len);

    uint8_t *_message_hash = (uint8_t *)sqlite3_column_blob(fromScratchStmt, 4);
    lo.message_hash_len = sqlite3_column_bytes(fromScratchStmt, 4);
    memcpy(lo.message_hash, _message_hash, lo.message_hash_len);

    lo.rx_time = sqlite3_column_int(fromScratchStmt, 5);

    lo.root_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.root_hash, root_hash, lo.root_hash_len);

    sqlite3_reset(fromScratchStmt);

    return lo;
}

void StoreForwardPlusPlusModule::updatePayload(uint8_t *message_hash_bytes, size_t message_hash_len, std::string payload)
{
    LOG_WARN("StoreForwardpp updatePayload");
    sqlite3_bind_text(updatePayloadStmt, 1, payload.c_str(), payload.length(), NULL);
    sqlite3_bind_int(updatePayloadStmt, 2, message_hash_len);
    sqlite3_bind_blob(updatePayloadStmt, 3, message_hash_bytes, message_hash_len, NULL);
    auto res = sqlite3_step(updatePayloadStmt);
    if (res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(ppDb);
        LOG_WARN("StoreForwardpp updatePayloadStmt step error %u, %s", res, _error_mesg);
    }
    sqlite3_reset(updatePayloadStmt);
}

StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::getFromScratch(uint8_t *message_hash_bytes, size_t hash_len)
{
    link_object lo;

    sqlite3_bind_int(fromScratchByHashStmt, 1, hash_len);
    sqlite3_bind_blob(fromScratchByHashStmt, 2, message_hash_bytes, hash_len, NULL);
    auto res = sqlite3_step(fromScratchByHashStmt);
    if (res != SQLITE_ROW && res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(ppDb);
        LOG_WARN("StoreForwardpp fromScratchByHashStmt step error %u, %s", res, _error_mesg);
    }
    lo.to = sqlite3_column_int(fromScratchByHashStmt, 0);
    lo.from = sqlite3_column_int(fromScratchByHashStmt, 1);
    lo.id = sqlite3_column_int(fromScratchByHashStmt, 2);

    uint8_t *encrypted_bytes = (uint8_t *)sqlite3_column_blob(fromScratchByHashStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(fromScratchByHashStmt, 3);
    memcpy(lo.encrypted_bytes, encrypted_bytes, lo.encrypted_len);

    uint8_t *message_hash = (uint8_t *)sqlite3_column_blob(fromScratchByHashStmt, 4);
    lo.message_hash_len = sqlite3_column_bytes(fromScratchByHashStmt, 4);
    memcpy(lo.message_hash, message_hash, SFPP_HASH_SIZE);

    lo.rx_time = sqlite3_column_int(fromScratchByHashStmt, 5);

    uint8_t *root_hash = (uint8_t *)sqlite3_column_blob(fromScratchByHashStmt, 6);
    lo.root_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.root_hash, root_hash, SFPP_HASH_SIZE);
    lo.payload =
        std::string((char *)sqlite3_column_text(fromScratchByHashStmt, 7), sqlite3_column_bytes(fromScratchByHashStmt, 7));
    sqlite3_reset(fromScratchByHashStmt);
    return lo;
}

// should not need size considerations
StoreForwardPlusPlusModule::link_object
StoreForwardPlusPlusModule::ingestTextPacket(const meshtastic_MeshPacket &mp, const meshtastic_MeshPacket *encrypted_meshpacket)
{
    link_object lo;
    lo.to = mp.to;
    lo.from = mp.from;
    lo.id = mp.id;
    lo.rx_time = mp.rx_time;
    lo.channel_hash = encrypted_meshpacket->channel;
    memcpy(lo.encrypted_bytes, encrypted_meshpacket->encrypted.bytes, encrypted_meshpacket->encrypted.size);
    lo.encrypted_len = encrypted_meshpacket->encrypted.size;
    lo.payload = std::string((char *)mp.decoded.payload.bytes, mp.decoded.payload.size);

    sfppframe::messageHash(encrypted_meshpacket->encrypted.bytes, encrypted_meshpacket->encrypted.size, mp.to, mp.from, mp.id,
                           lo.message_hash);
    lo.message_hash_len = SFPP_HASH_SIZE;

    lo.root_hash_len = getOrDeriveRootFromChannel(encrypted_meshpacket->channel, lo.root_hash);
    return lo;
}

StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::ingestLinkMessage(meshtastic_StoreForwardPlusPlus *t)
{
    link_object lo;

    lo.to = t->encapsulated_to;
    if (lo.to == 0) {
        lo.to = NODENUM_BROADCAST;
    }
    lo.from = t->encapsulated_from;
    lo.id = t->encapsulated_id;
    lo.rx_time = t->encapsulated_rxtime;
    lo.counter = t->chain_count; // Should this be skipped if stratum0?

    // What if we don't have this root hash? Should drop this packet before this point.
    lo.channel_hash = getChannelHashFromRoot(t->root_hash.bytes, t->root_hash.size);

    memcpy(lo.encrypted_bytes, t->message.bytes, t->message.size);
    lo.encrypted_len = t->message.size;
    memcpy(lo.message_hash, t->message_hash.bytes, t->message_hash.size);
    lo.message_hash_len = t->message_hash.size;
    memcpy(lo.root_hash, t->root_hash.bytes, t->root_hash.size);
    lo.root_hash_len = t->root_hash.size;
    memcpy(lo.commit_hash, t->commit_hash.bytes, t->commit_hash.size);
    lo.commit_hash_len = t->commit_hash.size;

    // we don't ever get the payload here, so it's always an empty string
    lo.payload = "";
    lo.validObject = true;

    return lo;
}

void StoreForwardPlusPlusModule::rebroadcastLinkObject(link_object &lo)
{
    LOG_INFO("StoreForwardpp Attempting to Rebroadcast a message received over SF++");
    texts_rebroadcast++;
    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = lo.to;
    p->from = lo.from;
    p->id = lo.id;
    p->hop_limit = HOP_RELIABLE;
    p->hop_start = HOP_RELIABLE;
    p->channel = lo.channel_hash;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = lo.encrypted_len;
    memcpy(p->encrypted.bytes, lo.encrypted_bytes, lo.encrypted_len);
    p->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA; // only a tiny white lie
    // Send to mesh, but don't cc the phone, to avoid a double message.
    sendSfppPacket(p, RX_SRC_RADIO, false);
}

bool StoreForwardPlusPlusModule::checkCommitHash(StoreForwardPlusPlusModule::link_object &lo, uint8_t *commit_hash_bytes,
                                                 size_t hash_len)
{
    SHA256 commit_hash;
    uint8_t tmp_commit_hash[SFPP_HASH_SIZE];

    link_object chain_end = getLinkFromPositionFromTip(0, lo.root_hash, lo.root_hash_len);

    commit_hash.reset();

    if (chain_end.commit_hash_len == SFPP_HASH_SIZE) {
        printBytes("StoreForwardpp last message: 0x", chain_end.commit_hash, SFPP_HASH_SIZE);
        commit_hash.update(chain_end.commit_hash, SFPP_HASH_SIZE);
    } else {
        if (lo.root_hash_len != SFPP_HASH_SIZE) {
            LOG_ERROR("StoreForwardpp Short root hash in link object, cannot create new chain");
            return false;
        }
        printBytes("StoreForwardpp new chain root: 0x", lo.root_hash, SFPP_HASH_SIZE);
        commit_hash.update(lo.root_hash, SFPP_HASH_SIZE);
    }

    commit_hash.update(lo.message_hash, SFPP_HASH_SIZE);
    commit_hash.finalize(tmp_commit_hash, SFPP_HASH_SIZE);

    if (hash_len == 0 || memcmp(commit_hash_bytes, tmp_commit_hash, hash_len) == 0) {
        lo.commit_hash_len = SFPP_HASH_SIZE;
        memcpy(lo.commit_hash, tmp_commit_hash, SFPP_HASH_SIZE);
        return true;
    }
    return false;
}

bool StoreForwardPlusPlusModule::lookUpFullRootHash(uint8_t *partial_root_hash, size_t partial_root_hash_len,
                                                    uint8_t *full_root_hash)
{
    printBytes("StoreForwardpp partial_root_hash", partial_root_hash, partial_root_hash_len);
    sqlite3_bind_int(getFullRootHashStmt, 1, partial_root_hash_len);
    sqlite3_bind_blob(getFullRootHashStmt, 2, partial_root_hash, partial_root_hash_len, NULL);
    sqlite3_step(getFullRootHashStmt);
    uint8_t *tmp_root_hash = (uint8_t *)sqlite3_column_blob(getFullRootHashStmt, 0);
    if (tmp_root_hash) {
        LOG_DEBUG("StoreForwardpp Found full root hash!");
        memcpy(full_root_hash, tmp_root_hash, SFPP_HASH_SIZE);
        sqlite3_reset(getFullRootHashStmt);
        return true;
    }
    sqlite3_reset(getFullRootHashStmt);
    return false;
}

void StoreForwardPlusPlusModule::setChainCount(uint8_t *root_hash, size_t root_hash_len, uint32_t count)
{
    sqlite3_bind_int(setChainCountStmt, 1, root_hash_len);
    sqlite3_bind_blob(setChainCountStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(setChainCountStmt, 3, count);
    sqlite3_step(setChainCountStmt);
    sqlite3_reset(setChainCountStmt);
}

uint32_t StoreForwardPlusPlusModule::getChainCount(uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_bind_int(getChainCountStmt, 1, root_hash_len);
    sqlite3_bind_blob(getChainCountStmt, 2, root_hash, root_hash_len, NULL);

    int res = sqlite3_step(getChainCountStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE && res != SQLITE_ROW) {
        LOG_ERROR("StoreForwardpp getChainCount sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    uint32_t count = sqlite3_column_int(getChainCountStmt, 0);
    sqlite3_reset(getChainCountStmt);
    return count;
}

uint32_t StoreForwardPlusPlusModule::getScratchCount(uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_bind_int(getScratchCountStmt, 1, root_hash_len);
    sqlite3_bind_blob(getScratchCountStmt, 2, root_hash, root_hash_len, NULL);

    int res = sqlite3_step(getScratchCountStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE && res != SQLITE_ROW) {
        LOG_ERROR("StoreForwardpp getScratchCountStmt sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    uint32_t count = sqlite3_column_int(getScratchCountStmt, 0);
    sqlite3_reset(getScratchCountStmt);
    return count;
}

uint32_t StoreForwardPlusPlusModule::getCanonScratchCount(uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_bind_int(getRootCanonScratchCountStmt, 1, root_hash_len);
    sqlite3_bind_blob(getRootCanonScratchCountStmt, 2, root_hash, root_hash_len, NULL);

    int res = sqlite3_step(getRootCanonScratchCountStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE && res != SQLITE_ROW) {
        LOG_ERROR("StoreForwardpp getRootCanonScratchCountStmt sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    uint32_t count = sqlite3_column_int(getRootCanonScratchCountStmt, 0);
    sqlite3_reset(getRootCanonScratchCountStmt);
    return count;
}

StoreForwardPlusPlusModule::link_object
StoreForwardPlusPlusModule::getLinkFromPositionFromTip(uint32_t _count, uint8_t *_root_hash, size_t _root_hash_len)
{
    link_object lo;
    int step = 0;
    uint32_t _rx_time = 0;

    uint8_t last_message_commit_hash[SFPP_HASH_SIZE] = {0};
    uint8_t last_message_hash[SFPP_HASH_SIZE] = {0};

    sqlite3_bind_int(getChainEndStmt, 1, _root_hash_len);
    sqlite3_bind_blob(getChainEndStmt, 2, _root_hash, _root_hash_len, NULL);
    // this needs to handle a count of 0, indicating the latest
    while (sqlite3_step(getChainEndStmt) == SQLITE_ROW) {
        // get the data from the row while it is still valid
        uint8_t *last_message_commit_hash_ptr = (uint8_t *)sqlite3_column_blob(getChainEndStmt, 0);
        uint8_t *last_message_hash_ptr = (uint8_t *)sqlite3_column_blob(getChainEndStmt, 1);
        _rx_time = sqlite3_column_int(getChainEndStmt, 2);
        memcpy(last_message_commit_hash, last_message_commit_hash_ptr, SFPP_HASH_SIZE);
        memcpy(last_message_hash, last_message_hash_ptr, SFPP_HASH_SIZE);
        if (_count == step)
            break;

        step++;
    }
    if (!memfll(last_message_commit_hash, '\0', SFPP_HASH_SIZE) && _rx_time != 0) {
        lo = getLink(last_message_commit_hash, SFPP_HASH_SIZE);
        if (lo.rx_time == 0)
            lo.validObject = false;
    } else {
        LOG_WARN("StoreForwardpp Failed to get link from count");
        lo.validObject = false;
    }

    sqlite3_reset(getChainEndStmt);
    return lo;
}

void StoreForwardPlusPlusModule::pruneScratchQueue()
{
    // Very little utility in holding on to very old scratch messages
    sqlite3_bind_int(pruneScratchQueueStmt, 1, time(nullptr) - 60 * 60 * 6);
    int res = sqlite3_step(pruneScratchQueueStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp Prune Scratch sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(pruneScratchQueueStmt);
    scratch_timed_out += sqlite3_changes(ppDb);
}

void StoreForwardPlusPlusModule::trimOldestLink(uint8_t *root_hash, size_t root_hash_len)
{
    links_pruned++;
    sqlite3_bind_int(trimOldestLinkStmt, 1, root_hash_len);
    sqlite3_bind_blob(trimOldestLinkStmt, 2, root_hash, root_hash_len, NULL);
    int res = sqlite3_step(trimOldestLinkStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp Trim Oldest Link sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(trimOldestLinkStmt);
}

void StoreForwardPlusPlusModule::clearChain(uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_bind_int(clearChainStmt, 1, root_hash_len);
    sqlite3_bind_blob(clearChainStmt, 2, root_hash, root_hash_len, NULL);
    int res = sqlite3_step(clearChainStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp Clear Chain sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(clearChainStmt);
    setChainCount(root_hash, root_hash_len, 0);
}

void StoreForwardPlusPlusModule::recalculateMessageHash(link_object &lo)
{
    sfppframe::messageHash(lo.encrypted_bytes, lo.encrypted_len, lo.to, lo.from, lo.id, lo.message_hash);
    lo.message_hash_len = SFPP_HASH_SIZE;
}

bool StoreForwardPlusPlusModule::recalculateHash(StoreForwardPlusPlusModule::link_object &lo, uint8_t *_root_hash_bytes,
                                                 size_t _root_hash_len, uint8_t *_commit_hash_bytes, size_t _commit_hash_len)
{
    recalculateMessageHash(lo);

    // look up full root hash and copy over the partial if it matches
    if (lookUpFullRootHash(_root_hash_bytes, _root_hash_len, lo.root_hash)) {
        printBytes("StoreForwardpp Found full root hash: 0x", lo.root_hash, SFPP_HASH_SIZE);
        lo.root_hash_len = SFPP_HASH_SIZE;
    } else {
        LOG_WARN("StoreForwardpp root hash does not match %d bytes", _root_hash_len);
        lo.root_hash_len = 0;
        lo.validObject = false;
        return false;
    }

    if (_commit_hash_len == SFPP_HASH_SIZE && getChainCount(_root_hash_bytes, _root_hash_len) == 0 &&
        portduino_config.sfpp_initial_sync != 0 && !portduino_config.sfpp_stratum0) {
        lo.commit_hash_len = SFPP_HASH_SIZE;
        memcpy(lo.commit_hash, _commit_hash_bytes, SFPP_HASH_SIZE);

    } else if (_commit_hash_len > 0) {
        // calculate the full commit hash and replace the partial if it matches
        if (checkCommitHash(lo, _commit_hash_bytes, _commit_hash_len)) {
            printBytes("StoreForwardpp commit hash matches: 0x", _commit_hash_bytes, _commit_hash_len);
        } else {
            LOG_WARN("StoreForwardpp commit hash does not match, rejecting link.");
            lo.validObject = false;
            return false;
        }
    }
    return true;
}

void StoreForwardPlusPlusModule::updatePeers(const meshtastic_MeshPacket &mp,
                                             meshtastic_StoreForwardPlusPlus_SFPP_message_type type)
{
    sqlite3_bind_int64(getPeerStmt, 1, (sqlite3_int64)mp.from);
    int res = sqlite3_step(getPeerStmt);
    if (res == SQLITE_ROW) {
        auto announce_count = sqlite3_column_int(getPeerStmt, 0);
        auto query_count = sqlite3_column_int(getPeerStmt, 1);
        auto request_count = sqlite3_column_int(getPeerStmt, 2);
        auto provide_count = sqlite3_column_int(getPeerStmt, 3);
        auto split_count = sqlite3_column_int(getPeerStmt, 4);
        auto total_count = sqlite3_column_int(getPeerStmt, 5);
        auto average_hops = sqlite3_column_double(getPeerStmt, 6);

        total_count++;
        if (type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_CANON_ANNOUNCE) {
            announce_count++;
        } else if (type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_CHAIN_QUERY) {
            query_count++;
        } else if (type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_REQUEST) {
            request_count++;
        } else if (type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE) {
            provide_count++;
        } else if (type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_FIRSTHALF ||
                   type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_SECONDHALF) {
            split_count++;
        }
        // recalculate average hops
        average_hops = ((average_hops * (total_count - 1)) + (mp.hop_start - mp.hop_limit)) / total_count;

        sqlite3_bind_int(updatePeerStmt, 1, announce_count);
        sqlite3_bind_int(updatePeerStmt, 2, query_count);
        sqlite3_bind_int(updatePeerStmt, 3, request_count);
        sqlite3_bind_int(updatePeerStmt, 4, provide_count);
        sqlite3_bind_int(updatePeerStmt, 5, split_count);
        sqlite3_bind_int(updatePeerStmt, 6, total_count);
        sqlite3_bind_double(updatePeerStmt, 7, average_hops);
        sqlite3_bind_int64(updatePeerStmt, 8, (sqlite3_int64)mp.from);
        res = sqlite3_step(updatePeerStmt);
        if (res != SQLITE_OK && res != SQLITE_DONE) {
            LOG_ERROR("StoreForwardpp Update Peer sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
        }
        sqlite3_reset(updatePeerStmt);

    } else {

        sqlite3_bind_int64(maybeAddPeerStmt, 1, (sqlite3_int64)mp.from);
        sqlite3_bind_int(maybeAddPeerStmt, 2, 1);
        sqlite3_bind_int(maybeAddPeerStmt, 3, mp.hop_start - mp.hop_limit);
        res = sqlite3_step(maybeAddPeerStmt);
        if (res != SQLITE_OK && res != SQLITE_DONE) {
            LOG_ERROR("StoreForwardpp Maybe Add Peer sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
        }
        sqlite3_reset(maybeAddPeerStmt);
    }
    sqlite3_reset(getPeerStmt);

    // get the peer row for the from node
    //  if it's new, just push the new one in
    //  if not new, run the calculations and update the numbers
}

void StoreForwardPlusPlusModule::maybeMoveFromCanonScratch(uint8_t *root_hash, size_t root_hash_len)
{

    // get the earliest chain count. See if it's the next one. Try to commit
    // if not, speculate and try to commit
    link_object lo = getfromCanonScratch(root_hash, root_hash_len);
    if (!lo.validObject)
        return;
    link_object chain_end = getLinkFromPositionFromTip(0, root_hash, root_hash_len);
    if (!chain_end.validObject)
        return;
    if (chain_end.counter > lo.counter) {
        removeFromCanonScratch(lo.message_hash, lo.message_hash_len);
        maybeMoveFromCanonScratch(root_hash, root_hash_len); // recursion!
    }

    if (lo.counter == chain_end.counter + 1 &&
        recalculateHash(lo, root_hash, root_hash_len, lo.commit_hash, lo.commit_hash_len)) {

        if (addToChain(lo))
            links_from_canon_scratch++;
        maybeMoveFromCanonScratch(root_hash, root_hash_len); // recursion!
        return;
    }
    // TODO check hash lengths here
    LOG_INFO("speculating chain, attempting to commit from canon scratch");
    speculateScratchChain(lo.commit_hash, lo.commit_hash_len, root_hash, lo.commit_hash);
}

void StoreForwardPlusPlusModule::addToCanonScratch(link_object &lo)
{

    // recalculate full message hash?
    LOG_INFO("StoreForwardpp addToCanonScratch");
    if (lo.counter == 0) {
        LOG_WARN("StoreForwardpp Counter 0, not adding to Canon Scratch");
        return;
    }

    logLinkObject(lo);

    // push a message into the local chain DB
    // destination
    sqlite3_bind_int64(canon_scratch_insert_stmt, 1, (sqlite3_int64)lo.to);
    // sender
    sqlite3_bind_int64(canon_scratch_insert_stmt, 2, (sqlite3_int64)lo.from);
    // packet_id
    sqlite3_bind_int64(canon_scratch_insert_stmt, 3, (sqlite3_int64)lo.id);
    // root_hash
    sqlite3_bind_blob(canon_scratch_insert_stmt, 4, lo.root_hash, lo.root_hash_len, NULL);
    // encrypted_bytes
    sqlite3_bind_blob(canon_scratch_insert_stmt, 5, lo.encrypted_bytes, lo.encrypted_len, NULL);
    // message_hash
    sqlite3_bind_blob(canon_scratch_insert_stmt, 6, lo.message_hash, lo.message_hash_len, NULL);
    // rx_time
    sqlite3_bind_int(canon_scratch_insert_stmt, 7, lo.rx_time);
    // commit_hash
    sqlite3_bind_blob(canon_scratch_insert_stmt, 8, lo.commit_hash, lo.commit_hash_len, NULL);
    // payload
    sqlite3_bind_text(canon_scratch_insert_stmt, 9, lo.payload.c_str(), lo.payload.length(), NULL);

    sqlite3_bind_int(canon_scratch_insert_stmt, 10, lo.counter);
    int res = sqlite3_step(canon_scratch_insert_stmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp canon_scratch_insert_stmt Cannot step %u: %s", res, sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(canon_scratch_insert_stmt);
}

StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::getfromCanonScratch(uint8_t *root_hash_bytes, size_t hash_len)
{

    link_object lo;
    sqlite3_bind_int(getCanonScratchStmt, 1, hash_len);
    sqlite3_bind_blob(getCanonScratchStmt, 2, root_hash_bytes, hash_len, NULL);
    auto res = sqlite3_step(getCanonScratchStmt);
    if (res != SQLITE_ROW && res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(ppDb);
        LOG_ERROR("StoreForwardpp getCanonScratchStmt step error %u, %s", res, _error_mesg);
        lo.validObject = false;
        sqlite3_reset(getCanonScratchStmt);
        return lo;
    }
    lo.to = sqlite3_column_int(getCanonScratchStmt, 0);
    lo.from = sqlite3_column_int(getCanonScratchStmt, 1);
    lo.id = sqlite3_column_int(getCanonScratchStmt, 2);

    uint8_t *_payload = (uint8_t *)sqlite3_column_blob(getCanonScratchStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(getCanonScratchStmt, 3);
    memcpy(lo.encrypted_bytes, _payload, lo.encrypted_len);

    uint8_t *_message_hash = (uint8_t *)sqlite3_column_blob(getCanonScratchStmt, 4);
    lo.message_hash_len = sqlite3_column_bytes(getCanonScratchStmt, 4);
    memcpy(lo.message_hash, _message_hash, lo.message_hash_len);

    lo.rx_time = sqlite3_column_int(getCanonScratchStmt, 5);

    uint8_t *_tmp_commit_hash = (uint8_t *)sqlite3_column_blob(getCanonScratchStmt, 6);
    lo.commit_hash_len = sqlite3_column_bytes(getCanonScratchStmt, 6);
    memcpy(lo.commit_hash, _tmp_commit_hash, lo.commit_hash_len);

    uint8_t *_root_hash = (uint8_t *)sqlite3_column_blob(getCanonScratchStmt, 7);
    lo.root_hash_len = sqlite3_column_bytes(getCanonScratchStmt, 7);
    memcpy(lo.root_hash, _root_hash, lo.root_hash_len);

    lo.counter = sqlite3_column_int(getCanonScratchStmt, 8);

    lo.payload = std::string((char *)sqlite3_column_text(getCanonScratchStmt, 9));

    sqlite3_reset(getCanonScratchStmt);

    return lo;
}

void StoreForwardPlusPlusModule::removeFromCanonScratch(uint8_t *message_hash_bytes, size_t message_hash_len)
{
    printBytes("StoreForwardpp removing from canon scratch: ", message_hash_bytes, message_hash_len);
    sqlite3_bind_int(removeCanonScratch, 1, message_hash_len);
    sqlite3_bind_blob(removeCanonScratch, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(removeCanonScratch);
    sqlite3_reset(removeCanonScratch);
}

bool StoreForwardPlusPlusModule::isInCanonScratch(uint8_t *message_hash, size_t message_hash_len)
{
    if (message_hash_len < SFPP_SHORT_HASH_SIZE) {
        return false;
    }
    sqlite3_bind_int(getCanonScratchCountStmt, 1, message_hash_len);
    sqlite3_bind_blob(getCanonScratchCountStmt, 2, message_hash, message_hash_len, NULL);
    sqlite3_step(getCanonScratchCountStmt);
    int count = sqlite3_column_int(getCanonScratchCountStmt, 0);
    sqlite3_reset(getCanonScratchCountStmt);
    if (count > 0)
        return true;
    return false;
}

void StoreForwardPlusPlusModule::clearCanonScratch(uint8_t *root_hash, size_t root_hash_len, uint32_t new_count)
{
    sqlite3_bind_int(clearCanonScratchStmt, 1, root_hash_len);
    sqlite3_bind_blob(clearCanonScratchStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(clearCanonScratchStmt, 3, new_count);
    int res = sqlite3_step(clearCanonScratchStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp Clear Canon Scratch sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(clearCanonScratchStmt);
}

void StoreForwardPlusPlusModule::logLinkObject(link_object &lo)
{
    LOG_DEBUG("To: %u, From: %u, id: %u, rx_time: %u, counter: %u, channel_hash: 0x%02x", lo.to, lo.from, lo.id, lo.rx_time,
              lo.counter, lo.channel_hash);
    printBytes("encrypted_bytes: ", lo.encrypted_bytes, lo.encrypted_len);
    printBytes("message_hash: ", lo.message_hash, lo.message_hash_len);
    printBytes("root_hash: ", lo.root_hash, lo.root_hash_len);
    printBytes("commit_hash: ", lo.commit_hash, lo.commit_hash_len);
    LOG_DEBUG("payload: %s", lo.payload.c_str());
    if (lo.validObject) {
        LOG_DEBUG("Valid Object");
    }
}

StoreForwardPlusPlusModule *storeForwardPlusPlusModule;

// --- Arm C: detector + short-ID enumeration ---

void StoreForwardPlusPlusModule::logEnum(const char *dir, const char *type, const sfppsr::Scope &scope, size_t payloadBytes,
                                         NodeNum peer, int ids, int frameIndex, int frameTotal, size_t sketchBytes)
{
    char rootHex[9];
    formatRootHex(scope.rootPrefix, scope.rootPrefixLen, rootHex);

    if (sketchBytes > 0)
        LOG_INFO("SFPP-ENUM dir=%s type=%s root=%s bucket=%u payload=%u peer=0x%08x ids=%d frame=%d/%d sketch=%u", dir, type,
                 rootHex, scope.bucket, (unsigned)payloadBytes, peer, ids, frameIndex, frameTotal, (unsigned)sketchBytes);
    else if (ids >= 0)
        LOG_INFO("SFPP-ENUM dir=%s type=%s root=%s bucket=%u payload=%u peer=0x%08x ids=%d frame=%d/%d", dir, type, rootHex,
                 scope.bucket, (unsigned)payloadBytes, peer, ids, frameIndex, frameTotal);
    else
        LOG_INFO("SFPP-ENUM dir=%s type=%s root=%s bucket=%u payload=%u peer=0x%08x", dir, type, rootHex, scope.bucket,
                 (unsigned)payloadBytes, peer);
}

bool StoreForwardPlusPlusModule::sendEnumMessage(const meshtastic_StoreForwardPlusPlus &msg, NodeNum to, const char *type,
                                                 size_t sketchBytes, const sfppsr::Scope *scopeOverride)
{
    if (portduino_config.sfpp_enum_listen_only)
        return true; // listen-only: accept overheard traffic, never transmit

    sfppsr::Scope scope;
    if (scopeOverride) {
        scope = *scopeOverride;
    } else {
        scope.rootPrefixLen = msg.root_hash.size;
        memcpy(scope.rootPrefix, msg.root_hash.bytes, msg.root_hash.size);
        scope.bucket = msg.chain_count;
    }

    int ids = -1;
    if (msg.sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE ||
        msg.sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
        ids = msg.sr_short_ids_count;

    int frameIndex = 0;
    int frameTotal = 1;
    if (msg.sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME) {
        if (msg.frame != 0) {
            frameIndex = msg.frame & 0xFF;
            frameTotal = msg.frame >> 8 & 0xFF;
        }
    } else if (msg.sr_frame != 0) {
        frameIndex = msg.sr_frame & 0xFF;
        frameTotal = msg.sr_frame >> 8 & 0xFF;
    }

    meshtastic_MeshPacket *p = allocDataProtobuf(msg);
    if (!p) {
        LOG_WARN("StoreForwardpp cannot allocate an enumeration packet");
        return false;
    }
    p->to = to;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->channel = 0;
    p->hop_limit = portduino_config.sfpp_hops;
    p->hop_start = portduino_config.sfpp_hops;

    // sendToMesh takes ownership and may free the packet; capture everything first. Enumeration
    // frames are protocol traffic, so no phone copy (which would read the packet after the send).
    const size_t payloadBytes = p->decoded.payload.size;
    const ErrorCode res = sendSfppPacket(p, RX_SRC_LOCAL, false);
    if (res == ERRNO_SHOULD_RELEASE)
        return true; // handled locally, not an on-air send
    if (res != ERRNO_OK) {
        LOG_WARN("StoreForwardpp enumeration send failed (%d), will retry", (int)res);
        return false;
    }

    logEnum("tx", type, scope, payloadBytes, to, ids, frameIndex, frameTotal, sketchBytes);
    if (enumExchangeActive) {
        enumTx++;
        enumTxBytes += payloadBytes;
        enumExchangeStartedMs = Time::getMillis();
    }
    return true;
}

void StoreForwardPlusPlusModule::beginEnumExchange(const uint8_t *root_hash, uint32_t bucket, NodeNum peer, uint32_t peerCount,
                                                   uint64_t peerChecksum)
{
    enumExchangeActive = true;
    enumExchangeSucceeded = false;
    memcpy(enumExchangeRoot, root_hash, SFPP_HASH_SIZE);
    enumExchangeBucket = bucket;
    enumExchangePeer = peer;
    // The pending entry survives the exchange so its attempt count and first-failure time carry
    // across retries; a successful exchange removes it in endEnumExchange().
    enumExchangeStartedMs = Time::getMillis();
    enumRecovered = enumTx = enumRx = enumTxBytes = enumRxBytes = 0;
    enumWanted.clear();
    enumWantedRemaining = 0;
    enumProvideActive = false;
    enumPeerCount = peerCount;
    enumPeerChecksum = peerChecksum;
    enumCollisionProbe = false;
    enumSketchAttempt = false;
    enumSketchIds = 0;
    enumLastItemMs = enumExchangeStartedMs;
    enumAwaitingList = true;
    enumListRetries = 0;
    enumItemRetries = 0;
    enumRequestSentMs = enumExchangeStartedMs;
    enumLateActive = false;
    enumLateWanted.clear();
    enumPromoted.clear();
    enumOnDemand = false;
    // The thread only checks the deadline when runOnce() runs; schedule it now.
    setIntervalFromNow(5 * 1000);

    char rootHex[9];
    formatRootHex(root_hash, SFPP_HASH_SIZE, rootHex);
    LOG_INFO("SFPP-ENUM exchange begin role=puller root=%s bucket=%u peer=0x%08x", rootHex, bucket, peer);
}

void StoreForwardPlusPlusModule::endEnumExchange()
{
    if (!enumExchangeActive)
        return;

    char rootHex[9];
    formatRootHex(enumExchangeRoot, SFPP_HASH_SIZE, rootHex);
    LOG_INFO("SFPP-ENUM exchange end role=puller root=%s bucket=%u recovered=%u tx=%u rx=%u tx_bytes=%u rx_bytes=%u", rootHex,
             enumExchangeBucket, enumRecovered, enumTx, enumRx, enumTxBytes, enumRxBytes);

    if (enumExchangeSucceeded) {
        enumPending.erase(std::remove_if(enumPending.begin(), enumPending.end(),
                                         [&](const EnumPendingBucket &entry) {
                                             return entry.bucket == enumExchangeBucket && entry.authority == enumExchangePeer &&
                                                    memcmp(entry.root, enumExchangeRoot, SFPP_HASH_SIZE) == 0;
                                         }),
                          enumPending.end());
    }

    enumExchangeActive = false;
    enumProvideActive = false;
    enumCollisionProbe = false;
    enumSketchAttempt = false;
    enumSketchIds = 0;
    enumOnDemand = false;
    enumAwaitingList = false;
    enumPromoted.clear();
    enumListRetries = 0;
    enumItemRetries = 0;
    enumWanted.clear();
    enumWantedRemaining = 0;
}

void StoreForwardPlusPlusModule::expireEnumExchange()
{
    if (!enumExchangeActive)
        return;

    if (Throttle::hasElapsed(enumExchangeStartedMs, kEnumExchangeTimeoutMs)) {
        if (!enumWanted.empty() && !enumCollisionProbe)
            beginEnumLateAccept(); // paced frames may still arrive
        if (!enumExchangeSucceeded)
            rememberFailedPull();
        endEnumExchange();
        return;
    }

    // A collision probe has no named end: the peer serves every row for the shared IDs and then
    // stops. Silence is the only completion signal, so finalise a probe or a half-received provide
    // after an idle gap rather than waiting for the 15 min overall timeout.
    const bool partialProvide = enumProvideActive && enumProvideSeenCount < enumProvideTotal;
    if ((enumCollisionProbe || partialProvide) && Throttle::hasElapsed(enumLastItemMs, kEnumProbeIdleMs)) {
        if (!bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
            char rootHex[9];
            formatRootHex(enumExchangeRoot, SFPP_HASH_SIZE, rootHex);
            LOG_INFO("SFPP-ENUM collision_suspected root=%s bucket=%u", rootHex, enumExchangeBucket);
            suppressEnumBucket(enumExchangeRoot, enumExchangeBucket, enumPeerCount, enumPeerChecksum);
        }
        if (!enumExchangeSucceeded)
            rememberFailedPull();
        endEnumExchange();
    }
}

void StoreForwardPlusPlusModule::beginEnumLateAccept()
{
    if (enumWanted.empty())
        return;
    enumLateActive = true;
    memcpy(enumLateRoot, enumExchangeRoot, SFPP_HASH_SIZE);
    enumLateBucket = enumExchangeBucket;
    enumLatePeer = enumExchangePeer;
    enumLateWanted = enumWanted;
    enumLateStartedMs = Time::getMillis();
}

void StoreForwardPlusPlusModule::markEnumProgress()
{
    if (enumPeerTip == 0)
        return;
    // We have confirmed our store up to the end of this bucket, but no further than the peer tip.
    const uint32_t bucketEnd = (enumExchangeBucket + 1) * sfppsr::kBucketObjects;
    const uint32_t reached = enumPeerTip < bucketEnd ? enumPeerTip : bucketEnd;
    if (reached > enumKnownTip)
        enumKnownTip = reached;
}

void StoreForwardPlusPlusModule::addPendingBucket(const uint8_t *root_hash, NodeNum authority, uint32_t bucket, uint32_t count,
                                                  uint64_t checksum, uint32_t tip)
{
    for (auto &entry : enumPending) {
        if (entry.bucket == bucket && entry.authority == authority && memcmp(entry.root, root_hash, SFPP_HASH_SIZE) == 0) {
            // A changed advertised summary revives a capped or parked hole with a fresh bound.
            if (entry.count != count || entry.checksum != checksum || entry.tip != tip) {
                entry.attempts = 0;
                entry.firstFailedMs = 0;
                entry.retryAfterMs = 0;
            }
            entry.count = count;
            entry.checksum = checksum;
            entry.tip = tip;
            entry.onDemand = count == 0 && checksum == 0;
            return;
        }
    }
    if (enumPending.size() >= kEnumPendingMax) {
        size_t lowest = 0;
        for (size_t i = 1; i < enumPending.size(); i++) {
            if (enumPending[i].bucket < enumPending[lowest].bucket)
                lowest = i;
        }
        enumPending.erase(enumPending.begin() + lowest);
    }
    EnumPendingBucket entry = {};
    entry.bucket = bucket;
    entry.count = count;
    entry.checksum = checksum;
    entry.tip = tip;
    memcpy(entry.root, root_hash, SFPP_HASH_SIZE);
    entry.authority = authority;
    entry.onDemand = count == 0 && checksum == 0;
    enumPending.push_back(entry);
}

void StoreForwardPlusPlusModule::rememberFailedPull()
{
    // Opt-in: the sim shows this raises airtime without closing the rotation hole. Drop the
    // launching entry too, or the next advert would re-air the same hole without a retry state.
    if (!portduino_config.sfpp_pull_retry) {
        enumPending.erase(std::remove_if(enumPending.begin(), enumPending.end(),
                                         [&](const EnumPendingBucket &entry) {
                                             return entry.bucket == enumExchangeBucket && entry.authority == enumExchangePeer &&
                                                    memcmp(entry.root, enumExchangeRoot, SFPP_HASH_SIZE) == 0;
                                         }),
                          enumPending.end());
        return;
    }
    uint8_t root[SFPP_HASH_SIZE];
    memcpy(root, enumExchangeRoot, SFPP_HASH_SIZE); // the failed exchange's own root
    const uint32_t nowMs = Time::getMillis();

    // A failed tail pull stays a tail pull; only a named exchange has a checksum to compare.
    const uint32_t count = enumOnDemand ? 0 : enumPeerCount;
    const uint64_t checksum = enumOnDemand ? 0 : enumPeerChecksum;
    addPendingBucket(root, enumExchangePeer, enumExchangeBucket, count, checksum, enumPeerTip);
    for (auto &entry : enumPending) {
        if (entry.bucket != enumExchangeBucket || entry.authority != enumExchangePeer ||
            memcmp(entry.root, root, SFPP_HASH_SIZE) != 0)
            continue;
        if (entry.firstFailedMs != 0 && Throttle::hasElapsed(entry.firstFailedMs, kServiceWindowMs))
            return; // past the proposed service window: accept the hole as lost
        if (entry.attempts >= kFailedPullMaxAttempts) {
            // Park it: only a different advertised summary for this bucket should revive the hole.
            entry.retryAfterMs = nowMs + kServiceWindowMs;
            return;
        }
        entry.attempts++;
        uint32_t delay = kFailedPullBackoffMs;
        for (uint8_t i = 1; i < entry.attempts && delay < kFailedPullBackoffMaxMs; i++)
            delay = delay < kFailedPullBackoffMaxMs / 2 ? delay * 2 : kFailedPullBackoffMaxMs;
        entry.retryAfterMs = nowMs + (delay > kFailedPullBackoffMaxMs ? kFailedPullBackoffMaxMs : delay);
        if (entry.firstFailedMs == 0)
            entry.firstFailedMs = nowMs;
        char rootHex[9];
        formatRootHex(root, SFPP_HASH_SIZE, rootHex);
        LOG_INFO("SFPP-ENUM failed pull root=%s bucket=%u peer=0x%08x attempts=%u", rootHex, entry.bucket, entry.authority,
                 entry.attempts);
        return;
    }
}

void StoreForwardPlusPlusModule::startPendingPull()
{
    if (portduino_config.sfpp_enum_listen_only)
        return;
    if (enumExchangeActive || enumPending.empty() || enumAuthorityNode == 0)
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root) == 0)
        return;
    const uint32_t nowMs = Time::getMillis();

    // Key by (root, authority); a hole older than the proposed service window is no longer chased.
    enumPending.erase(
        std::remove_if(enumPending.begin(), enumPending.end(),
                       [&](const EnumPendingBucket &entry) {
                           return entry.authority != enumAuthorityNode || memcmp(entry.root, root, SFPP_HASH_SIZE) != 0 ||
                                  (entry.firstFailedMs != 0 && Throttle::hasElapsed(entry.firstFailedMs, kServiceWindowMs));
                       }),
        enumPending.end());
    if (enumPending.empty())
        return;

    // One pull per pacer slot (D11/G3), shared with the item drain on this node.
    if (enumLastDrainMs != 0 && !Throttle::hasElapsed(enumLastDrainMs, enumDrainIntervalMs()))
        return;

    auto retryReady = [&](const EnumPendingBucket &entry) {
        return entry.retryAfterMs == 0 || Throttle::deadlinePassedAt(nowMs, entry.retryAfterMs);
    };
    size_t best = enumPending.size();
    for (size_t i = 0; i < enumPending.size(); i++) {
        if (retryReady(enumPending[i]) && (best == enumPending.size() || enumPending[i].bucket > enumPending[best].bucket))
            best = i;
    }
    if (best == enumPending.size())
        return;
    const EnumPendingBucket entry = enumPending[best];

    // A named bucket that could not be repaired from the same advertised summary is skipped.
    if (!entry.onDemand && enumBucketSuppressed(root, entry.bucket, entry.count, entry.checksum)) {
        enumPending.erase(enumPending.begin() + best);
        return;
    }
    // Keep the entry: endEnumExchange() removes it on success, rememberFailedPull() advances its
    // attempt and first-failure state on failure.

    char rootHex[9];
    formatRootHex(root, SFPP_HASH_SIZE, rootHex);
    LOG_INFO("SFPP-ENUM sketch_decode result=skipped reason=pending_pull root=%s bucket=%u peer=0x%08x", rootHex, entry.bucket,
             entry.authority);
    if (entry.onDemand) {
        beginEnumExchange(root, entry.bucket, entry.authority, entry.tip, 0);
        enumPeerTip = entry.tip;
        enumOnDemand = true;
    } else {
        beginEnumExchange(root, entry.bucket, entry.authority, entry.count, entry.checksum);
        enumPeerTip = entry.tip;
    }
    requestEnumList(root, entry.bucket, entry.authority);
    enumLastDrainMs = nowMs; // consume the pacer slot
}

void StoreForwardPlusPlusModule::repairPromotions()
{
    if (enumPromoted.empty()) {
        endEnumExchange();
        return;
    }

    std::vector<pinsketch::Element> ids;
    for (const auto &promoted : enumPromoted) {
        if (isInDB(promoted.hash, SFPP_HASH_SIZE)) {
            sqlite3_reset(deleteCanonStmt);
            sqlite3_bind_blob(deleteCanonStmt, 1, promoted.hash, SFPP_HASH_SIZE, SQLITE_STATIC);
            sqlite3_step(deleteCanonStmt);
            sqlite3_reset(deleteCanonStmt);
        }
        ids.push_back(promoted.id);
    }
    enumPromoted.clear();
    enumSketchAttempt = false;
    enumCollisionProbe = false;
    enumItemRetries = 0;
    enumWanted.clear();
    enumWantedRemaining = 0;

    if (!ids.empty())
        requestMissingItems(enumExchangeRoot, enumExchangeBucket, enumExchangePeer, ids);
    else
        endEnumExchange();
}

void StoreForwardPlusPlusModule::expireEnumLate()
{
    if (!enumLateActive)
        return;
    if (enumLateWanted.empty() || Throttle::hasElapsed(enumLateStartedMs, kEnumLateAcceptMs)) {
        enumLateActive = false;
        enumLateWanted.clear();
    }
}

bool StoreForwardPlusPlusModule::bucketMatchesPeer(const uint8_t *root_hash, uint32_t bucket)
{
    sfppsr::BucketSummary local(4);
    buildBucketSummary(root_hash, SFPP_HASH_SIZE, bucket, local);
    return local.count() == enumPeerCount && local.checksum() == enumPeerChecksum;
}

bool StoreForwardPlusPlusModule::enumBucketSuppressed(const uint8_t *root_hash, uint32_t bucket, uint32_t count,
                                                      uint64_t checksum) const
{
    for (const auto &entry : enumSuppressed) {
        if (entry.bucket == bucket && entry.count == count && entry.checksum == checksum &&
            memcmp(entry.root, root_hash, SFPP_HASH_SIZE) == 0)
            return true;
    }
    return false;
}

void StoreForwardPlusPlusModule::suppressEnumBucket(const uint8_t *root_hash, uint32_t bucket, uint32_t count, uint64_t checksum)
{
    for (auto &entry : enumSuppressed) {
        if (entry.bucket == bucket && memcmp(entry.root, root_hash, SFPP_HASH_SIZE) == 0) {
            entry.count = count;
            entry.checksum = checksum;
            return;
        }
    }
    EnumSuppression entry;
    memcpy(entry.root, root_hash, SFPP_HASH_SIZE);
    entry.bucket = bucket;
    entry.count = count;
    entry.checksum = checksum;
    enumSuppressed.push_back(entry);
}

void StoreForwardPlusPlusModule::advertiseOneBucket(uint8_t *root_hash, size_t root_hash_len)
{
    if (memcmp(enumSeenRoot, root_hash, SFPP_HASH_SIZE) != 0) {
        memcpy(enumSeenRoot, root_hash, SFPP_HASH_SIZE);
        enumAdvertSeen.clear();
    }
    if (getChainCount(root_hash, root_hash_len) == 0)
        return;

    link_object tip = getLinkFromPositionFromTip(0, root_hash, root_hash_len);
    if (tip.counter == 0)
        return;
    const uint32_t tipCounter = tip.counter;
    const uint32_t maxBucket = (tipCounter - 1) / sfppsr::kBucketObjects;

    // Stage 8f: newest-changed first. A bucket whose summary differs from the one last advertised
    // (or that was never advertised) goes ahead of the round-robin.
    int64_t chosen = -1;
    for (int64_t b = (int64_t)maxBucket; b >= 0; b--) {
        sfppsr::BucketSummary summary(4);
        if (!buildBucketSummary(root_hash, root_hash_len, (uint32_t)b, summary) || summary.count() == 0)
            continue;
        bool changed = true;
        for (const auto &seen : enumAdvertSeen) {
            if (seen.bucket == (uint32_t)b) {
                changed = (seen.count != summary.count() || seen.checksum != summary.checksum());
                break;
            }
        }
        if (changed) {
            chosen = b;
            break;
        }
    }

    uint32_t bucket = 0;
    if (chosen >= 0) {
        bucket = (uint32_t)chosen;
        enumBucketCursor = (bucket + 1) % (maxBucket + 1);
    } else {
        // No change: one bucket per tick, round-robin, skipping empty buckets.
        bool found = false;
        for (uint32_t attempts = 0; attempts <= maxBucket; attempts++) {
            if (enumBucketCursor > maxBucket)
                enumBucketCursor = 0;
            const uint32_t candidate = enumBucketCursor;
            enumBucketCursor = (enumBucketCursor + 1) % (maxBucket + 1);
            sfppsr::BucketSummary summary(4);
            if (buildBucketSummary(root_hash, root_hash_len, candidate, summary) && summary.count() > 0) {
                bucket = candidate;
                found = true;
                break;
            }
        }
        if (!found)
            return;
    }

    sfppsr::BucketSummary summary(4);
    if (!buildBucketSummary(root_hash, root_hash_len, bucket, summary) || summary.count() == 0)
        return;

    sfppsr::Scope scope;
    // Every tenth advert carries the full root so a blank follower can join; the rest use the
    // 4-byte prefix to keep the standing cost down.
    const bool fullRoot = (enumAdvertTick++ % kEnumFullRootEvery) == (kEnumFullRootEvery - 1);
    copyScopeRoot(root_hash, root_hash_len, scope, bucket, fullRoot);
    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildAdvert(scope, summary, msg))
        return;
    // Stage 8f: the advert names this bucket; encapsulated_rxtime carries the sender's tip counter.
    msg.encapsulated_rxtime = tipCounter;
    if (!sendEnumMessage(msg, NODENUM_BROADCAST, "advert", msg.message.size))
        return;

    bool stored = false;
    for (auto &seen : enumAdvertSeen) {
        if (seen.bucket == bucket) {
            seen.count = summary.count();
            seen.checksum = summary.checksum();
            stored = true;
            break;
        }
    }
    if (!stored)
        enumAdvertSeen.push_back({bucket, summary.count(), summary.checksum()});
}

void StoreForwardPlusPlusModule::handleEnumAdvert(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    sfppsr::Advert advert;
    if (!sfppsr::parseAdvert(*t, advert))
        return;

    uint8_t derived[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), derived) == 0)
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    bool known = lookUpFullRootHash(advert.scope.rootPrefix, advert.scope.rootPrefixLen, root);
    if (known && memcmp(root, derived, SFPP_HASH_SIZE) != 0) {
        noteEnumPeer(mp.from, advert.scope.rootPrefix);
        if (mp.from != enumForeignRootLoggedPeer) {
            char rootHex[9];
            formatRootHex(root, SFPP_HASH_SIZE, rootHex);
            LOG_INFO("SFPP-ENUM ignoring advert from 0x%08x: foreign root %s", mp.from, rootHex);
            enumForeignRootLoggedPeer = mp.from;
        }
        return;
    }
    if (!known) {
        // A 4-byte prefix cannot be checked; only a full root can join us.
        if (advert.scope.rootPrefixLen != SFPP_HASH_SIZE)
            return;
        if (memcmp(advert.scope.rootPrefix, derived, SFPP_HASH_SIZE) != 0) {
            noteEnumPeer(mp.from, advert.scope.rootPrefix);
            if (mp.from != enumForeignRootLoggedPeer) {
                char rootHex[9];
                formatRootHex(advert.scope.rootPrefix, SFPP_HASH_SIZE, rootHex);
                LOG_INFO("SFPP-ENUM ignoring advert from 0x%08x: foreign root %s", mp.from, rootHex);
                enumForeignRootLoggedPeer = mp.from;
            }
            return;
        }
        memcpy(root, derived, SFPP_HASH_SIZE);
    }
    enumForeignRootLoggedPeer = 0;

    // F3.2: only a parsed advert whose root is ours names the authority.
    enumAuthorityNode = mp.from;
    // Stage 8h: the tip and advert bookkeeping belong to (root, authority); reset on a change.
    if (memcmp(enumTipRoot, root, SFPP_HASH_SIZE) != 0 || enumTipAuthority != mp.from) {
        memcpy(enumTipRoot, root, SFPP_HASH_SIZE);
        enumTipAuthority = mp.from;
        enumKnownTip = 0;
        enumPending.clear();
    }
    logEnum("rx", "advert", advert.scope, mp.decoded.payload.size, mp.from, -1, 0, 1, t->message.size);
    if (portduino_config.sfpp_enum_listen_only)
        return; // learn the authority, never request

    // A failed pull is retried on the next advert from that peer, whichever bucket it names.
    for (const auto &entry : enumPending) {
        if (entry.authority == mp.from && entry.retryAfterMs != 0 && memcmp(entry.root, root, SFPP_HASH_SIZE) == 0 &&
            Throttle::deadlinePassedAt(Time::getMillis(), entry.retryAfterMs)) {
            setIntervalFromNow(10);
            break;
        }
    }

    const uint32_t advertTip = t->encapsulated_rxtime; // Stage 8f: the sender's highest counter
    const uint32_t ownTip = highestCounter(root, SFPP_HASH_SIZE);

    // The receiver of a mismatching advert always pulls. An empty local bucket is count 0 and
    // checksum 0, so it mismatches a non-empty peer and pulls too.
    sfppsr::BucketSummary local(4);
    buildBucketSummary(root, SFPP_HASH_SIZE, advert.scope.bucket, local);
    const bool namedMismatch = !(local.count() == advert.count && local.checksum() == advert.checksum);
    const uint32_t knownTip = ownTip > enumKnownTip ? ownTip : enumKnownTip;
    const bool tailBehind = advertTip > knownTip;
    if (!namedMismatch && !tailBehind)
        return;

    const uint32_t tipBucket = advertTip > 0 ? (advertTip - 1) / sfppsr::kBucketObjects : 0;

    if (enumExchangeActive) {
        // Stage 8g: a behind bucket seen while busy is remembered (bounded, newest first) and
        // pulled when this exchange ends.
        if (namedMismatch && advert.scope.bucket != enumExchangeBucket)
            addPendingBucket(root, mp.from, advert.scope.bucket, advert.count, advert.checksum, advertTip);
        if (tailBehind && tipBucket != enumExchangeBucket)
            addPendingBucket(root, mp.from, tipBucket, 0, 0, advertTip);
        return;
    }

    // Stage 8h: prefer the newest tail over an older named bucket.
    if (tailBehind && namedMismatch && advert.scope.bucket != tipBucket) {
        addPendingBucket(root, mp.from, advert.scope.bucket, advert.count, advert.checksum, advertTip);
        addPendingBucket(root, mp.from, tipBucket, 0, 0, advertTip);
        setIntervalFromNow(10);
        return;
    }

    if (namedMismatch) {
        // A bucket that could not be repaired from this advertised summary is not pulled again until
        // the authority advertises a different one for it.
        if (enumBucketSuppressed(root, advert.scope.bucket, advert.count, advert.checksum))
            return;

        beginEnumExchange(root, advert.scope.bucket, mp.from, advert.count, advert.checksum);
        enumPeerTip = advertTip;
        if (portduino_config.sfpp_repair == SFPP_REPAIR_SKETCH &&
            trySketchExchange(advert, local, root, advert.scope.bucket, mp.from))
            return;
        requestEnumList(root, advert.scope.bucket, mp.from);
        return;
    }

    // The named bucket agrees but the sender's tip is ahead: queue the newest bucket; startPendingPull
    // paces it with the one server pacer.
    addPendingBucket(root, mp.from, tipBucket, 0, 0, advertTip);
    setIntervalFromNow(10);
}

bool StoreForwardPlusPlusModule::trySketchExchange(const sfppsr::Advert &advert, sfppsr::BucketSummary &local,
                                                   const uint8_t *root_hash, uint32_t bucket, NodeNum peer)
{
    char rootHex[9];
    formatRootHex(root_hash, SFPP_HASH_SIZE, rootHex);
    auto logDecode = [&](const char *result, size_t decoded, size_t promoted, size_t missing) {
        LOG_INFO("SFPP-ENUM sketch_decode result=%s root=%s bucket=%u capacity=%u decoded=%u promoted=%u missing=%u", result,
                 rootHex, bucket, (unsigned)advert.sketch.capacity(), (unsigned)decoded, (unsigned)promoted, (unsigned)missing);
    };

    std::vector<pinsketch::Element> difference;
    if (!local.difference(advert.sketch, difference)) {
        logDecode("fail", 0, 0, 0);
        return false;
    }

    std::vector<pinsketch::Element> own;
    collectBucketIds(root_hash, SFPP_HASH_SIZE, bucket, own);
    std::sort(own.begin(), own.end());
    own.erase(std::unique(own.begin(), own.end()), own.end());

    // The decoded set is the symmetric difference; what we lack is what to fetch.
    std::vector<pinsketch::Element> missing;
    std::set_difference(difference.begin(), difference.end(), own.begin(), own.end(), std::back_inserter(missing));
    if (missing.empty()) {
        logDecode("bad_checksum", difference.size(), 0, 0);
        return false;
    }

    // Cheap cardinality check: a wrong over-capacity decode names IDs the peer does not hold, and
    // its own-only and missing counts will not reconstruct the advertised count.
    const size_t ownOnly = difference.size() - missing.size();
    if (ownOnly > local.count() || advert.count != local.count() - (uint32_t)ownOnly + (uint32_t)missing.size()) {
        logDecode("bad_checksum", difference.size(), 0, missing.size());
        return false;
    }

    // Item 10: a peer ID we already hold in scratch needs no fetch; promote it into the bucket.
    const size_t missingBefore = missing.size();
    promoteScratchMatches(root_hash, bucket, missing);
    const size_t promoted = missingBefore - missing.size();
    if (missing.empty()) {
        if (bucketMatchesPeer(root_hash, bucket)) {
            logDecode("ok", difference.size(), promoted, 0);
            enumExchangeSucceeded = true;
            endEnumExchange();
            return true;
        }
        logDecode("bad_checksum", difference.size(), promoted, 0);
        return false;
    }

    enumSketchAttempt = true;
    enumSketchIds = (uint32_t)missing.size();
    logDecode("ok", difference.size(), promoted, missing.size());
    requestMissingItems(root_hash, bucket, peer, missing);
    return true;
}

void StoreForwardPlusPlusModule::fallbackToEnumList()
{
    enumSketchAttempt = false;
    enumSketchIds = 0;
    enumWanted.clear();
    enumWantedRemaining = 0;
    enumProvideActive = false;
    enumCollisionProbe = false;
    enumListRetries = 0;
    enumItemRetries = 0;
    requestEnumList(enumExchangeRoot, enumExchangeBucket, enumExchangePeer);
}

void StoreForwardPlusPlusModule::requestEnumList(const uint8_t *root_hash, uint32_t bucket, NodeNum peer)
{
    sfppsr::Scope scope;
    copyScopeRoot(root_hash, SFPP_HASH_SIZE, scope, bucket, false);
    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildEnumRequest(scope, msg))
        return;
    enumAwaitingList = true;
    enumListRetries = 0;
    enumRequestSentMs = Time::getMillis();
    sendEnumMessage(msg, peer, "enum_req");
}

void StoreForwardPlusPlusModule::handleEnumRequest(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    if (portduino_config.sfpp_enum_listen_only || mp.to != nodeDB->getNodeNum())
        return;

    sfppsr::Scope scope;
    if (!sfppsr::parseScope(*t, scope))
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (!lookUpFullRootHash(scope.rootPrefix, scope.rootPrefixLen, root))
        return;

    noteEnumPeer(mp.from, scope.rootPrefix);
    logEnum("rx", "enum_req", scope, mp.decoded.payload.size, mp.from, -1, 0, 1);

    std::vector<pinsketch::Element> ids;
    if (!collectBucketIds(root, SFPP_HASH_SIZE, scope.bucket, ids))
        return;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    const size_t total = (ids.size() + sfppsr::kMaxShortIds - 1) / sfppsr::kMaxShortIds;
    if (total == 0 || total > 255)
        return;

    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    for (size_t i = 0; i < total; i++) {
        const size_t begin = i * sfppsr::kMaxShortIds;
        const size_t end = std::min(begin + sfppsr::kMaxShortIds, ids.size());
        std::vector<pinsketch::Element> slice(ids.begin() + begin, ids.begin() + end);
        meshtastic_StoreForwardPlusPlus msg;
        if (!sfppsr::buildEnumProvide(scope, slice, (uint8_t)i, (uint8_t)total, msg))
            continue;
        frames.push_back(msg);
    }
    if (!frames.empty()) {
        queueEnumFrames(frames, mp.from, mp.from, scope, "enum_prov"); // N6: the same paced drain as items
        setIntervalFromNow(10);
    }
}

void StoreForwardPlusPlusModule::handleEnumProvide(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    if (!enumExchangeActive || mp.from != enumExchangePeer)
        return;

    sfppsr::ShortIdList list;
    if (!sfppsr::parseShortIds(*t, list))
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (!lookUpFullRootHash(list.scope.rootPrefix, list.scope.rootPrefixLen, root))
        return;
    if (list.scope.bucket != enumExchangeBucket || memcmp(root, enumExchangeRoot, SFPP_HASH_SIZE) != 0)
        return;

    logEnum("rx", "enum_prov", list.scope, mp.decoded.payload.size, mp.from, (int)list.ids.size(), list.frameIndex,
            list.frameTotal);
    enumRx++;
    enumRxBytes += mp.decoded.payload.size;
    enumExchangeStartedMs = Time::getMillis();
    enumRequestSentMs = enumExchangeStartedMs;
    enumLastItemMs = enumExchangeStartedMs; // a received provide frame is progress
    enumAwaitingList = false;

    if (list.frameTotal == 0 || list.frameTotal > (uint8_t)(sizeof(enumProvideSeen)))
        return;

    if (!enumProvideActive) {
        enumProvideActive = true;
        enumProvideTotal = list.frameTotal;
        enumProvideSeenCount = 0;
        memset(enumProvideSeen, 0, sizeof(enumProvideSeen));
        enumProvideIds.clear();
        enumProvideBucket = list.scope.bucket;
        enumProvidePeer = mp.from;
    }
    if (list.frameTotal != enumProvideTotal)
        return;
    if (list.frameIndex >= enumProvideTotal || enumProvideSeen[list.frameIndex])
        return;

    enumProvideSeen[list.frameIndex] = true;
    enumProvideSeenCount++;
    enumProvideIds.insert(enumProvideIds.end(), list.ids.begin(), list.ids.end());
    if (enumProvideSeenCount < enumProvideTotal)
        return;

    // Complete list: everything the peer holds that we do not is what we fetch.
    std::vector<pinsketch::Element> own;
    collectBucketIds(root, SFPP_HASH_SIZE, enumProvideBucket, own);
    std::sort(enumProvideIds.begin(), enumProvideIds.end());
    enumProvideIds.erase(std::unique(enumProvideIds.begin(), enumProvideIds.end()), enumProvideIds.end());
    std::sort(own.begin(), own.end());
    own.erase(std::unique(own.begin(), own.end()), own.end());
    if (enumOnDemand) {
        // The bucket's own count (from the peer's list), not the advert tip.
        enumPeerCount = (uint32_t)enumProvideIds.size();
        enumPeerChecksum = 0;
    }

    std::vector<pinsketch::Element> missing;
    std::set_difference(enumProvideIds.begin(), enumProvideIds.end(), own.begin(), own.end(), std::back_inserter(missing));
    // Item 10: a peer ID we already hold in scratch needs no fetch; promote it into the bucket.
    promoteScratchMatches(root, enumProvideBucket, missing);

    enumProvideActive = false;
    if (!missing.empty()) {
        enumCollisionProbe = false;
        requestMissingItems(root, enumProvideBucket, enumProvidePeer, missing);
        return;
    }

    if (enumOnDemand) {
        markEnumProgress();
        enumExchangeSucceeded = true;
        endEnumExchange(); // no advertised count/checksum to confirm against; the list was the check
        return;
    }

    // Stage 8h: a promotion that the peer's checksum does not confirm is a collision; undo it and
    // fetch the peer's objects instead.
    if (!enumPromoted.empty() && !bucketMatchesPeer(root, enumProvideBucket)) {
        repairPromotions();
        return;
    }

    if (bucketMatchesPeer(root, enumProvideBucket)) {
        markEnumProgress();
        enumExchangeSucceeded = true;
        endEnumExchange();
        return;
    }

    // The lists match by short ID but the checksums differ: a truncated-ID collision may hide an
    // object. Ask for every object the shared IDs name; the probe ends on convergence or idle.
    enumCollisionProbe = true;
    enumLastItemMs = Time::getMillis();
    requestMissingItems(root, enumProvideBucket, enumProvidePeer, enumProvideIds);
}

void StoreForwardPlusPlusModule::requestMissingItems(const uint8_t *root_hash, uint32_t bucket, NodeNum peer,
                                                     const std::vector<pinsketch::Element> &ids)
{
    if (ids.empty() || ids.size() > sfppsr::kMaxShortIds)
        return;

    sfppsr::Scope scope;
    copyScopeRoot(root_hash, SFPP_HASH_SIZE, scope, bucket, false);
    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildItemRequest(scope, ids, msg))
        return;

    enumWanted = ids;
    enumWantedRemaining = ids.size();
    enumAwaitingList = false;
    enumItemRetries = 0;
    enumRequestSentMs = Time::getMillis();
    sendEnumMessage(msg, peer, "item_req");
}

void StoreForwardPlusPlusModule::handleItemRequest(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    if (portduino_config.sfpp_enum_listen_only || mp.to != nodeDB->getNodeNum())
        return;

    sfppsr::ShortIdList list;
    if (!sfppsr::parseShortIds(*t, list))
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (!lookUpFullRootHash(list.scope.rootPrefix, list.scope.rootPrefixLen, root))
        return;

    noteEnumPeer(mp.from, list.scope.rootPrefix);
    noteRequesterActivity(mp.from); // a re-request refreshes the requester's entry
    logEnum("rx", "item_req", list.scope, mp.decoded.payload.size, mp.from, (int)list.ids.size(), 0, 1);

    const uint32_t first = list.scope.bucket * sfppsr::kBucketObjects + 1;
    const uint32_t last = first + sfppsr::kBucketObjects - 1;
    const uint32_t now = getValidTime(RTCQuality::RTCQualityNTP, true);

    for (pinsketch::Element wanted : list.ids) {
        // Already queued for this requester: do not duplicate it, just refresh the entry.
        if (enumRequesterHasQueued(mp.from, wanted))
            continue;

        sqlite3_reset(getLinkByShortIdStmt);
        sqlite3_bind_int(getLinkByShortIdStmt, 1, SFPP_HASH_SIZE);
        sqlite3_bind_blob(getLinkByShortIdStmt, 2, root, SFPP_HASH_SIZE, NULL);
        sqlite3_bind_int(getLinkByShortIdStmt, 3, first);
        sqlite3_bind_int(getLinkByShortIdStmt, 4, last);
        sqlite3_bind_int64(getLinkByShortIdStmt, 5, (sqlite3_int64)wanted);

        // A short ID can name more than one stored object; serve every one so a collided pair
        // cannot hide behind a single row.
        bool served = false;
        while (sqlite3_step(getLinkByShortIdStmt) == SQLITE_ROW) {
            served = true;
            const uint32_t to = (uint32_t)sqlite3_column_int64(getLinkByShortIdStmt, 0);
            const uint32_t from = (uint32_t)sqlite3_column_int64(getLinkByShortIdStmt, 1);
            const uint32_t id = (uint32_t)sqlite3_column_int64(getLinkByShortIdStmt, 2);
            const uint8_t *enc = (const uint8_t *)sqlite3_column_blob(getLinkByShortIdStmt, 3);
            const int encLen = sqlite3_column_bytes(getLinkByShortIdStmt, 3);
            const uint32_t rxTime = (uint32_t)sqlite3_column_int64(getLinkByShortIdStmt, 5);
            std::vector<uint8_t> cipher(enc, enc + encLen);

            const uint32_t heardAgo = rxTime < now ? now - rxTime : 0;
            std::vector<meshtastic_StoreForwardPlusPlus> frames;
            if (!sfppframe::split(cipher.data(), cipher.size(), to, from, id, heardAgo, false, frames))
                continue;

            sfppsr::Scope scope;
            copyScopeRoot(root, SFPP_HASH_SIZE, scope, list.scope.bucket, false);
            // Item 6: every frame names its bucket so an overhearing archive can place it.
            for (auto &frame : frames)
                frame.chain_count = list.scope.bucket;
            const NodeNum dest = portduino_config.sfpp_item_transport == SFPP_ITEM_DIRECT ? mp.from : NODENUM_BROADCAST;
            queueItemFrames(frames, mp.from, dest, scope, wanted);
        }
        sqlite3_reset(getLinkByShortIdStmt);

        if (!served)
            LOG_WARN("StoreForwardpp item request for an unknown short ID 0x%08x", wanted);
    }

    if (anyEnumQueuePending())
        setIntervalFromNow(10); // G21: the first frame goes on the next tick, not after a full drain interval
}

bool StoreForwardPlusPlusModule::anyEnumQueuePending() const
{
    for (const auto &requester : enumTxQueues) {
        if (!requester.queue.empty())
            return true;
    }
    return false;
}

bool StoreForwardPlusPlusModule::enumRequesterHasQueued(NodeNum to, pinsketch::Element id) const
{
    if (id == 0)
        return false;
    for (const auto &requester : enumTxQueues) {
        if (requester.to != to)
            continue;
        for (const auto &out : requester.queue) {
            if (out.id == id)
                return true;
        }
    }
    return false;
}

void StoreForwardPlusPlusModule::noteRequesterActivity(NodeNum to)
{
    const uint32_t now = Time::getMillis();
    for (auto &requester : enumTxQueues) {
        if (requester.to == to) {
            requester.lastUsedMs = now;
            return;
        }
    }
}

void StoreForwardPlusPlusModule::queueItemFrames(const std::vector<meshtastic_StoreForwardPlusPlus> &frames, NodeNum requester,
                                                 NodeNum dest, const sfppsr::Scope &scope, pinsketch::Element id)
{
    queueEnumFrames(frames, requester, dest, scope, "item_frame", id);
}

void StoreForwardPlusPlusModule::queueEnumFrames(const std::vector<meshtastic_StoreForwardPlusPlus> &frames, NodeNum requester,
                                                 NodeNum dest, const sfppsr::Scope &scope, const char *type,
                                                 pinsketch::Element id)
{
    const uint32_t now = Time::getMillis();

    // Drop empty or expired requester entries so the table does not grow without bound.
    for (auto it = enumTxQueues.begin(); it != enumTxQueues.end();) {
        if (it->queue.empty() || Throttle::hasElapsed(it->lastUsedMs, kEnumRequesterTtlMs))
            it = enumTxQueues.erase(it);
        else
            ++it;
    }

    EnumRequester *slot = nullptr;
    for (auto &entry : enumTxQueues) {
        if (entry.to == requester) {
            slot = &entry;
            break;
        }
    }
    if (slot == nullptr) {
        if (enumTxQueues.size() >= kEnumMaxRequesters) {
            LOG_WARN("StoreForwardpp enumeration queue limit reached, dropping the request");
            return;
        }
        enumTxQueues.push_back(EnumRequester());
        slot = &enumTxQueues.back();
        slot->to = requester;
        slot->lastUsedMs = now;
    }

    // Per-requester budget (2 x 32 x 4 frames); one requester cannot starve the others.
    for (const auto &frame : frames) {
        if (slot->queue.size() >= kEnumTxQueueMax) {
            LOG_WARN("StoreForwardpp item frame queue full, dropping the rest of the request");
            break;
        }
        EnumOutgoing out;
        out.msg = frame;
        out.to = dest;
        out.scope = scope;
        out.type = type;
        out.id = id;
        slot->queue.push_back(out);
    }
    slot->lastUsedMs = now;
}

bool StoreForwardPlusPlusModule::drainOneEnumFrame()
{
    // F3.7/N11: a requester's TTL is refreshed only by its own traffic, never by the drain. Drop
    // its queue once it goes quiet.
    for (auto it = enumTxQueues.begin(); it != enumTxQueues.end();) {
        if (it->queue.empty() || Throttle::hasElapsed(it->lastUsedMs, kEnumRequesterTtlMs))
            it = enumTxQueues.erase(it);
        else
            ++it;
    }
    if (!anyEnumQueuePending())
        return false;

    // One last-send timestamp across all requesters (N7); do not reset it on enqueue.
    if (enumLastDrainMs != 0 && !Throttle::hasElapsed(enumLastDrainMs, enumDrainIntervalMs()))
        return true;
    if (!consumeServeBudget())
        return true; // the queue stays pending until the window rolls over

    // Round-robin across requesters so one cannot starve another. One attempt per step; a failed
    // send leaves its frame queued for the next turn.
    const uint32_t nowMs = Time::getMillis();
    size_t active = 0;
    for (const auto &entry : enumTxQueues)
        if (!entry.queue.empty())
            active++;
    const uint8_t fairShare = active == 0 ? kServeWindowFrames : (uint8_t)(kServeWindowFrames / active);
    bool deferred = false;
    for (size_t i = 0; i < enumTxQueues.size(); i++) {
        const size_t idx = (enumQueueCursor + i) % enumTxQueues.size();
        auto &requester = enumTxQueues[idx];
        if (requester.queue.empty())
            continue;
        if (requester.windowStartMs == 0 || Throttle::hasElapsed(requester.windowStartMs, kServeWindowMs)) {
            requester.windowStartMs = nowMs;
            requester.sentInWindow = 0;
        }
        if (requester.sentInWindow >= (fairShare == 0 ? 1 : fairShare)) {
            deferred = true;
            continue;
        }

        const EnumOutgoing &out = requester.queue.front();
        if (sendEnumMessage(out.msg, out.to, out.type, 0, &out.scope)) {
            requester.queue.pop_front();
            requester.sentInWindow++;
        }
        enumQueueCursor = (idx + 1) % enumTxQueues.size();
        enumLastDrainMs = nowMs;
        return true;
    }
    return deferred;
}

void StoreForwardPlusPlusModule::resendEnumList()
{
    sfppsr::Scope scope;
    copyScopeRoot(enumExchangeRoot, SFPP_HASH_SIZE, scope, enumExchangeBucket, false);
    meshtastic_StoreForwardPlusPlus msg;
    if (sfppsr::buildEnumRequest(scope, msg))
        sendEnumMessage(msg, enumExchangePeer, "enum_req");
    enumRequestSentMs = Time::getMillis();
}

void StoreForwardPlusPlusModule::resendItemRequest()
{
    if (enumWanted.empty())
        return;
    sfppsr::Scope scope;
    copyScopeRoot(enumExchangeRoot, SFPP_HASH_SIZE, scope, enumExchangeBucket, false);
    meshtastic_StoreForwardPlusPlus msg;
    if (sfppsr::buildItemRequest(scope, enumWanted, msg))
        sendEnumMessage(msg, enumExchangePeer, "item_req");
    enumRequestSentMs = Time::getMillis();
}

void StoreForwardPlusPlusModule::pumpEnumRetries()
{
    if (!enumExchangeActive)
        return;

    // Bounded retries: one more list request, or up to two more item requests, before the exchange
    // ends and the next advert starts it again.
    if (enumAwaitingList) {
        if (!Throttle::hasElapsed(enumRequestSentMs, itemRetryIntervalMs()))
            return;
        if (enumListRetries < 1) {
            enumListRetries++;
            resendEnumList();
        } else {
            rememberFailedPull();
            endEnumExchange();
        }
        return;
    }

    if (!enumWanted.empty()) {
        if (!Throttle::hasElapsed(enumRequestSentMs, itemRetryIntervalMs()))
            return;
        if (enumItemRetries < 2) {
            enumItemRetries++;
            resendItemRequest();
        } else if (enumSketchAttempt) {
            // The sketch named IDs the peer did not serve; the list exchange is the only way out.
            char rootHex[9];
            formatRootHex(enumExchangeRoot, SFPP_HASH_SIZE, rootHex);
            LOG_INFO("SFPP-ENUM type=sketch_decode result=bad_checksum root=%s bucket=%u peer=0x%08x ids=%u", rootHex,
                     enumExchangeBucket, enumExchangePeer, enumSketchIds);
            fallbackToEnumList();
        } else {
            rememberFailedPull();
            // Paced frames may still be in flight; keep accepting them for a while (Stage 8e).
            beginEnumLateAccept();
            endEnumExchange();
        }
    }
}

bool StoreForwardPlusPlusModule::isEnumKnownPeer(NodeNum node, const uint8_t *root_prefix) const
{
    for (const auto &peer : enumKnownPeers) {
        if (peer.node == node && memcmp(peer.rootPrefix, root_prefix, sfppsr::kRootPrefixBytes) == 0)
            return true;
    }
    return false;
}

bool StoreForwardPlusPlusModule::isEnumKnownPeerOnAnotherRoot(NodeNum node, const uint8_t *root_prefix) const
{
    for (const auto &peer : enumKnownPeers) {
        if (peer.node == node && memcmp(peer.rootPrefix, root_prefix, sfppsr::kRootPrefixBytes) != 0)
            return true;
    }
    return false;
}

void StoreForwardPlusPlusModule::noteEnumPeer(NodeNum node, const uint8_t *root_prefix)
{
    if (node == 0 || node == NODENUM_BROADCAST)
        return;
    for (size_t i = 0; i < enumKnownPeers.size(); i++) {
        if (enumKnownPeers[i].node == node) {
            EnumPeer peer = enumKnownPeers[i];
            memcpy(peer.rootPrefix, root_prefix, sfppsr::kRootPrefixBytes);
            enumKnownPeers.erase(enumKnownPeers.begin() + i); // re-noted peer becomes most recent
            enumKnownPeers.push_back(peer);
            return;
        }
    }
    if (enumKnownPeers.size() >= kEnumMaxKnownPeers) {
        size_t victim = 0;
        for (size_t i = 0; i < enumKnownPeers.size(); i++) {
            const NodeNum candidate = enumKnownPeers[i].node;
            bool served = false;
            for (const auto &requester : enumTxQueues)
                if (requester.to == candidate && !requester.queue.empty())
                    served = true;
            // Never evict the pull peer or a peer whose frames are queued to be served.
            if (candidate != enumExchangePeer && !served) {
                victim = i;
                break;
            }
        }
        enumKnownPeers.erase(enumKnownPeers.begin() + victim);
    }
    EnumPeer peer;
    peer.node = node;
    memcpy(peer.rootPrefix, root_prefix, sfppsr::kRootPrefixBytes);
    enumKnownPeers.push_back(peer);
}

bool StoreForwardPlusPlusModule::consumePushBudget(NodeNum node)
{
    const uint32_t now = Time::getMillis();
    for (auto &sender : enumPushSenders) {
        if (sender.node != node)
            continue;
        if (Throttle::hasElapsed(sender.windowStartMs, scratchSweepMs())) {
            sender.windowStartMs = now;
            sender.accepted = 0;
        }
        if (sender.accepted >= kPushBudgetRows)
            return false;
        sender.accepted++;
        return true;
    }
    if (enumPushSenders.size() >= kEnumMaxPushSenders)
        enumPushSenders.erase(enumPushSenders.begin());
    enumPushSenders.push_back({node, now, 1});
    return true;
}

bool StoreForwardPlusPlusModule::enumAlreadyOffered(const uint8_t *hash) const
{
    for (const auto &entry : enumPushOffered) {
        if (memcmp(entry.prefix, hash, sizeof(entry.prefix)) == 0)
            return true;
    }
    return false;
}

void StoreForwardPlusPlusModule::noteEnumOffered(const uint8_t *hash)
{
    EnumPushed entry;
    memcpy(entry.prefix, hash, sizeof(entry.prefix));
    entry.offeredMs = Time::getMillis();
    enumPushOffered.push_back(entry);
    while (enumPushOffered.size() > kEnumPushOfferedMax)
        enumPushOffered.pop_front();
}

void StoreForwardPlusPlusModule::pushScratchRows()
{
    if (portduino_config.sfpp_enum_listen_only || enumAuthorityNode == 0)
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root) == 0)
        return;

    // An offer older than the reoffer window may be tried again if the row is still in scratch.
    for (auto it = enumPushOffered.begin(); it != enumPushOffered.end();) {
        if (Throttle::hasElapsed(it->offeredMs, kScratchReofferMs))
            it = enumPushOffered.erase(it);
        else
            ++it;
    }

    const uint32_t now = getValidTime(RTCQuality::RTCQualityNTP, true);
    sfppsr::Scope scope;
    copyScopeRoot(root, SFPP_HASH_SIZE, scope, 0, false);

    sqlite3_reset(fromScratchStmt);
    sqlite3_bind_blob(fromScratchStmt, 1, root, SFPP_HASH_SIZE, NULL);

    size_t queued = 0;
    while (queued < kScratchPushPerSweep && sqlite3_step(fromScratchStmt) == SQLITE_ROW) {
        const uint32_t to = (uint32_t)sqlite3_column_int64(fromScratchStmt, 0);
        const uint32_t from = (uint32_t)sqlite3_column_int64(fromScratchStmt, 1);
        const uint32_t id = (uint32_t)sqlite3_column_int64(fromScratchStmt, 2);
        const uint8_t *cipher = (const uint8_t *)sqlite3_column_blob(fromScratchStmt, 3);
        const int cipherLen = sqlite3_column_bytes(fromScratchStmt, 3);
        const uint8_t *hash = (const uint8_t *)sqlite3_column_blob(fromScratchStmt, 4);
        const uint32_t rxTime = (uint32_t)sqlite3_column_int64(fromScratchStmt, 5);

        if (cipher == nullptr || cipherLen <= 0 || hash == nullptr)
            continue;
        if (rxTime > now || now - rxTime < kScratchPushMinAgeS)
            continue; // too young: the authority may still hear it live and announce it
        if (enumAlreadyOffered(hash))
            continue;

        const uint32_t heardAgo = now - rxTime;
        std::vector<meshtastic_StoreForwardPlusPlus> frames;
        if (!sfppframe::split(cipher, (size_t)cipherLen, to, from, id, heardAgo, false, frames))
            continue;
        queueItemFrames(frames, enumAuthorityNode, enumAuthorityNode, scope);
        noteEnumOffered(hash);
        queued++;
    }
    sqlite3_reset(fromScratchStmt);

    if (queued > 0)
        setIntervalFromNow(10); // the first offered frame drains on the next tick
}

bool StoreForwardPlusPlusModule::acceptPushedItemFrame(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    sfppframe::Replay replay;
    if (!enumPushReassembler.offer(*t, Time::getMillis(), replay))
        return false;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root) == 0)
        return false;

    if (isEnumKnownPeerOnAnotherRoot(mp.from, root)) {
        LOG_INFO("StoreForwardpp refusing a push from 0x%08x on a foreign root", mp.from);
        return false;
    }

    // F3.6: trust a known peer on this root; an unknown pusher gets a small budget per window.
    if (!isEnumKnownPeer(mp.from, root) && !consumePushBudget(mp.from)) {
        LOG_WARN("StoreForwardpp push from 0x%08x is over budget, dropping", mp.from);
        return false;
    }

    link_object lo;
    if (replay.ciphertext.size() > sizeof(lo.encrypted_bytes))
        return false;

    lo.to = replay.to;
    lo.from = replay.from;
    lo.id = replay.id;
    const uint32_t now = getValidTime(RTCQuality::RTCQualityNTP, true);
    lo.rx_time = replay.heardAgoS <= now ? now - replay.heardAgoS : now;
    memcpy(lo.root_hash, root, SFPP_HASH_SIZE);
    lo.root_hash_len = SFPP_HASH_SIZE;
    lo.channel_hash = channels.getHash(0);
    lo.encrypted_len = replay.ciphertext.size();
    memcpy(lo.encrypted_bytes, replay.ciphertext.data(), replay.ciphertext.size());
    sfppframe::messageHash(lo.encrypted_bytes, lo.encrypted_len, lo.to, lo.from, lo.id, lo.message_hash);
    lo.message_hash_len = SFPP_HASH_SIZE;
    lo.counter = 0; // 0 tells addToChain to append at the tip

    if (isInDB(lo.message_hash, lo.message_hash_len))
        return true; // already committed, maybe by another follower's push
    LOG_INFO("StoreForwardpp committing a writer-missed text from 0x%08x", mp.from);
    return addToChain(lo);
}

void StoreForwardPlusPlusModule::handleItemFrame(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    const bool addressed = (mp.to == nodeDB->getNodeNum() || mp.to == NODENUM_BROADCAST);
    if (!addressed)
        return;

    // A writer-missed push is unicast to this archive. Any archive accepts one for its own root;
    // frames belonging to the exchange (or its late window) go to the paths below.
    if (mp.to == nodeDB->getNodeNum() && !(enumExchangeActive && mp.from == enumExchangePeer) &&
        !(enumLateActive && mp.from == enumLatePeer)) {
        acceptPushedItemFrame(mp, t);
        return;
    }

    // Until archive membership is authenticated, a follower trusts item frames only from the
    // authority it learned from an advert.
    if (enumAuthorityNode == 0 || mp.from != enumAuthorityNode)
        return;

    const bool fromActivePeer = enumExchangeActive && mp.from == enumExchangePeer;
    if (fromActivePeer) {
        enumExchangeStartedMs = Time::getMillis();
        enumRequestSentMs = enumExchangeStartedMs;
    }

    sfppsr::Scope logScope;
    if (fromActivePeer)
        copyScopeRoot(enumExchangeRoot, SFPP_HASH_SIZE, logScope, enumExchangeBucket, false);
    else if (enumLateActive && mp.from == enumLatePeer)
        copyScopeRoot(enumLateRoot, SFPP_HASH_SIZE, logScope, enumLateBucket, false);
    else
        logScope.bucket = t->chain_count;
    int frameIndex = t->frame != 0 ? (t->frame & 0xFF) : 0;
    int frameTotal = t->frame != 0 ? (t->frame >> 8 & 0xFF) : 1;
    logEnum("rx", "item_frame", logScope, mp.decoded.payload.size, mp.from, -1, frameIndex, frameTotal);
    enumRx++;
    enumRxBytes += mp.decoded.payload.size;

    sfppframe::Replay replay;
    if (!enumReassembler.offer(*t, Time::getMillis(), replay))
        return;

    uint8_t hash[SFPP_HASH_SIZE];
    sfppframe::messageHash(replay.ciphertext.data(), replay.ciphertext.size(), replay.to, replay.from, replay.id, hash);
    const pinsketch::Element shortId = sfppsr::shortId(hash, sizeof(hash));

    // 1. An object this exchange asked for.
    if (fromActivePeer) {
        auto it = std::find(enumWanted.begin(), enumWanted.end(), shortId);
        if (it != enumWanted.end()) {
            const bool isNew = !isInDB(hash, sizeof(hash));
            if (!storeRecoveredObject(replay, enumExchangeRoot, enumExchangeBucket))
                return;
            enumLastItemMs = Time::getMillis();
            if (isNew)
                enumRecovered++;

            if (enumCollisionProbe) {
                if (bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
                    markEnumProgress();
                    enumExchangeSucceeded = true;
                    endEnumExchange();
                }
                return;
            }

            enumWanted.erase(it);
            enumWantedRemaining = enumWanted.size();
            if (!enumWanted.empty())
                return;

            if (enumSketchAttempt) {
                enumSketchAttempt = false;
                if (bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
                    markEnumProgress();
                    enumExchangeSucceeded = true;
                    endEnumExchange();
                } else if (!enumPromoted.empty()) {
                    repairPromotions();
                    return;
                } else {
                    char rootHex[9];
                    formatRootHex(enumExchangeRoot, SFPP_HASH_SIZE, rootHex);
                    LOG_INFO("SFPP-ENUM type=sketch_decode result=bad_checksum root=%s bucket=%u peer=0x%08x ids=%u", rootHex,
                             enumExchangeBucket, enumExchangePeer, enumSketchIds);
                    fallbackToEnumList();
                }
                return;
            }
            if (!enumOnDemand && !enumPromoted.empty() && !bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
                repairPromotions();
                return;
            }
            markEnumProgress();
            enumExchangeSucceeded = true;
            endEnumExchange();
            return;
        }
    }

    // 2. A late object for a recently ended exchange.
    if (enumLateActive && mp.from == enumLatePeer) {
        auto it = std::find(enumLateWanted.begin(), enumLateWanted.end(), shortId);
        if (it != enumLateWanted.end()) {
            storeRecoveredObject(replay, enumLateRoot, enumLateBucket);
            enumLateWanted.erase(it);
            if (enumLateWanted.empty())
                enumLateActive = false;
            return;
        }
    }

    // 3. Overheard: any complete, hash-verified object from the authority, named by its bucket
    // hint, whether or not an exchange is open. Item 6 until membership is authenticated.
    const uint32_t hint = t->chain_count;
    if (hint >= 0x100000)
        return;
    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root) == 0)
        return;
    if (isInDB(hash, sizeof(hash)))
        return;
    if (!storeRecoveredObject(replay, root, hint))
        return;

    // Item 7: suppress our own pending request for an object we just overheard.
    enumWanted.erase(std::remove(enumWanted.begin(), enumWanted.end(), shortId), enumWanted.end());
    enumWantedRemaining = enumWanted.size();
    if (enumLateActive) {
        enumLateWanted.erase(std::remove(enumLateWanted.begin(), enumLateWanted.end(), shortId), enumLateWanted.end());
        if (enumLateWanted.empty())
            enumLateActive = false;
    }
    if (enumExchangeActive && enumWanted.empty()) {
        enumExchangeSucceeded = true;
        endEnumExchange();
    }
}

bool StoreForwardPlusPlusModule::insertCanonObject(uint32_t to, uint32_t from, uint32_t packetId, const uint8_t *cipher,
                                                   size_t cipherLen, const uint8_t *hash, uint32_t rxTime,
                                                   const uint8_t *root_hash, uint32_t bucket)
{
    if (isInDB(hash, SFPP_HASH_SIZE))
        return true;

    // Bucket membership is a counter range, so any in-range counter stores the row in the right bucket.
    const uint32_t counter = bucket * sfppsr::kBucketObjects + 1;
    sqlite3_reset(chain_insert_stmt);
    sqlite3_bind_int64(chain_insert_stmt, 1, (sqlite3_int64)to);
    sqlite3_bind_int64(chain_insert_stmt, 2, (sqlite3_int64)from);
    sqlite3_bind_int64(chain_insert_stmt, 3, (sqlite3_int64)packetId);
    sqlite3_bind_blob(chain_insert_stmt, 4, root_hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_blob(chain_insert_stmt, 5, cipher, (int)cipherLen, SQLITE_STATIC);
    sqlite3_bind_blob(chain_insert_stmt, 6, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_int64(chain_insert_stmt, 7, (sqlite3_int64)rxTime);
    // A unique non-zero commit hash keeps trimOldestLink deleting one row rather than every zero row.
    sqlite3_bind_blob(chain_insert_stmt, 8, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_text(chain_insert_stmt, 9, "", 0, SQLITE_STATIC);
    sqlite3_bind_int64(chain_insert_stmt, 10, (sqlite3_int64)counter);
    sqlite3_bind_int64(chain_insert_stmt, 11, (sqlite3_int64)sfppsr::shortId(hash, SFPP_HASH_SIZE));
    sqlite3_bind_int64(chain_insert_stmt, 12, (sqlite3_int64)sfppsr::checksumContribution(hash, SFPP_HASH_SIZE));

    const int res = sqlite3_step(chain_insert_stmt);
    sqlite3_reset(chain_insert_stmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp cannot store a canonical object: %s", sqlite3_errmsg(ppDb));
        return false;
    }
    return true;
}

bool StoreForwardPlusPlusModule::storeRecoveredObject(const sfppframe::Replay &replay, const uint8_t *root_hash, uint32_t bucket)
{
    uint8_t hash[SFPP_HASH_SIZE];
    sfppframe::messageHash(replay.ciphertext.data(), replay.ciphertext.size(), replay.to, replay.from, replay.id, hash);
    if (isInDB(hash, sizeof(hash))) {
        removeFromScratch(hash, SFPP_HASH_SIZE);
        return true;
    }

    const uint32_t now = getValidTime(RTCQuality::RTCQualityNTP, true);
    const uint32_t rxTime = replay.heardAgoS <= now ? now - replay.heardAgoS : now;
    if (!insertCanonObject(replay.to, replay.from, replay.id, replay.ciphertext.data(), replay.ciphertext.size(), hash, rxTime,
                           root_hash, bucket))
        return false;
    // The object is canonical now, so a scratch copy (a writer-missed offer) is no longer needed.
    removeFromScratch(hash, SFPP_HASH_SIZE);
    return true;
}

void StoreForwardPlusPlusModule::promoteScratchMatches(const uint8_t *root_hash, uint32_t bucket,
                                                       std::vector<pinsketch::Element> &ids)
{
    if (ids.empty())
        return;

    struct ScratchRow {
        uint32_t to;
        uint32_t from;
        uint32_t id;
        uint32_t rxTime;
        std::vector<uint8_t> cipher;
        uint8_t hash[SFPP_HASH_SIZE];
    };
    std::vector<ScratchRow> matches;
    std::vector<std::vector<uint8_t>> corrupt;

    sqlite3_reset(fromScratchStmt);
    sqlite3_bind_blob(fromScratchStmt, 1, root_hash, SFPP_HASH_SIZE, NULL);
    while (sqlite3_step(fromScratchStmt) == SQLITE_ROW) {
        const uint8_t *stored = (const uint8_t *)sqlite3_column_blob(fromScratchStmt, 4);
        if (stored == nullptr)
            continue;
        const uint8_t *cipher = (const uint8_t *)sqlite3_column_blob(fromScratchStmt, 3);
        const int cipherLen = sqlite3_column_bytes(fromScratchStmt, 3);
        if (cipher == nullptr || cipherLen <= 0)
            continue;

        const uint32_t to = (uint32_t)sqlite3_column_int64(fromScratchStmt, 0);
        const uint32_t from = (uint32_t)sqlite3_column_int64(fromScratchStmt, 1);
        const uint32_t id = (uint32_t)sqlite3_column_int64(fromScratchStmt, 2);

        // The stored hash is only an index; recompute it from the object and refuse a mismatch.
        uint8_t computed[SFPP_HASH_SIZE];
        sfppframe::messageHash(cipher, (size_t)cipherLen, to, from, id, computed);
        if (memcmp(computed, stored, SFPP_HASH_SIZE) != 0) {
            corrupt.emplace_back(stored, stored + SFPP_HASH_SIZE);
            continue;
        }

        const pinsketch::Element sid = sfppsr::shortId(computed, SFPP_HASH_SIZE);
        if (std::find(ids.begin(), ids.end(), sid) == ids.end())
            continue;

        ScratchRow row;
        row.to = to;
        row.from = from;
        row.id = id;
        row.cipher.assign(cipher, cipher + cipherLen);
        memcpy(row.hash, computed, SFPP_HASH_SIZE);
        row.rxTime = (uint32_t)sqlite3_column_int64(fromScratchStmt, 5);
        matches.push_back(row);
    }
    sqlite3_reset(fromScratchStmt);

    for (const auto &bad : corrupt)
        removeFromScratch(bad.data(), SFPP_HASH_SIZE);

    for (const auto &row : matches) {
        insertCanonObject(row.to, row.from, row.id, row.cipher.data(), row.cipher.size(), row.hash, row.rxTime, root_hash,
                          bucket);
        removeFromScratch(row.hash, SFPP_HASH_SIZE);
        const pinsketch::Element sid = sfppsr::shortId(row.hash, SFPP_HASH_SIZE);
        enumPromoted.push_back({sid, {}});
        memcpy(enumPromoted.back().hash, row.hash, SFPP_HASH_SIZE);
        ids.erase(std::remove(ids.begin(), ids.end(), sid), ids.end());
    }
}

#endif // has include sqlite3