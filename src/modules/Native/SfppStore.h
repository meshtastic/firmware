#pragma once

#if __has_include("sqlite3.h")

#include "Channels.h"
#include "SfppFrame.h"
#include "SketchIndex.h"
#include "sqlite3.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define SFPP_HASH_SIZE 16
#define SFPP_SHORT_HASH_SIZE 8

class StoreForwardPlusPlusModule;

// Owns the archive database: its handle, prepared statements and row operations.
class SfppStore
{
  public:
    struct stored_object {
        uint32_t to;
        uint32_t from;
        uint32_t id;
        uint32_t rx_time = 0;
        uint8_t time_basis = 0;  // 0 epoch, 1 uptime seconds, 2 age at the restart that stamped it
        uint32_t boot_id = 0;    // the boot a basis-1 stamp belongs to
        uint32_t age_margin = 0; // extra uncertainty in seconds
        ChannelHash channel_hash;
        uint8_t encrypted_bytes[256] = {0};
        size_t encrypted_len;
        uint8_t message_hash[SFPP_HASH_SIZE] = {0};
        size_t message_hash_len = 0;
        uint8_t root_hash[SFPP_HASH_SIZE] = {0};
        size_t root_hash_len = 0;
        std::string payload;
        bool validObject = true; // set this false when an object calculation fails, etc.
    };

    // Time model. A row carries its stamp, its basis and (for a boot stamp) the boot that wrote it.
    // Everything that ages a row goes through rowAge() so the bases are never compared raw.
    struct RowTime {
        uint32_t stamp = 0;
        uint8_t basis = 0;
        uint32_t boot_id = 0;
        uint32_t margin = 0;
    };
    struct RowAge {
        uint32_t age = 0;
        uint32_t margin = 0;
        bool has_epoch = false;
        uint32_t epoch = 0;
        uint32_t estimate_epoch = 0;
    };

    // One object per (root, from, id, to); a sender's copy may replace a relay's.
    struct IdentityHit {
        uint8_t hash[SFPP_HASH_SIZE];
        bool canon;
    };

    // Stamped into PRAGMA user_version. A store with any other value is moved aside, never migrated.
    static constexpr int kSchemaVersion = 5;

    SfppStore() = default;
    ~SfppStore();

    void openStore(const uint8_t *derivedRoot, bool haveDerived);

  private:
    friend class StoreForwardPlusPlusModule;

    enum chain_types {
        channel_chain = 0,
    };

    void initTimeState();
    void writeTimeState();
    void restampPreviousBoot();
    void backfillSetIndex();

    sqlite3 *ppDb = nullptr;
    sqlite3_stmt *canon_insert_stmt = nullptr;
    sqlite3_stmt *scratch_insert_stmt = nullptr;
    sqlite3_stmt *identityCanonStmt = nullptr;
    sqlite3_stmt *identityScratchStmt = nullptr;
    sqlite3_stmt *replaceCanonCopyStmt = nullptr;
    sqlite3_stmt *replaceScratchCopyStmt = nullptr;
    sqlite3_stmt *checkDupMessageHash = nullptr;
    sqlite3_stmt *checkScratch = nullptr;
    sqlite3_stmt *removeScratch = nullptr;
    sqlite3_stmt *updatePayloadStmt = nullptr;
    sqlite3_stmt *payloadForHashStmt = nullptr;
    sqlite3_stmt *getPayloadFromScratchStmt = nullptr;
    sqlite3_stmt *fromScratchStmt = nullptr;
    sqlite3_stmt *fromScratchByHashStmt = nullptr;
    sqlite3_stmt *getHashFromRootStmt = nullptr;
    sqlite3_stmt *addRootToMappingsStmt = nullptr;
    sqlite3_stmt *getFullRootHashStmt = nullptr;
    sqlite3_stmt *getScratchCountStmt = nullptr;
    sqlite3_stmt *bucketSummaryStmt = nullptr;
    sqlite3_stmt *bucketIdsStmt = nullptr;
    sqlite3_stmt *distinctBucketsStmt = nullptr;
    sqlite3_stmt *getObjectByShortIdStmt = nullptr;
    sqlite3_stmt *deleteCanonStmt = nullptr;
    sqlite3_stmt *unindexedObjectsStmt = nullptr;
    sqlite3_stmt *setObjectIndexStmt = nullptr;
    sqlite3_stmt *legacyHistoryStmt = nullptr;
    sqlite3_stmt *timeStateReadStmt = nullptr;
    sqlite3_stmt *timeStateWriteStmt = nullptr;
    sqlite3_stmt *restampChannelStmt = nullptr;
    sqlite3_stmt *restampScratchStmt = nullptr;
    sqlite3_stmt *restampDirectStmt = nullptr;
    sqlite3_stmt *updateChannelTimeStmt = nullptr;
    sqlite3_stmt *updateScratchTimeStmt = nullptr;
    sqlite3_stmt *updateDirectTimeStmt = nullptr;
    sqlite3_stmt *canonTimeRowsStmt = nullptr;
    sqlite3_stmt *scratchTimeRowsStmt = nullptr;
    sqlite3_stmt *deleteCanonRowidStmt = nullptr;
    sqlite3_stmt *deleteScratchRowidStmt = nullptr;

    uint32_t bootId = 0;
    uint32_t bootEpoch = 0;
    uint32_t prevBootId = 0;
    uint32_t prevLastUptime = 0;
    uint32_t prevLastEpoch = 0;
    uint32_t lastSeenUptime = 0;
    uint32_t lastSeenEpoch = 0;
};

#endif
