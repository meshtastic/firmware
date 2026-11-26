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
#include "SfppInternal.h"
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

static_assert(sfppframe::kMessageHashBytes == SFPP_HASH_SIZE, "frame and store message-hash widths must agree");

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
        time_basis INT NOT NULL DEFAULT 0, \
        boot_id INT NOT NULL DEFAULT 0,    \
        age_margin INT NOT NULL DEFAULT 0, \
        bucket INT DEFAULT 0,          \
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
        time_basis INT NOT NULL DEFAULT 0, \
        boot_id INT NOT NULL DEFAULT 0,    \
        age_margin INT NOT NULL DEFAULT 0, \
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
        time_basis INT NOT NULL DEFAULT 0, \
        boot_id INT NOT NULL DEFAULT 0,    \
        age_margin INT NOT NULL DEFAULT 0, \
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
        time_basis INT NOT NULL DEFAULT 0, \
        boot_id INT NOT NULL DEFAULT 0,    \
        age_margin INT NOT NULL DEFAULT 0, \
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

    // One row of time state: the boot in progress and the last uptime/epoch seen in it, so a restart
    // can age a previous boot's rows and add the downtime when both ends had a proper clock.
    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        time_state(                    \
        id INT PRIMARY KEY,            \
        boot_id INT NOT NULL DEFAULT 0,    \
        boot_epoch INT NOT NULL DEFAULT 0, \
        last_uptime INT NOT NULL DEFAULT 0,\
        last_epoch INT NOT NULL DEFAULT 0  \
        );",
                       NULL, NULL, &err);
    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    sqlite3_exec(ppDb, ("PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";").c_str(), NULL, NULL, NULL);

    // prepared statements *should* make this faster.
    sqlite3_prepare_v2(ppDb, "INSERT INTO channel_messages (destination, sender, packet_id, root_hash, \
        encrypted_bytes, message_hash, rx_time, commit_hash, payload, counter, short_id, checksum, \
        time_basis, boot_id, age_margin, bucket) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
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

    sqlite3_prepare_v2(ppDb, "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, counter, \
        time_basis, boot_id, age_margin from channel_messages \
        where substr(root_hash,1,?)=? and counter>=? and counter<=? and short_id=?;",
                       -1, &getLinkByShortIdStmt, NULL);

    sqlite3_prepare_v2(ppDb, "delete from channel_messages where message_hash=?;", -1, &deleteCanonStmt, NULL);

    // Legacy CLIENT_HISTORY reads the stored plaintext, or the ciphertext a follower holds instead.
    // Window and cursor are applied per row after ageing, because the two bases cannot be compared raw.
    sqlite3_prepare_v2(ppDb,
                       "select sender, packet_id, destination, rx_time, payload, encrypted_bytes, time_basis, boot_id, "
                       "age_margin, rowid from channel_messages where substr(root_hash,1,?)=? and sender!=? "
                       "and (destination=? or destination=?) order by rowid asc;",
                       -1, &legacyHistoryStmt, NULL);

    sqlite3_prepare_v2(ppDb, "INSERT INTO local_messages (destination, sender, packet_id, root_hash, \
        encrypted_bytes, message_hash, rx_time, payload, time_basis, boot_id, age_margin) \
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &scratch_insert_stmt, NULL);

    sqlite3_prepare_v2(ppDb, "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, root_hash, \
        time_basis, boot_id, age_margin from local_messages where root_hash=? order by rx_time asc;", // earliest first
                       -1, &fromScratchStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, root_hash, payload, \
        time_basis, boot_id, age_margin from local_messages \
        where substr(message_hash,1,?)=? order by rx_time asc LIMIT 1;", // earliest first
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
        "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, commit_hash, root_hash, counter, payload, \
        time_basis, boot_id, age_margin from channel_messages where substr(commit_hash,1,?)=?;",
        -1, &getLinkStmt, NULL);

    sqlite3_prepare_v2(
        ppDb,
        "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, commit_hash, root_hash, counter, payload, \
        time_basis, boot_id, age_margin from channel_messages where substr(message_hash,1,?)=?;",
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

    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from local_messages;", -1,
                       &scratchTimeRowsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM local_messages WHERE rowid=?;", -1, &deleteScratchRowidStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "DELETE FROM channel_messages WHERE commit_hash in ( select commit_hash from channel_messages where "
                       "substr(root_hash,1,?)=? ORDER BY rowid ASC LIMIT 1);",
                       -1, &trimOldestLinkStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "select rowid, rx_time, time_basis, boot_id, age_margin from channel_messages "
                       "where substr(root_hash,1,?)=?;",
                       -1, &canonTimeRowsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM channel_messages WHERE rowid=?;", -1, &deleteCanonRowidStmt, NULL);

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
        encrypted_bytes, message_hash, rx_time, commit_hash, payload, counter, time_basis, boot_id, age_margin) \
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &canon_scratch_insert_stmt, NULL);

    sqlite3_prepare_v2(ppDb, "SELECT count(*) FROM canon_scratch WHERE substr(message_hash,1,?)=?;", -1,
                       &getCanonScratchCountStmt, NULL);

    sqlite3_prepare_v2(
        ppDb,
        "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, commit_hash, root_hash, counter, payload, \
        time_basis, boot_id, age_margin from canon_scratch where substr(root_hash,1,?)=? ORDER BY counter ASC;",
        -1, &getCanonScratchStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE from canon_scratch where substr(message_hash,1,?)=?", -1, &removeCanonScratch, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE from canon_scratch where substr(root_hash,1,?)=? AND counter < ?;", -1,
                       &clearCanonScratchStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select boot_id, last_uptime, last_epoch from time_state where id=1;", -1, &timeStateReadStmt, NULL);
    sqlite3_prepare_v2(ppDb,
                       "INSERT OR REPLACE INTO time_state (id, boot_id, boot_epoch, last_uptime, last_epoch) "
                       "VALUES(1, ?, ?, ?, ?);",
                       -1, &timeStateWriteStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from channel_messages;", -1,
                       &restampChannelStmt, NULL);
    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from local_messages;", -1,
                       &restampScratchStmt, NULL);
    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from canon_scratch;", -1,
                       &restampCanonScratchStmt, NULL);
    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from direct_messages;", -1,
                       &restampDirectStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update channel_messages set rx_time=?, time_basis=?, boot_id=?, age_margin=? where rowid=?;", -1,
                       &updateChannelTimeStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update local_messages set rx_time=?, time_basis=?, boot_id=?, age_margin=? where rowid=?;", -1,
                       &updateScratchTimeStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update canon_scratch set rx_time=?, time_basis=?, boot_id=?, age_margin=? where rowid=?;", -1,
                       &updateCanonScratchTimeStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update direct_messages set rx_time=?, time_basis=?, boot_id=?, age_margin=? where rowid=?;", -1,
                       &updateDirectTimeStmt, NULL);

    encryptedOk = true;

    initTimeState();
    restampPreviousBoot();
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
        scratchTimeRowsStmt,
        deleteScratchRowidStmt,
        trimOldestLinkStmt,
        canonTimeRowsStmt,
        deleteCanonRowidStmt,
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
        timeStateReadStmt,
        timeStateWriteStmt,
        restampChannelStmt,
        restampScratchStmt,
        restampCanonScratchStmt,
        restampDirectStmt,
        updateChannelTimeStmt,
        updateScratchTimeStmt,
        updateCanonScratchTimeStmt,
        updateDirectTimeStmt,
    };
    for (sqlite3_stmt *stmt : statements) {
        if (stmt)
            sqlite3_finalize(stmt);
    }
    if (ppDb)
        sqlite3_close(ppDb);
}

int32_t StoreForwardPlusPlusModule::runOnce()
{
    if (legacyHistoryBusy) {
        sendNextLegacyHistory();
        return legacyHistoryBusy ? 5 * 1000 : portduino_config.sfpp_announce_interval * 60 * 1000;
    }
    // G19: promote the host's synchronized clock to NTP when we have no proper source yet, but never
    // defer on it; a store without one works in boot-relative ages.
    if (getRTCQuality() < RTCQualityNTP) {
        std::string timeCommandResult = exec("timedatectl status | grep synchronized | grep yes -c");
        if (timeCommandResult[0] == '1') {
            struct timeval tv;
            tv.tv_sec = time(NULL);
            tv.tv_usec = 0;
            perhapsSetRTC(RTCQualityNTP, &tv);
        }
    }
    updateTimeState();
    healBootRelativeTimestamps();

    uint8_t root_hash_bytes[SFPP_HASH_SIZE] = {0};
    ChannelHash hash = channels.getHash(0);

    if (getOrDeriveRootFromChannel(hash, root_hash_bytes) == 0) {
        LOG_WARN("No root hash found, not sending");
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    // D7: canon rows outside the retention window are gone even when the row cap is not reached.
    trimCanonByRetention(root_hash_bytes, SFPP_HASH_SIZE);
    // evict old messages from scratch
    pruneScratchQueue();

    if (memfll(root_hash_bytes, '\0', SFPP_HASH_SIZE)) {
        LOG_WARN("No root hash found, not sending");
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    // One bucket is advertised per tick; the next advert is the retry for an unfinished exchange.
    {
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
    insertCanonObject(lo.to, lo.from, lo.id, lo.encrypted_bytes, lo.encrypted_len, lo.message_hash,
                      RowTime{lo.rx_time, lo.time_basis, lo.boot_id, lo.age_margin}, lo.root_hash,
                      sfppsr::contentBucket(lo.message_hash, lo.message_hash_len));
}

ProcessMessage StoreForwardPlusPlusModule::handleReceived(const meshtastic_MeshPacket &mp)
{
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
        insertCanonObject(lo.to, lo.from, lo.id, lo.encrypted_bytes, lo.encrypted_len, lo.message_hash,
                          RowTime{lo.rx_time, lo.time_basis, lo.boot_id, lo.age_margin}, lo.root_hash,
                          sfppsr::contentBucket(lo.message_hash, lo.message_hash_len));
    }
}

bool StoreForwardPlusPlusModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
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
}

// should not need size considerations
StoreForwardPlusPlusModule::link_object
StoreForwardPlusPlusModule::ingestTextPacket(const meshtastic_MeshPacket &mp, const meshtastic_MeshPacket *encrypted_meshpacket)
{
    link_object lo;
    lo.to = mp.to;
    lo.from = mp.from;
    lo.id = mp.id;
    // Keep the age the packet arrived with; a live packet is normally zero, but a replay or a
    // queued API packet can be older. rx_time is never trusted when it points into the future.
    RowTime live = nowTime();
    RowTime stamped;
    const uint32_t heardAgo = (mp.rx_time != 0 && mp.rx_time <= live.stamp) ? live.stamp - mp.rx_time : 0;
    if (!timeFromAge(heardAgo, stamped))
        stamped = live;
    lo.rx_time = stamped.stamp;
    lo.time_basis = stamped.basis;
    lo.boot_id = stamped.boot_id;
    lo.age_margin = stamped.margin;
    lo.channel_hash = encrypted_meshpacket->channel;
    memcpy(lo.encrypted_bytes, encrypted_meshpacket->encrypted.bytes, encrypted_meshpacket->encrypted.size);
    lo.encrypted_len = encrypted_meshpacket->encrypted.size;
    lo.payload = std::string((char *)mp.decoded.payload.bytes, mp.decoded.payload.size);
    lo.root_hash_len = getOrDeriveRootFromChannel(encrypted_meshpacket->channel, lo.root_hash);
    return lo;
}

StoreForwardPlusPlusModule *storeForwardPlusPlusModule;

#endif // has include sqlite3