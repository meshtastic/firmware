#if __has_include("sqlite3.h")

#include "MeshService.h"
#include "RTC.h"
#include "SHA256.h"
#include "SfppFrame.h"
#include "SfppInternal.h"
#include "StoreForwardPlusPlus.h"
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

// Retention applies to canon rows as well as to the legacy read window.
static constexpr uint32_t kRetentionS = 48 * 60 * 60;

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

SfppStore::~SfppStore()
{
    sqlite3_stmt *statements[] = {
        canon_insert_stmt,      scratch_insert_stmt,   identityCanonStmt,      identityScratchStmt,    replaceCanonCopyStmt,
        replaceScratchCopyStmt, checkDupMessageHash,   checkScratch,           removeScratch,          updatePayloadStmt,
        fromScratchStmt,        fromScratchByHashStmt, getHashFromRootStmt,    addRootToMappingsStmt,  getFullRootHashStmt,
        getScratchCountStmt,    scratchTimeRowsStmt,   deleteScratchRowidStmt, canonTimeRowsStmt,      deleteCanonRowidStmt,
        bucketSummaryStmt,      bucketIdsStmt,         distinctBucketsStmt,    getObjectByShortIdStmt, deleteCanonStmt,
        unindexedObjectsStmt,   setObjectIndexStmt,    legacyHistoryStmt,      timeStateReadStmt,      timeStateWriteStmt,
        restampChannelStmt,     restampScratchStmt,    restampDirectStmt,      updateChannelTimeStmt,  updateScratchTimeStmt,
        updateDirectTimeStmt,
    };
    for (sqlite3_stmt *stmt : statements) {
        if (stmt)
            sqlite3_finalize(stmt);
    }
    if (ppDb)
        sqlite3_close(ppDb);
}

void SfppStore::openStore(const uint8_t *derivedRoot, bool haveDerived)
{
    std::string db_path = portduino_config.sfpp_db_path + "storeforwardpp.db";
    LOG_INFO("Opening StoreForwardpp DB at %s", db_path.c_str());
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
                    if (root && len == (int)SFPP_HASH_SIZE && memcmp(root, derivedRoot, SFPP_HASH_SIZE) == 0)
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
        payload TEXT,                  \
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

    // mappings table -- connects the root hashes to channel hashes and DM identifiers. Keyed by the
    // root, not the 1-byte identifier, so two channels that share a hash keep separate stores.
    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        mappings(                      \
        chain_type INT NOT NULL,       \
        identifier INT NOT NULL,       \
        root_hash BLOB NOT NULL,       \
        PRIMARY KEY (root_hash)        \
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
        encrypted_bytes, message_hash, rx_time, payload, short_id, checksum, time_basis, boot_id, age_margin, bucket) \
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &canon_insert_stmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "select message_hash from channel_messages where sender=? and packet_id=? and destination=? "
                       "and substr(root_hash,1,?)=? order by rowid limit 1;",
                       -1, &identityCanonStmt, NULL);
    sqlite3_prepare_v2(ppDb,
                       "select message_hash from local_messages where sender=? and packet_id=? and destination=? "
                       "and substr(root_hash,1,?)=? order by rowid limit 1;",
                       -1, &identityScratchStmt, NULL);
    sqlite3_prepare_v2(ppDb,
                       "update channel_messages set encrypted_bytes=?, message_hash=?, short_id=?, checksum=?, bucket=? "
                       "where message_hash=?;",
                       -1, &replaceCanonCopyStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update local_messages set encrypted_bytes=?, message_hash=? where message_hash=?;", -1,
                       &replaceScratchCopyStmt, NULL);

    // Both identifiers are stored beside the object, so summarising a bucket never rehashes and a
    // restart costs a query rather than a pass over the store.
    sqlite3_prepare_v2(ppDb, "select short_id, checksum from channel_messages \
        where substr(root_hash,1,?)=? and bucket=? and short_id is not null;",
                       -1, &bucketSummaryStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select message_hash from channel_messages where short_id is null limit 128;", -1,
                       &unindexedObjectsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "update channel_messages set short_id=?, checksum=? where message_hash=?;", -1, &setObjectIndexStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "select short_id from channel_messages \
        where substr(root_hash,1,?)=? and bucket=? and short_id is not null order by short_id;",
                       -1, &bucketIdsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select distinct bucket from channel_messages \
        where substr(root_hash,1,?)=? and short_id is not null order by bucket;",
                       -1, &distinctBucketsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, \
        time_basis, boot_id, age_margin from channel_messages where substr(root_hash,1,?)=? and bucket=? and short_id=?;",
                       -1, &getObjectByShortIdStmt, NULL);

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

    sqlite3_prepare_v2(ppDb, "SELECT COUNT(*) from local_messages where substr(message_hash,1,?)=?", -1, &checkScratch, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE from local_messages where substr(message_hash,1,?)=?", -1, &removeScratch, NULL);

    sqlite3_prepare_v2(ppDb, "UPDATE channel_messages SET payload=? WHERE substr(message_hash,1,?)=?", -1, &updatePayloadStmt,
                       NULL);
    sqlite3_prepare_v2(ppDb, "select payload from channel_messages where substr(message_hash,1,?)=? limit 1;", -1,
                       &payloadForHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select identifier from mappings where substr(root_hash,1,?)=?;", -1, &getHashFromRootStmt, NULL);

    sqlite3_prepare_v2(ppDb, "INSERT OR REPLACE INTO mappings (chain_type, identifier, root_hash) VALUES(?, ?, ?);", -1,
                       &addRootToMappingsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select root_hash from mappings where substr(root_hash,1,?)=?;", -1, &getFullRootHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "SELECT count(*) FROM local_messages WHERE substr(root_hash,1,?)=?;", -1, &getScratchCountStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from local_messages;", -1,
                       &scratchTimeRowsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM local_messages WHERE rowid=?;", -1, &deleteScratchRowidStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "select rowid, rx_time, time_basis, boot_id, age_margin from channel_messages "
                       "where substr(root_hash,1,?)=?;",
                       -1, &canonTimeRowsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM channel_messages WHERE rowid=?;", -1, &deleteCanonRowidStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select boot_id, last_uptime, last_epoch from time_state where id=1;", -1, &timeStateReadStmt, NULL);
    sqlite3_prepare_v2(ppDb,
                       "INSERT OR REPLACE INTO time_state (id, boot_id, boot_epoch, last_uptime, last_epoch) "
                       "VALUES(1, ?, ?, ?, ?);",
                       -1, &timeStateWriteStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from channel_messages;", -1,
                       &restampChannelStmt, NULL);
    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from local_messages;", -1,
                       &restampScratchStmt, NULL);
    sqlite3_prepare_v2(ppDb, "select rowid, rx_time, time_basis, boot_id, age_margin from direct_messages;", -1,
                       &restampDirectStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update channel_messages set rx_time=?, time_basis=?, boot_id=?, age_margin=? where rowid=?;", -1,
                       &updateChannelTimeStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update local_messages set rx_time=?, time_basis=?, boot_id=?, age_margin=? where rowid=?;", -1,
                       &updateScratchTimeStmt, NULL);
    sqlite3_prepare_v2(ppDb, "update direct_messages set rx_time=?, time_basis=?, boot_id=?, age_margin=? where rowid=?;", -1,
                       &updateDirectTimeStmt, NULL);

    initTimeState();
    restampPreviousBoot();
    backfillSetIndex();
}

void SfppStore::initTimeState()
{
    prevBootId = prevLastUptime = prevLastEpoch = 0;
    sqlite3_reset(timeStateReadStmt);
    if (sqlite3_step(timeStateReadStmt) == SQLITE_ROW) {
        prevBootId = (uint32_t)sqlite3_column_int64(timeStateReadStmt, 0);
        prevLastUptime = (uint32_t)sqlite3_column_int64(timeStateReadStmt, 1);
        prevLastEpoch = (uint32_t)sqlite3_column_int64(timeStateReadStmt, 2);
    }
    sqlite3_reset(timeStateReadStmt);

    bootId = prevBootId == UINT32_MAX ? 1 : prevBootId + 1;
    if (bootId == 0)
        bootId = 1;
    bootEpoch = getValidTime(RTCQualityNTP, true);
    lastSeenUptime = sfppUptimeSecs();
    lastSeenEpoch = bootEpoch;
    writeTimeState();
}

void SfppStore::writeTimeState()
{
    sqlite3_reset(timeStateWriteStmt);
    sqlite3_bind_int64(timeStateWriteStmt, 1, (sqlite3_int64)bootId);
    sqlite3_bind_int64(timeStateWriteStmt, 2, (sqlite3_int64)bootEpoch);
    sqlite3_bind_int64(timeStateWriteStmt, 3, (sqlite3_int64)lastSeenUptime);
    sqlite3_bind_int64(timeStateWriteStmt, 4, (sqlite3_int64)lastSeenEpoch);
    sqlite3_step(timeStateWriteStmt);
    sqlite3_reset(timeStateWriteStmt);
}

void StoreForwardPlusPlusModule::updateTimeState()
{
    const uint32_t nowUptime = sfppUptimeSecs();
    const uint32_t nowEpoch = getValidTime(RTCQualityNTP, true);
    if (nowEpoch) {
        store_.lastSeenEpoch = nowEpoch;
        // A clock that arrived after boot still pins the boot start for ageing epoch rows.
        if (store_.bootEpoch == 0)
            store_.bootEpoch = nowEpoch > nowUptime ? nowEpoch - nowUptime : 0;
    }
    store_.lastSeenUptime = nowUptime;
    store_.writeTimeState();
}

uint32_t StoreForwardPlusPlusModule::estimateEpochNow() const
{
    const uint32_t proper = getValidTime(RTCQualityNTP, true);
    if (proper)
        return proper;
    // Mesh/device time may age an existing epoch but never stamps a new row.
    const uint32_t rough = getValidTime(RTCQualityDevice, true);
    if (rough)
        return rough;
    const uint32_t nowUptime = sfppUptimeSecs();
    if (store_.bootEpoch > 0)
        return store_.bootEpoch + nowUptime;
    if (store_.lastSeenEpoch > 0)
        return store_.lastSeenEpoch + (nowUptime - store_.lastSeenUptime);
    return 0;
}

StoreForwardPlusPlusModule::RowTime StoreForwardPlusPlusModule::nowTime() const
{
    RowTime t;
    const uint32_t nowEpoch = getValidTime(RTCQualityNTP, true);
    if (nowEpoch) {
        t.basis = kTimeBasisEpoch;
        t.stamp = nowEpoch;
    } else {
        t.basis = kTimeBasisUptime;
        t.stamp = sfppUptimeSecs();
        t.boot_id = store_.bootId;
    }
    return t;
}

bool StoreForwardPlusPlusModule::timeFromAge(uint32_t heardAgo, RowTime &out) const
{
    out = nowTime();
    if (heardAgo > out.stamp)
        return false; // an age that would put the object before its own basis is not plausible
    out.stamp -= heardAgo;
    out.margin = 0;
    return true;
}

StoreForwardPlusPlusModule::RowAge StoreForwardPlusPlusModule::rowAge(const RowTime &t) const
{
    RowAge a;
    a.margin = kTimeMarginS + t.margin;
    const uint32_t nowUptime = sfppUptimeSecs();

    if (t.basis == kTimeBasisEpoch) {
        a.has_epoch = true;
        a.epoch = t.stamp;
        a.estimate_epoch = t.stamp;
        const uint32_t nowEpoch = estimateEpochNow();
        if (nowEpoch == 0) {
            a.margin = kRetentionS; // no reference to date against; keep the row out of every trim
            return a;
        }
        if (t.stamp > nowEpoch + a.margin) {
            a.margin += t.stamp - nowEpoch;
        } else {
            a.age = t.stamp < nowEpoch ? nowEpoch - t.stamp : 0;
        }
        return a;
    }

    if (t.basis == kTimeBasisUptime) {
        if (t.boot_id == store_.bootId) {
            if (t.stamp > nowUptime + a.margin)
                a.margin += t.stamp - nowUptime;
            else
                a.age = t.stamp < nowUptime ? nowUptime - t.stamp : 0;
        } else {
            // A row from a boot that was not restamped: age it to the previous end, add the gap we
            // know about and the uptime since this boot started.
            const uint32_t atEnd =
                (t.boot_id == store_.prevBootId && store_.prevLastUptime > t.stamp) ? store_.prevLastUptime - t.stamp : 0;
            const uint32_t down = (store_.bootEpoch > 0 && store_.prevLastEpoch > 0 && store_.bootEpoch >= store_.prevLastEpoch)
                                      ? store_.bootEpoch - store_.prevLastEpoch
                                      : 0;
            a.age = atEnd > UINT32_MAX - down ? UINT32_MAX : atEnd + down;
            a.age = a.age > UINT32_MAX - nowUptime ? UINT32_MAX : a.age + nowUptime;
        }
    } else {
        a.age = t.stamp > UINT32_MAX - nowUptime ? UINT32_MAX : t.stamp + nowUptime;
    }

    const uint32_t nowEpoch = estimateEpochNow();
    if (nowEpoch == 0) {
        a.margin = kRetentionS;
    } else if (nowEpoch > a.age) {
        a.estimate_epoch = nowEpoch - a.age;
    }
    return a;
}

// Restamps rows written by an earlier boot. The age at that boot's last observed second is kept;
// a known gap is added, and an unknown one only widens the margin. A module restart in the same
// boot (uptime did not go backwards) keeps its stamps as they are.
void SfppStore::restampPreviousBoot()
{
    const uint32_t nowUptime = sfppUptimeSecs();
    const bool continuousBoot = prevLastUptime > 0 && nowUptime >= prevLastUptime;
    const bool downKnown = bootEpoch > 0 && prevLastEpoch > 0 && bootEpoch >= prevLastEpoch;
    const uint32_t down = downKnown ? bootEpoch - prevLastEpoch : 0;

    auto restamp = [&](sqlite3_stmt *sel, sqlite3_stmt *upd) {
        if (!sel || !upd)
            return;
        std::vector<std::pair<sqlite3_int64, RowTime>> changes;
        sqlite3_reset(sel);
        while (sqlite3_step(sel) == SQLITE_ROW) {
            RowTime old;
            const sqlite3_int64 rowid = sqlite3_column_int64(sel, 0);
            old.stamp = (uint32_t)sqlite3_column_int64(sel, 1);
            old.basis = (uint8_t)sqlite3_column_int64(sel, 2);
            old.boot_id = (uint32_t)sqlite3_column_int64(sel, 3);
            old.margin = (uint32_t)sqlite3_column_int64(sel, 4);
            if (old.basis == kTimeBasisEpoch)
                continue; // an epoch needs no restamp: its age is now - stamp

            RowTime next = old;
            if (continuousBoot) {
                if (old.basis == kTimeBasisUptime && old.boot_id == prevBootId) {
                    next.boot_id = bootId; // same uptime base: keep the stamp, name the new module boot
                    changes.emplace_back(rowid, next);
                }
                continue;
            }

            uint32_t ageAtEnd = 0;
            if (old.basis == kTimeBasisUptime)
                ageAtEnd = (old.boot_id == prevBootId && prevLastUptime > old.stamp) ? prevLastUptime - old.stamp : 0;
            else
                ageAtEnd = old.stamp > UINT32_MAX - prevLastUptime ? UINT32_MAX : old.stamp + prevLastUptime;
            const uint32_t total = ageAtEnd > UINT32_MAX - down ? UINT32_MAX : ageAtEnd + down;

            // An unknown gap widens the margin to the retention horizon, never past it.
            const uint32_t extra = downKnown ? 0 : kRetentionS;
            const uint32_t widened = old.margin > UINT32_MAX - extra ? UINT32_MAX : old.margin + extra;
            next.margin = widened > kRetentionS ? kRetentionS : widened;
            next.boot_id = 0;
            if (bootEpoch > 0) {
                next.basis = kTimeBasisEpoch;
                next.stamp = bootEpoch > total ? bootEpoch - total : 0;
            } else {
                next.basis = kTimeBasisAge;
                next.stamp = total;
            }
            changes.emplace_back(rowid, next);
        }
        sqlite3_reset(sel);
        for (const auto &change : changes) {
            sqlite3_reset(upd);
            sqlite3_bind_int64(upd, 1, (sqlite3_int64)change.second.stamp);
            sqlite3_bind_int64(upd, 2, (sqlite3_int64)change.second.basis);
            sqlite3_bind_int64(upd, 3, (sqlite3_int64)change.second.boot_id);
            sqlite3_bind_int64(upd, 4, (sqlite3_int64)change.second.margin);
            sqlite3_bind_int64(upd, 5, change.first);
            sqlite3_step(upd);
            sqlite3_reset(upd);
        }
    };

    restamp(restampChannelStmt, updateChannelTimeStmt);
    restamp(restampScratchStmt, updateScratchTimeStmt);
    restamp(restampDirectStmt, updateDirectTimeStmt);
}

// On the first proper clock, every boot-relative row becomes an epoch with its age unchanged, so
// the 48 h trim cannot mistake an uptime stamp for an ancient epoch.
void StoreForwardPlusPlusModule::healBootRelativeTimestamps()
{
    const uint32_t nowEpoch = getValidTime(RTCQualityNTP, true);
    if (nowEpoch == 0)
        return;

    auto heal = [&](sqlite3_stmt *sel, sqlite3_stmt *upd) {
        if (!sel || !upd)
            return;
        std::vector<std::pair<sqlite3_int64, RowTime>> changes;
        sqlite3_reset(sel);
        while (sqlite3_step(sel) == SQLITE_ROW) {
            RowTime old;
            const sqlite3_int64 rowid = sqlite3_column_int64(sel, 0);
            old.stamp = (uint32_t)sqlite3_column_int64(sel, 1);
            old.basis = (uint8_t)sqlite3_column_int64(sel, 2);
            old.boot_id = (uint32_t)sqlite3_column_int64(sel, 3);
            old.margin = (uint32_t)sqlite3_column_int64(sel, 4);
            if (old.basis == kTimeBasisEpoch)
                continue;

            const RowAge age = rowAge(old);
            RowTime next;
            next.basis = kTimeBasisEpoch;
            next.stamp = age.age < nowEpoch ? nowEpoch - age.age : 0;
            next.margin = old.margin;
            changes.emplace_back(rowid, next);
        }
        sqlite3_reset(sel);
        for (const auto &change : changes) {
            sqlite3_reset(upd);
            sqlite3_bind_int64(upd, 1, (sqlite3_int64)change.second.stamp);
            sqlite3_bind_int64(upd, 2, (sqlite3_int64)change.second.basis);
            sqlite3_bind_int64(upd, 3, (sqlite3_int64)change.second.boot_id);
            sqlite3_bind_int64(upd, 4, (sqlite3_int64)change.second.margin);
            sqlite3_bind_int64(upd, 5, change.first);
            sqlite3_step(upd);
            sqlite3_reset(upd);
        }
    };

    heal(store_.restampChannelStmt, store_.updateChannelTimeStmt);
    heal(store_.restampScratchStmt, store_.updateScratchTimeStmt);
    heal(store_.restampDirectStmt, store_.updateDirectTimeStmt);
}

void StoreForwardPlusPlusModule::trimCanonByRetention(uint8_t *root_hash, size_t root_hash_len)
{
    std::vector<sqlite3_int64> drop;
    sqlite3_reset(store_.canonTimeRowsStmt);
    sqlite3_bind_int(store_.canonTimeRowsStmt, 1, root_hash_len);
    sqlite3_bind_blob(store_.canonTimeRowsStmt, 2, root_hash, root_hash_len, SQLITE_STATIC);
    while (sqlite3_step(store_.canonTimeRowsStmt) == SQLITE_ROW) {
        RowTime t;
        const sqlite3_int64 rowid = sqlite3_column_int64(store_.canonTimeRowsStmt, 0);
        t.stamp = (uint32_t)sqlite3_column_int64(store_.canonTimeRowsStmt, 1);
        t.basis = (uint8_t)sqlite3_column_int64(store_.canonTimeRowsStmt, 2);
        t.boot_id = (uint32_t)sqlite3_column_int64(store_.canonTimeRowsStmt, 3);
        t.margin = (uint32_t)sqlite3_column_int64(store_.canonTimeRowsStmt, 4);
        if (rowAge(t).age >= kRetentionS)
            drop.push_back(rowid);
    }
    sqlite3_reset(store_.canonTimeRowsStmt);

    for (sqlite3_int64 rowid : drop) {
        sqlite3_reset(store_.deleteCanonRowidStmt);
        sqlite3_bind_int64(store_.deleteCanonRowidStmt, 1, rowid);
        sqlite3_step(store_.deleteCanonRowidStmt);
        sqlite3_reset(store_.deleteCanonRowidStmt);
    }
}

bool StoreForwardPlusPlusModule::linkIsFresh(const stored_object &lo) const
{
    RowTime t;
    t.stamp = lo.rx_time;
    t.basis = kTimeBasisEpoch; // the wire's rxtime is an epoch claim; an unusable one counts as stale
    const RowAge age = rowAge(t);
    return age.age + age.margin <= rebroadcastTimeout;
}

void SfppStore::backfillSetIndex()
{
    uint32_t indexed = 0;

    // One batch per pass: the select finds rows the update then fills, so re-running it is how the
    // loop advances. A store written before these columns existed converges in one pass per 128 rows.
    while (true) {
        std::vector<std::pair<uint32_t, uint64_t>> derived;
        std::vector<std::vector<uint8_t>> hashes;

        sqlite3_reset(unindexedObjectsStmt);
        while (sqlite3_step(unindexedObjectsStmt) == SQLITE_ROW) {
            const uint8_t *hash = (const uint8_t *)sqlite3_column_blob(unindexedObjectsStmt, 0);
            const int len = sqlite3_column_bytes(unindexedObjectsStmt, 0);
            if (hash == nullptr || len <= 0)
                continue;
            hashes.emplace_back(hash, hash + len);
            derived.emplace_back(sfppsr::shortId(hash, len), sfppsr::checksumContribution(hash, len));
        }
        sqlite3_reset(unindexedObjectsStmt);

        if (hashes.empty())
            break;

        for (size_t i = 0; i < hashes.size(); i++) {
            sqlite3_reset(setObjectIndexStmt);
            sqlite3_bind_int64(setObjectIndexStmt, 1, derived[i].first);
            sqlite3_bind_int64(setObjectIndexStmt, 2, (sqlite3_int64)derived[i].second);
            sqlite3_bind_blob(setObjectIndexStmt, 3, hashes[i].data(), hashes[i].size(), NULL);
            if (sqlite3_step(setObjectIndexStmt) != SQLITE_DONE) {
                LOG_ERROR("StoreForwardpp cannot index object: %s", sqlite3_errmsg(ppDb));
                sqlite3_reset(setObjectIndexStmt);
                return; // an update that does not stick would loop the select forever
            }
            indexed++;
        }
        sqlite3_reset(setObjectIndexStmt);
    }

    if (indexed > 0)
        LOG_INFO("StoreForwardpp indexed %u existing objects for set reconciliation", indexed);
}

bool StoreForwardPlusPlusModule::buildBucketSummary(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket,
                                                    sfppsr::BucketSummary &out)
{
    out.clear();

    sqlite3_reset(store_.bucketSummaryStmt);
    sqlite3_bind_int(store_.bucketSummaryStmt, 1, root_hash_len);
    sqlite3_bind_blob(store_.bucketSummaryStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(store_.bucketSummaryStmt, 3, (int)bucket);

    while (sqlite3_step(store_.bucketSummaryStmt) == SQLITE_ROW) {
        out.add((pinsketch::Element)sqlite3_column_int64(store_.bucketSummaryStmt, 0),
                (uint64_t)sqlite3_column_int64(store_.bucketSummaryStmt, 1));
    }
    sqlite3_reset(store_.bucketSummaryStmt);

    return out.count() > 0;
}

bool StoreForwardPlusPlusModule::collectBucketIds(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket,
                                                  std::vector<pinsketch::Element> &out)
{
    out.clear();
    sqlite3_reset(store_.bucketIdsStmt);
    sqlite3_bind_int(store_.bucketIdsStmt, 1, root_hash_len);
    sqlite3_bind_blob(store_.bucketIdsStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(store_.bucketIdsStmt, 3, (int)bucket);

    while (sqlite3_step(store_.bucketIdsStmt) == SQLITE_ROW)
        out.push_back((pinsketch::Element)sqlite3_column_int64(store_.bucketIdsStmt, 0));
    sqlite3_reset(store_.bucketIdsStmt);

    return !out.empty();
}

bool StoreForwardPlusPlusModule::collectBuckets(const uint8_t *root_hash, size_t root_hash_len, std::vector<uint32_t> &out)
{
    out.clear();
    sqlite3_reset(store_.distinctBucketsStmt);
    sqlite3_bind_int(store_.distinctBucketsStmt, 1, root_hash_len);
    sqlite3_bind_blob(store_.distinctBucketsStmt, 2, root_hash, root_hash_len, NULL);
    while (sqlite3_step(store_.distinctBucketsStmt) == SQLITE_ROW)
        out.push_back((uint32_t)sqlite3_column_int64(store_.distinctBucketsStmt, 0));
    sqlite3_reset(store_.distinctBucketsStmt);
    return !out.empty();
}

//
bool StoreForwardPlusPlusModule::addToScratch(stored_object &lo, bool senderCopy)
{
    IdentityHit hit;
    if (findIdentityCopy(lo.to, lo.from, lo.id, lo.root_hash, hit)) {
        if (memcmp(hit.hash, lo.message_hash, SFPP_HASH_SIZE) == 0)
            return true;
        if (!senderCopy)
            return true; // first copy wins unless the sender's copy arrives
        if (isInDB(lo.message_hash, SFPP_HASH_SIZE)) {
            LOG_WARN("StoreForwardpp identity copy would collide with another object");
            return true;
        }
        return replaceIdentityCopy(hit, lo.message_hash, lo.encrypted_bytes, lo.encrypted_len);
    }

    // push a message into the canonical store
    // destination
    sqlite3_bind_int64(store_.scratch_insert_stmt, 1, (sqlite3_int64)lo.to);
    // sender
    sqlite3_bind_int64(store_.scratch_insert_stmt, 2, (sqlite3_int64)lo.from);
    // packet_id
    sqlite3_bind_int64(store_.scratch_insert_stmt, 3, (sqlite3_int64)lo.id);
    // root_hash
    sqlite3_bind_blob(store_.scratch_insert_stmt, 4, lo.root_hash, SFPP_HASH_SIZE, NULL);
    // encrypted_bytes
    sqlite3_bind_blob(store_.scratch_insert_stmt, 5, lo.encrypted_bytes, lo.encrypted_len, NULL);
    // message_hash
    sqlite3_bind_blob(store_.scratch_insert_stmt, 6, lo.message_hash, SFPP_HASH_SIZE, NULL);
    // rx_time
    sqlite3_bind_int(store_.scratch_insert_stmt, 7, lo.rx_time);
    // payload
    sqlite3_bind_text(store_.scratch_insert_stmt, 8, lo.payload.c_str(), lo.payload.length(), NULL);
    sqlite3_bind_int64(store_.scratch_insert_stmt, 9, (sqlite3_int64)lo.time_basis);
    sqlite3_bind_int64(store_.scratch_insert_stmt, 10, (sqlite3_int64)lo.boot_id);
    sqlite3_bind_int64(store_.scratch_insert_stmt, 11, (sqlite3_int64)lo.age_margin);

    int res = sqlite3_step(store_.scratch_insert_stmt);
    if (res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(store_.ppDb);
        LOG_WARN("StoreForwardpp step %u, %s", res, _error_mesg);
    }
    sqlite3_reset(store_.scratch_insert_stmt);
    return true;
}

bool StoreForwardPlusPlusModule::isInDB(const uint8_t *message_hash_bytes, size_t message_hash_len)
{
    if (message_hash_len < SFPP_SHORT_HASH_SIZE)
        return false;
    sqlite3_bind_int(store_.checkDupMessageHash, 1, message_hash_len);
    sqlite3_bind_blob(store_.checkDupMessageHash, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(store_.checkDupMessageHash);
    int numberFound = sqlite3_column_int(store_.checkDupMessageHash, 0);
    sqlite3_reset(store_.checkDupMessageHash);
    if (numberFound > 0)
        return true;
    return false;
}

bool StoreForwardPlusPlusModule::isInScratch(uint8_t *message_hash_bytes, size_t message_hash_len)
{
    if (message_hash_len < SFPP_SHORT_HASH_SIZE)
        return false;
    sqlite3_bind_int(store_.checkScratch, 1, message_hash_len);
    sqlite3_bind_blob(store_.checkScratch, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(store_.checkScratch);
    int numberFound = sqlite3_column_int(store_.checkScratch, 0);
    sqlite3_reset(store_.checkScratch);
    if (numberFound > 0)
        return true;
    return false;
}

void StoreForwardPlusPlusModule::removeFromScratch(const uint8_t *message_hash_bytes, size_t message_hash_len)
{
    printBytes("StoreForwardpp removing from scratch: ", message_hash_bytes, message_hash_len);
    sqlite3_bind_int(store_.removeScratch, 1, message_hash_len);
    sqlite3_bind_blob(store_.removeScratch, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(store_.removeScratch);
    sqlite3_reset(store_.removeScratch);
}

void StoreForwardPlusPlusModule::fillPayloadIfEmpty(const stored_object &lo)
{
    if (lo.payload.empty() || lo.message_hash_len < SFPP_SHORT_HASH_SIZE)
        return;
    sqlite3_reset(store_.payloadForHashStmt);
    sqlite3_bind_int(store_.payloadForHashStmt, 1, (int)lo.message_hash_len);
    sqlite3_bind_blob(store_.payloadForHashStmt, 2, lo.message_hash, lo.message_hash_len, SQLITE_STATIC);
    bool empty = false;
    if (sqlite3_step(store_.payloadForHashStmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(store_.payloadForHashStmt, 0);
        empty = text == nullptr || sqlite3_column_bytes(store_.payloadForHashStmt, 0) == 0;
    }
    sqlite3_reset(store_.payloadForHashStmt);
    if (empty)
        updatePayload((uint8_t *)lo.message_hash, lo.message_hash_len, lo.payload);
}

void StoreForwardPlusPlusModule::updatePayload(uint8_t *message_hash_bytes, size_t message_hash_len, std::string payload)
{
    LOG_WARN("StoreForwardpp updatePayload");
    sqlite3_bind_text(store_.updatePayloadStmt, 1, payload.c_str(), payload.length(), NULL);
    sqlite3_bind_int(store_.updatePayloadStmt, 2, message_hash_len);
    sqlite3_bind_blob(store_.updatePayloadStmt, 3, message_hash_bytes, message_hash_len, NULL);
    auto res = sqlite3_step(store_.updatePayloadStmt);
    if (res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(store_.ppDb);
        LOG_WARN("StoreForwardpp store_.updatePayloadStmt step error %u, %s", res, _error_mesg);
    }
    sqlite3_reset(store_.updatePayloadStmt);
}

StoreForwardPlusPlusModule::stored_object StoreForwardPlusPlusModule::getFromScratch(uint8_t *message_hash_bytes, size_t hash_len)
{
    stored_object lo;

    sqlite3_bind_int(store_.fromScratchByHashStmt, 1, hash_len);
    sqlite3_bind_blob(store_.fromScratchByHashStmt, 2, message_hash_bytes, hash_len, NULL);
    auto res = sqlite3_step(store_.fromScratchByHashStmt);
    if (res != SQLITE_ROW && res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(store_.ppDb);
        LOG_WARN("StoreForwardpp store_.fromScratchByHashStmt step error %u, %s", res, _error_mesg);
    }
    lo.to = sqlite3_column_int(store_.fromScratchByHashStmt, 0);
    lo.from = sqlite3_column_int(store_.fromScratchByHashStmt, 1);
    lo.id = sqlite3_column_int(store_.fromScratchByHashStmt, 2);

    uint8_t *encrypted_bytes = (uint8_t *)sqlite3_column_blob(store_.fromScratchByHashStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(store_.fromScratchByHashStmt, 3);
    memcpy(lo.encrypted_bytes, encrypted_bytes, lo.encrypted_len);

    uint8_t *message_hash = (uint8_t *)sqlite3_column_blob(store_.fromScratchByHashStmt, 4);
    lo.message_hash_len = sqlite3_column_bytes(store_.fromScratchByHashStmt, 4);
    memcpy(lo.message_hash, message_hash, SFPP_HASH_SIZE);

    lo.rx_time = sqlite3_column_int(store_.fromScratchByHashStmt, 5);

    uint8_t *root_hash = (uint8_t *)sqlite3_column_blob(store_.fromScratchByHashStmt, 6);
    lo.root_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.root_hash, root_hash, SFPP_HASH_SIZE);
    lo.payload = std::string((char *)sqlite3_column_text(store_.fromScratchByHashStmt, 7),
                             sqlite3_column_bytes(store_.fromScratchByHashStmt, 7));
    lo.time_basis = (uint8_t)sqlite3_column_int(store_.fromScratchByHashStmt, 8);
    lo.boot_id = (uint32_t)sqlite3_column_int64(store_.fromScratchByHashStmt, 9);
    lo.age_margin = (uint32_t)sqlite3_column_int64(store_.fromScratchByHashStmt, 10);
    sqlite3_reset(store_.fromScratchByHashStmt);
    return lo;
}

bool StoreForwardPlusPlusModule::lookUpFullRootHash(uint8_t *partial_root_hash, size_t partial_root_hash_len,
                                                    uint8_t *full_root_hash)
{
    printBytes("StoreForwardpp partial_root_hash", partial_root_hash, partial_root_hash_len);
    sqlite3_bind_int(store_.getFullRootHashStmt, 1, partial_root_hash_len);
    sqlite3_bind_blob(store_.getFullRootHashStmt, 2, partial_root_hash, partial_root_hash_len, NULL);
    sqlite3_step(store_.getFullRootHashStmt);
    uint8_t *tmp_root_hash = (uint8_t *)sqlite3_column_blob(store_.getFullRootHashStmt, 0);
    if (tmp_root_hash) {
        LOG_DEBUG("StoreForwardpp Found full root hash!");
        memcpy(full_root_hash, tmp_root_hash, SFPP_HASH_SIZE);
        sqlite3_reset(store_.getFullRootHashStmt);
        return true;
    }
    sqlite3_reset(store_.getFullRootHashStmt);
    return false;
}

uint32_t StoreForwardPlusPlusModule::getScratchCount(uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_bind_int(store_.getScratchCountStmt, 1, root_hash_len);
    sqlite3_bind_blob(store_.getScratchCountStmt, 2, root_hash, root_hash_len, NULL);

    int res = sqlite3_step(store_.getScratchCountStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE && res != SQLITE_ROW) {
        LOG_ERROR("StoreForwardpp store_.getScratchCountStmt sqlite error %u, %s", res, sqlite3_errmsg(store_.ppDb));
    }
    uint32_t count = sqlite3_column_int(store_.getScratchCountStmt, 0);
    sqlite3_reset(store_.getScratchCountStmt);
    return count;
}

void StoreForwardPlusPlusModule::pruneScratchQueue()
{
    // Very little utility in holding on to very old scratch messages.
    constexpr uint32_t scratchMaxAgeS = 60 * 60 * 6;
    std::vector<sqlite3_int64> drop;
    sqlite3_reset(store_.scratchTimeRowsStmt);
    while (sqlite3_step(store_.scratchTimeRowsStmt) == SQLITE_ROW) {
        RowTime t;
        const sqlite3_int64 rowid = sqlite3_column_int64(store_.scratchTimeRowsStmt, 0);
        t.stamp = (uint32_t)sqlite3_column_int64(store_.scratchTimeRowsStmt, 1);
        t.basis = (uint8_t)sqlite3_column_int(store_.scratchTimeRowsStmt, 2);
        t.boot_id = (uint32_t)sqlite3_column_int64(store_.scratchTimeRowsStmt, 3);
        t.margin = (uint32_t)sqlite3_column_int64(store_.scratchTimeRowsStmt, 4);
        if (rowAge(t).age >= scratchMaxAgeS)
            drop.push_back(rowid);
    }
    sqlite3_reset(store_.scratchTimeRowsStmt);

    for (sqlite3_int64 rowid : drop) {
        sqlite3_reset(store_.deleteScratchRowidStmt);
        sqlite3_bind_int64(store_.deleteScratchRowidStmt, 1, rowid);
        sqlite3_step(store_.deleteScratchRowidStmt);
        sqlite3_reset(store_.deleteScratchRowidStmt);
    }
    scratch_timed_out += (uint32_t)drop.size();
}

bool StoreForwardPlusPlusModule::findIdentityCopy(uint32_t to, uint32_t from, uint32_t id, const uint8_t *root_hash,
                                                  IdentityHit &hit)
{
    auto lookup = [&](sqlite3_stmt *stmt, bool canon) {
        sqlite3_reset(stmt);
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)from);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)id);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)to);
        sqlite3_bind_int(stmt, 4, SFPP_HASH_SIZE);
        sqlite3_bind_blob(stmt, 5, root_hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const uint8_t *hash = (const uint8_t *)sqlite3_column_blob(stmt, 0);
            if (hash && sqlite3_column_bytes(stmt, 0) == (int)SFPP_HASH_SIZE) {
                memcpy(hit.hash, hash, SFPP_HASH_SIZE);
                hit.canon = canon;
                found = true;
            }
        }
        sqlite3_reset(stmt);
        return found;
    };
    return lookup(store_.identityCanonStmt, true) || lookup(store_.identityScratchStmt, false);
}

bool StoreForwardPlusPlusModule::replaceIdentityCopy(const IdentityHit &hit, const uint8_t *hash, const uint8_t *cipher,
                                                     size_t cipherLen)
{
    if (hit.canon) {
        sqlite3_reset(store_.replaceCanonCopyStmt);
        sqlite3_bind_blob(store_.replaceCanonCopyStmt, 1, cipher, (int)cipherLen, SQLITE_STATIC);
        sqlite3_bind_blob(store_.replaceCanonCopyStmt, 2, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_int64(store_.replaceCanonCopyStmt, 3, (sqlite3_int64)sfppsr::shortId(hash, SFPP_HASH_SIZE));
        sqlite3_bind_int64(store_.replaceCanonCopyStmt, 4, (sqlite3_int64)sfppsr::checksumContribution(hash, SFPP_HASH_SIZE));
        sqlite3_bind_int(store_.replaceCanonCopyStmt, 5, (int)sfppsr::contentBucket(hash, SFPP_HASH_SIZE));
        sqlite3_bind_blob(store_.replaceCanonCopyStmt, 6, hit.hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        const int res = sqlite3_step(store_.replaceCanonCopyStmt);
        sqlite3_reset(store_.replaceCanonCopyStmt);
        return res == SQLITE_OK || res == SQLITE_DONE;
    }

    sqlite3_reset(store_.replaceScratchCopyStmt);
    sqlite3_bind_blob(store_.replaceScratchCopyStmt, 1, cipher, (int)cipherLen, SQLITE_STATIC);
    sqlite3_bind_blob(store_.replaceScratchCopyStmt, 2, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_blob(store_.replaceScratchCopyStmt, 3, hit.hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    const int res = sqlite3_step(store_.replaceScratchCopyStmt);
    sqlite3_reset(store_.replaceScratchCopyStmt);
    return res == SQLITE_OK || res == SQLITE_DONE;
}

bool StoreForwardPlusPlusModule::insertCanonObject(uint32_t to, uint32_t from, uint32_t packetId, const uint8_t *cipher,
                                                   size_t cipherLen, const uint8_t *hash, const RowTime &time,
                                                   const std::string &payload, const uint8_t *root_hash, uint32_t bucket,
                                                   bool senderCopy)
{
    IdentityHit hit;
    if (findIdentityCopy(to, from, packetId, root_hash, hit) && hit.canon) {
        if (memcmp(hit.hash, hash, SFPP_HASH_SIZE) == 0)
            return true;
        if (!senderCopy)
            return true; // first copy wins unless the sender's copy arrives
        if (isInDB(hash, SFPP_HASH_SIZE)) {
            LOG_WARN("StoreForwardpp identity copy would collide with another object");
            return true;
        }
        return replaceIdentityCopy(hit, hash, cipher, cipherLen);
    }
    if (isInDB(hash, SFPP_HASH_SIZE))
        return true;

    sqlite3_reset(store_.canon_insert_stmt);
    sqlite3_bind_int64(store_.canon_insert_stmt, 1, (sqlite3_int64)to);
    sqlite3_bind_int64(store_.canon_insert_stmt, 2, (sqlite3_int64)from);
    sqlite3_bind_int64(store_.canon_insert_stmt, 3, (sqlite3_int64)packetId);
    sqlite3_bind_blob(store_.canon_insert_stmt, 4, root_hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_blob(store_.canon_insert_stmt, 5, cipher, (int)cipherLen, SQLITE_STATIC);
    sqlite3_bind_blob(store_.canon_insert_stmt, 6, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_int64(store_.canon_insert_stmt, 7, (sqlite3_int64)time.stamp);
    sqlite3_bind_text(store_.canon_insert_stmt, 8, payload.c_str(), (int)payload.size(), SQLITE_STATIC);
    sqlite3_bind_int64(store_.canon_insert_stmt, 9, (sqlite3_int64)sfppsr::shortId(hash, SFPP_HASH_SIZE));
    sqlite3_bind_int64(store_.canon_insert_stmt, 10, (sqlite3_int64)sfppsr::checksumContribution(hash, SFPP_HASH_SIZE));
    sqlite3_bind_int64(store_.canon_insert_stmt, 11, (sqlite3_int64)time.basis);
    sqlite3_bind_int64(store_.canon_insert_stmt, 12, (sqlite3_int64)time.boot_id);
    sqlite3_bind_int64(store_.canon_insert_stmt, 13, (sqlite3_int64)time.margin);
    sqlite3_bind_int(store_.canon_insert_stmt, 14, (int)sfppsr::contentBucket(hash, SFPP_HASH_SIZE));

    const int res = sqlite3_step(store_.canon_insert_stmt);
    sqlite3_reset(store_.canon_insert_stmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("StoreForwardpp cannot store a canonical object: %s", sqlite3_errmsg(store_.ppDb));
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

    RowTime time;
    if (!timeFromAge(replay.heardAgoS, time))
        return false;
    if (!insertCanonObject(replay.to, replay.from, replay.id, replay.ciphertext.data(), replay.ciphertext.size(), hash, time, "",
                           root_hash, bucket))
        return false;
    // The object is canonical now, so a scratch copy (a writer-missed offer) is no longer needed.
    removeFromScratch(hash, SFPP_HASH_SIZE);
    return true;
}

#endif // has include sqlite3
