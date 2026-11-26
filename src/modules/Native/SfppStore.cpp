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

// D7's retention applies to canon rows as well as to the legacy read window.
static constexpr uint32_t kRetentionS = 48 * 60 * 60;

void StoreForwardPlusPlusModule::initTimeState()
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
    lastSeenUptime = Time::getUptimeSecs();
    lastSeenEpoch = bootEpoch;
    writeTimeState();
}

void StoreForwardPlusPlusModule::writeTimeState()
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
    const uint32_t nowUptime = Time::getUptimeSecs();
    const uint32_t nowEpoch = getValidTime(RTCQualityNTP, true);
    if (nowEpoch) {
        lastSeenEpoch = nowEpoch;
        // A clock that arrived after boot still pins the boot start for ageing epoch rows.
        if (bootEpoch == 0)
            bootEpoch = nowEpoch > nowUptime ? nowEpoch - nowUptime : 0;
    }
    lastSeenUptime = nowUptime;
    writeTimeState();
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
    const uint32_t nowUptime = Time::getUptimeSecs();
    if (bootEpoch > 0)
        return bootEpoch + nowUptime;
    if (lastSeenEpoch > 0)
        return lastSeenEpoch + (nowUptime - lastSeenUptime);
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
        t.stamp = Time::getUptimeSecs();
        t.boot_id = bootId;
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
    const uint32_t nowUptime = Time::getUptimeSecs();

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
        if (t.boot_id == bootId) {
            if (t.stamp > nowUptime + a.margin)
                a.margin += t.stamp - nowUptime;
            else
                a.age = t.stamp < nowUptime ? nowUptime - t.stamp : 0;
        } else {
            // A row from a boot that was not restamped: age it to the previous end, add the gap we
            // know about and the uptime since this boot started.
            const uint32_t atEnd = (t.boot_id == prevBootId && prevLastUptime > t.stamp) ? prevLastUptime - t.stamp : 0;
            const uint32_t down =
                (bootEpoch > 0 && prevLastEpoch > 0 && bootEpoch >= prevLastEpoch) ? bootEpoch - prevLastEpoch : 0;
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
void StoreForwardPlusPlusModule::restampPreviousBoot()
{
    const uint32_t nowUptime = Time::getUptimeSecs();
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

            next.margin = old.margin + (downKnown ? 0 : kRetentionS);
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

    heal(restampChannelStmt, updateChannelTimeStmt);
    heal(restampScratchStmt, updateScratchTimeStmt);
    heal(restampDirectStmt, updateDirectTimeStmt);
}

void StoreForwardPlusPlusModule::trimCanonByRetention(uint8_t *root_hash, size_t root_hash_len)
{
    std::vector<sqlite3_int64> drop;
    sqlite3_reset(canonTimeRowsStmt);
    sqlite3_bind_int(canonTimeRowsStmt, 1, root_hash_len);
    sqlite3_bind_blob(canonTimeRowsStmt, 2, root_hash, root_hash_len, SQLITE_STATIC);
    while (sqlite3_step(canonTimeRowsStmt) == SQLITE_ROW) {
        RowTime t;
        const sqlite3_int64 rowid = sqlite3_column_int64(canonTimeRowsStmt, 0);
        t.stamp = (uint32_t)sqlite3_column_int64(canonTimeRowsStmt, 1);
        t.basis = (uint8_t)sqlite3_column_int64(canonTimeRowsStmt, 2);
        t.boot_id = (uint32_t)sqlite3_column_int64(canonTimeRowsStmt, 3);
        t.margin = (uint32_t)sqlite3_column_int64(canonTimeRowsStmt, 4);
        if (rowAge(t).age >= kRetentionS)
            drop.push_back(rowid);
    }
    sqlite3_reset(canonTimeRowsStmt);

    for (sqlite3_int64 rowid : drop) {
        sqlite3_reset(deleteCanonRowidStmt);
        sqlite3_bind_int64(deleteCanonRowidStmt, 1, rowid);
        sqlite3_step(deleteCanonRowidStmt);
        sqlite3_reset(deleteCanonRowidStmt);
    }
}

bool StoreForwardPlusPlusModule::linkIsFresh(const link_object &lo) const
{
    RowTime t;
    t.stamp = lo.rx_time;
    t.basis = kTimeBasisEpoch; // the wire's rxtime is an epoch claim; an unusable one counts as stale
    const RowAge age = rowAge(t);
    return age.age + age.margin <= rebroadcastTimeout;
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

    sqlite3_reset(bucketSummaryStmt);
    sqlite3_bind_int(bucketSummaryStmt, 1, root_hash_len);
    sqlite3_bind_blob(bucketSummaryStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(bucketSummaryStmt, 3, (int)bucket);

    while (sqlite3_step(bucketSummaryStmt) == SQLITE_ROW) {
        out.add((pinsketch::Element)sqlite3_column_int64(bucketSummaryStmt, 0),
                (uint64_t)sqlite3_column_int64(bucketSummaryStmt, 1));
    }
    sqlite3_reset(bucketSummaryStmt);

    return out.count() > 0;
}

bool StoreForwardPlusPlusModule::collectBucketIds(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket,
                                                  std::vector<pinsketch::Element> &out)
{
    out.clear();
    sqlite3_reset(bucketIdsStmt);
    sqlite3_bind_int(bucketIdsStmt, 1, root_hash_len);
    sqlite3_bind_blob(bucketIdsStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(bucketIdsStmt, 3, (int)bucket);

    while (sqlite3_step(bucketIdsStmt) == SQLITE_ROW)
        out.push_back((pinsketch::Element)sqlite3_column_int64(bucketIdsStmt, 0));
    sqlite3_reset(bucketIdsStmt);

    return !out.empty();
}

bool StoreForwardPlusPlusModule::collectBuckets(const uint8_t *root_hash, size_t root_hash_len, std::vector<uint32_t> &out)
{
    out.clear();
    sqlite3_reset(distinctBucketsStmt);
    sqlite3_bind_int(distinctBucketsStmt, 1, root_hash_len);
    sqlite3_bind_blob(distinctBucketsStmt, 2, root_hash, root_hash_len, NULL);
    while (sqlite3_step(distinctBucketsStmt) == SQLITE_ROW)
        out.push_back((uint32_t)sqlite3_column_int64(distinctBucketsStmt, 0));
    sqlite3_reset(distinctBucketsStmt);
    return !out.empty();
}

//
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
    sqlite3_bind_int64(scratch_insert_stmt, 9, (sqlite3_int64)lo.time_basis);
    sqlite3_bind_int64(scratch_insert_stmt, 10, (sqlite3_int64)lo.boot_id);
    sqlite3_bind_int64(scratch_insert_stmt, 11, (sqlite3_int64)lo.age_margin);

    int res = sqlite3_step(scratch_insert_stmt);
    if (res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(ppDb);
        LOG_WARN("StoreForwardpp step %u, %s", res, _error_mesg);
    }
    sqlite3_reset(scratch_insert_stmt);
    return true;
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
    lo.time_basis = (uint8_t)sqlite3_column_int(fromScratchByHashStmt, 8);
    lo.boot_id = (uint32_t)sqlite3_column_int64(fromScratchByHashStmt, 9);
    lo.age_margin = (uint32_t)sqlite3_column_int64(fromScratchByHashStmt, 10);
    sqlite3_reset(fromScratchByHashStmt);
    return lo;
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

void StoreForwardPlusPlusModule::pruneScratchQueue()
{
    // Very little utility in holding on to very old scratch messages.
    constexpr uint32_t scratchMaxAgeS = 60 * 60 * 6;
    std::vector<sqlite3_int64> drop;
    sqlite3_reset(scratchTimeRowsStmt);
    while (sqlite3_step(scratchTimeRowsStmt) == SQLITE_ROW) {
        RowTime t;
        const sqlite3_int64 rowid = sqlite3_column_int64(scratchTimeRowsStmt, 0);
        t.stamp = (uint32_t)sqlite3_column_int64(scratchTimeRowsStmt, 1);
        t.basis = (uint8_t)sqlite3_column_int(scratchTimeRowsStmt, 2);
        t.boot_id = (uint32_t)sqlite3_column_int64(scratchTimeRowsStmt, 3);
        t.margin = (uint32_t)sqlite3_column_int64(scratchTimeRowsStmt, 4);
        if (rowAge(t).age >= scratchMaxAgeS)
            drop.push_back(rowid);
    }
    sqlite3_reset(scratchTimeRowsStmt);

    for (sqlite3_int64 rowid : drop) {
        sqlite3_reset(deleteScratchRowidStmt);
        sqlite3_bind_int64(deleteScratchRowidStmt, 1, rowid);
        sqlite3_step(deleteScratchRowidStmt);
        sqlite3_reset(deleteScratchRowidStmt);
    }
    scratch_timed_out += (uint32_t)drop.size();
}

bool StoreForwardPlusPlusModule::insertCanonObject(uint32_t to, uint32_t from, uint32_t packetId, const uint8_t *cipher,
                                                   size_t cipherLen, const uint8_t *hash, const RowTime &time,
                                                   const std::string &payload, const uint8_t *root_hash, uint32_t bucket)
{
    if (isInDB(hash, SFPP_HASH_SIZE))
        return true;

    sqlite3_reset(chain_insert_stmt);
    sqlite3_bind_int64(chain_insert_stmt, 1, (sqlite3_int64)to);
    sqlite3_bind_int64(chain_insert_stmt, 2, (sqlite3_int64)from);
    sqlite3_bind_int64(chain_insert_stmt, 3, (sqlite3_int64)packetId);
    sqlite3_bind_blob(chain_insert_stmt, 4, root_hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_blob(chain_insert_stmt, 5, cipher, (int)cipherLen, SQLITE_STATIC);
    sqlite3_bind_blob(chain_insert_stmt, 6, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_int64(chain_insert_stmt, 7, (sqlite3_int64)time.stamp);
    sqlite3_bind_text(chain_insert_stmt, 8, payload.c_str(), (int)payload.size(), SQLITE_STATIC);
    sqlite3_bind_int64(chain_insert_stmt, 9, (sqlite3_int64)sfppsr::shortId(hash, SFPP_HASH_SIZE));
    sqlite3_bind_int64(chain_insert_stmt, 10, (sqlite3_int64)sfppsr::checksumContribution(hash, SFPP_HASH_SIZE));
    sqlite3_bind_int64(chain_insert_stmt, 11, (sqlite3_int64)time.basis);
    sqlite3_bind_int64(chain_insert_stmt, 12, (sqlite3_int64)time.boot_id);
    sqlite3_bind_int64(chain_insert_stmt, 13, (sqlite3_int64)time.margin);
    sqlite3_bind_int(chain_insert_stmt, 14, (int)sfppsr::contentBucket(hash, SFPP_HASH_SIZE));

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
