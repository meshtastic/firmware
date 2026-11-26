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
        time_basis, boot_id, age_margin) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
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
    if (pendingRun) {
        pendingRun = false;
        if (portduino_config.sfpp_repair == SFPP_REPAIR_CHAIN)
            setIntervalFromNow(portduino_config.sfpp_announce_interval * 60 * 1000 - 30 * 1000);
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

    // D7: canon rows outside the retention window are gone even when the row cap is not reached.
    trimCanonByRetention(root_hash_bytes, SFPP_HASH_SIZE);
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
                if (linkIsFresh(incoming_link)) {
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
                    if (linkIsFresh(incoming_link)) {
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
                    if (linkIsFresh(incoming_link)) {
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

    sfppframe::messageHash(encrypted_meshpacket->encrypted.bytes, encrypted_meshpacket->encrypted.size, mp.to, mp.from, mp.id,
                           lo.message_hash);
    lo.message_hash_len = SFPP_HASH_SIZE;

    lo.root_hash_len = getOrDeriveRootFromChannel(encrypted_meshpacket->channel, lo.root_hash);
    return lo;
}

StoreForwardPlusPlusModule *storeForwardPlusPlusModule;

#endif // has include sqlite3