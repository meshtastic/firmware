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

// After a short-ID-collision probe, silence this long means the peer has served all it has.
static constexpr uint32_t kEnumProbeIdleMs = 30 * 1000;

// Service window: a hole older than 24 h is accepted as lost instead of being retried.
static constexpr uint32_t kServiceWindowMs = 24 * 60 * 60 * 1000;
// Failed pulls back off, and the backoff and attempt count cap a busy advert stream's retry cost.
static constexpr uint32_t kFailedPullBackoffMs = 30 * 1000;
static constexpr uint32_t kFailedPullBackoffMaxMs = 5 * 60 * 1000;
static constexpr uint8_t kFailedPullMaxAttempts = 8;

// The item retry must outlast a full round-robin delivery at the current pacing.
static uint32_t itemRetryIntervalMs()
{
    const uint32_t scaled = 4 * enumDrainIntervalMs(); // 2 requesters x 2 intervals
    return scaled > kEnumRetryMs ? scaled : kEnumRetryMs;
}

// A slow pacer spaces listing frames wider than the floor, so wait out four drain intervals.
static uint32_t enumProbeIdleMs()
{
    const uint32_t scaled = 4 * enumDrainIntervalMs();
    return scaled > kEnumProbeIdleMs ? scaled : kEnumProbeIdleMs;
}

// Overheard listings kept, and how long one stands in for our own ENUM_REQUEST.
static constexpr size_t kEnumHeardListingsMax = 4;
static constexpr uint32_t kEnumHeardListingMs = 5 * 60 * 1000;

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
    enumPeerSummaryKnown = true;
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
    if ((enumCollisionProbe || partialProvide) && Throttle::hasElapsed(enumLastItemMs, enumProbeIdleMs())) {
        // A lost provide frame is not a collision; only a real probe may suppress the bucket, or a
        // later advert with the same summary could never restart the exchange.
        if (enumCollisionProbe && !bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
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

    // A named exchange keeps its count and checksum for the retry.
    const uint32_t count = enumPeerCount;
    const uint64_t checksum = enumPeerChecksum;
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
    if (enumExchangeActive || enumPending.empty())
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root) == 0)
        return;
    const uint32_t nowMs = Time::getMillis();

    // A hole for another root, or older than the proposed service window, is no longer chased.
    enumPending.erase(std::remove_if(enumPending.begin(), enumPending.end(),
                                     [&](const EnumPendingBucket &entry) {
                                         return memcmp(entry.root, root, SFPP_HASH_SIZE) != 0 ||
                                                (entry.firstFailedMs != 0 &&
                                                 Throttle::hasElapsed(entry.firstFailedMs, kServiceWindowMs));
                                     }),
                      enumPending.end());
    if (enumPending.empty())
        return;

    // One pull per pacer slot, shared with the item drain on this node.
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
    if (enumBucketSuppressed(root, entry.bucket, entry.count, entry.checksum)) {
        enumPending.erase(enumPending.begin() + best);
        return;
    }
    // Keep the entry: endEnumExchange() removes it on success, rememberFailedPull() advances its
    // attempt and first-failure state on failure.

    char rootHex[9];
    formatRootHex(root, SFPP_HASH_SIZE, rootHex);
    LOG_INFO("SFPP-ENUM sketch_decode result=skipped reason=pending_pull root=%s bucket=%u peer=0x%08x", rootHex, entry.bucket,
             entry.authority);
    enumLastDrainMs = nowMs; // consume the pacer slot
    NodeNum lister = 0;
    std::vector<pinsketch::Element> listed;
    if (takeHeardListing(root, entry.bucket, lister, listed)) {
        // An overheard listing already answered this bucket: ask the lister for items only.
        beginEnumExchange(root, entry.bucket, lister, entry.count, entry.checksum);
        enumPeerSummaryKnown = entry.summaryKnown && lister == entry.authority;
        enumPeerTip = entry.tip;
        enumProvideIds = listed;
        actOnListing(root, entry.bucket, lister);
        return;
    }
    beginEnumExchange(root, entry.bucket, entry.authority, entry.count, entry.checksum);
    enumPeerTip = entry.tip;
    requestEnumList(root, entry.bucket, entry.authority);
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
    if (!enumPeerSummaryKnown) {
        // An overheard listing has no checksum: matching means holding every listed ID.
        std::vector<pinsketch::Element> own;
        collectBucketIds(root_hash, SFPP_HASH_SIZE, bucket, own);
        std::sort(own.begin(), own.end());
        std::vector<pinsketch::Element> listed = enumProvideIds;
        std::sort(listed.begin(), listed.end());
        return std::includes(own.begin(), own.end(), listed.begin(), listed.end());
    }
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
    std::vector<uint32_t> buckets;
    if (!collectBuckets(root_hash, root_hash_len, buckets))
        return;

    // Newest-changed first: a bucket whose summary differs from the one last advertised (or that
    // was never advertised) goes ahead of the round-robin.
    int64_t chosenIndex = -1;
    for (int64_t i = (int64_t)buckets.size() - 1; i >= 0; i--) {
        sfppsr::BucketSummary summary(4);
        if (!buildBucketSummary(root_hash, root_hash_len, buckets[(size_t)i], summary) || summary.count() == 0)
            continue;
        bool changed = true;
        for (const auto &seen : enumAdvertSeen) {
            if (seen.bucket == buckets[(size_t)i]) {
                changed = (seen.count != summary.count() || seen.checksum != summary.checksum());
                break;
            }
        }
        if (changed) {
            chosenIndex = i;
            break;
        }
    }

    uint32_t bucket = 0;
    if (chosenIndex >= 0) {
        bucket = buckets[(size_t)chosenIndex];
        enumBucketCursor = (uint32_t)(chosenIndex + 1) % (uint32_t)buckets.size();
    } else {
        // No change: one bucket per tick, round-robin.
        if (enumBucketCursor >= buckets.size())
            enumBucketCursor = 0;
        bucket = buckets[enumBucketCursor];
        enumBucketCursor = (enumBucketCursor + 1) % (uint32_t)buckets.size();
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
    // The 4-byte tip is the advertised bucket's content count, not a chain counter.
    msg.encapsulated_rxtime = summary.count();
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

    logEnum("rx", "advert", advert.scope, mp.decoded.payload.size, mp.from, -1, 0, 1, t->message.size);
    if (portduino_config.sfpp_enum_listen_only)
        return; // listen-only: never request

    // A failed pull is retried on the next advert from that peer, whichever bucket it names.
    for (const auto &entry : enumPending) {
        if (entry.authority == mp.from && entry.retryAfterMs != 0 && memcmp(entry.root, root, SFPP_HASH_SIZE) == 0 &&
            Throttle::deadlinePassedAt(Time::getMillis(), entry.retryAfterMs)) {
            setIntervalFromNow(10);
            break;
        }
    }

    const uint32_t advertTip = t->encapsulated_rxtime; // the sender's content-bucket count

    // The receiver of a mismatching advert always pulls. An empty local bucket is count 0 and
    // checksum 0, so it mismatches a non-empty peer and pulls too.
    sfppsr::BucketSummary local(4);
    buildBucketSummary(root, SFPP_HASH_SIZE, advert.scope.bucket, local);
    const bool namedMismatch = !(local.count() == advert.count && local.checksum() == advert.checksum);
    if (!namedMismatch)
        return;

    if (enumExchangeActive) {
        // A behind bucket seen while busy is remembered (bounded, newest first) and
        // pulled when this exchange ends.
        if (advert.scope.bucket != enumExchangeBucket)
            addPendingBucket(root, mp.from, advert.scope.bucket, advert.count, advert.checksum, advertTip);
        return;
    }

    {
        // A bucket that could not be repaired from this advertised summary is not pulled again until
        // a peer advertises a different one for it.
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

    // A peer ID we already hold in scratch needs no fetch; promote it into the bucket.
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
    // Broadcast so every archive hears the listing it asks for; only the named peer answers.
    msg.encapsulated_to = peer;
    sendEnumMessage(msg, NODENUM_BROADCAST, "enum_req");
}

void StoreForwardPlusPlusModule::handleEnumRequest(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    const NodeNum self = nodeDB->getNodeNum();
    const bool named = mp.to == self || (mp.to == NODENUM_BROADCAST && t->encapsulated_to == self);
    if (portduino_config.sfpp_enum_listen_only || !named)
        return;

    sfppsr::Scope scope;
    if (!sfppsr::parseScope(*t, scope))
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (!lookUpFullRootHash(scope.rootPrefix, scope.rootPrefixLen, root))
        return;

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
        // Broadcast, so every archive behind on this bucket can use the listing; paced per requester.
        queueEnumFrames(frames, mp.from, NODENUM_BROADCAST, scope, "enum_prov");
        setIntervalFromNow(10);
    }
}

void StoreForwardPlusPlusModule::handleEnumProvide(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    sfppsr::ShortIdList list;
    if (!sfppsr::parseShortIds(*t, list))
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (!lookUpFullRootHash(list.scope.rootPrefix, list.scope.rootPrefixLen, root))
        return;
    if (!enumExchangeActive || mp.from != enumExchangePeer || list.scope.bucket != enumExchangeBucket ||
        memcmp(root, enumExchangeRoot, SFPP_HASH_SIZE) != 0) {
        handleOverheardListing(mp.from, root, list);
        return;
    }

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

    enumProvideActive = false;
    actOnListing(root, enumProvideBucket, enumProvidePeer);
}

// A complete listing (enumProvideIds) for the open exchange: fetch what the peer holds and we do not.
void StoreForwardPlusPlusModule::actOnListing(const uint8_t *root, uint32_t bucket, NodeNum peer)
{
    std::vector<pinsketch::Element> own;
    collectBucketIds(root, SFPP_HASH_SIZE, bucket, own);
    std::sort(enumProvideIds.begin(), enumProvideIds.end());
    enumProvideIds.erase(std::unique(enumProvideIds.begin(), enumProvideIds.end()), enumProvideIds.end());
    std::sort(own.begin(), own.end());
    own.erase(std::unique(own.begin(), own.end()), own.end());
    std::vector<pinsketch::Element> missing;
    std::set_difference(enumProvideIds.begin(), enumProvideIds.end(), own.begin(), own.end(), std::back_inserter(missing));
    // A peer ID we already hold in scratch needs no fetch; promote it into the bucket.
    promoteScratchMatches(root, bucket, missing);

    enumAwaitingList = false;
    if (!missing.empty()) {
        enumCollisionProbe = false;
        requestMissingItems(root, bucket, peer, missing);
        if (enumWanted.empty() && enumExchangeActive) {
            // Every missing ID is already in flight elsewhere; this exchange has nothing to wait for.
            enumExchangeSucceeded = bucketMatchesPeer(root, bucket);
            if (!enumExchangeSucceeded)
                rememberFailedPull();
            endEnumExchange();
        }
        return;
    }

    // A promotion that the peer's checksum does not confirm is a collision; undo it and
    // fetch the peer's objects instead.
    if (!enumPromoted.empty() && !bucketMatchesPeer(root, bucket)) {
        repairPromotions();
        return;
    }

    if (bucketMatchesPeer(root, bucket)) {
        enumExchangeSucceeded = true;
        endEnumExchange();
        return;
    }

    // The lists match by short ID but the checksums differ: a truncated-ID collision may hide an
    // object. Ask for every object the shared IDs name; the probe ends on convergence or idle.
    enumCollisionProbe = true;
    enumLastItemMs = Time::getMillis();
    requestMissingItems(root, bucket, peer, enumProvideIds);
}

void StoreForwardPlusPlusModule::handleOverheardListing(NodeNum peer, const uint8_t *root_hash, const sfppsr::ShortIdList &list)
{
    if (peer == 0 || peer == nodeDB->getNodeNum())
        return;
    uint8_t derived[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), derived) == 0 || memcmp(derived, root_hash, SFPP_HASH_SIZE) != 0)
        return;
    if (list.frameTotal == 0 || list.frameTotal > sizeof(EnumHeardListing::seen) || list.frameIndex >= list.frameTotal)
        return;

    const uint32_t nowMs = Time::getMillis();
    EnumHeardListing *heard = nullptr;
    for (auto &entry : enumHeardListings) {
        if (entry.bucket == list.scope.bucket && memcmp(entry.root, root_hash, SFPP_HASH_SIZE) == 0) {
            heard = &entry;
            break;
        }
    }
    if (heard && (heard->peer != peer || heard->total != list.frameTotal || heard->seenCount == heard->total ||
                  Throttle::hasElapsed(heard->heardMs, kEnumHeardListingMs))) {
        // A different lister, framing or a newer copy restarts the record.
        heard->peer = peer;
        heard->total = list.frameTotal;
        heard->seenCount = 0;
        memset(heard->seen, 0, sizeof(heard->seen));
        heard->ids.clear();
    }
    if (!heard) {
        if (enumHeardListings.size() >= kEnumHeardListingsMax)
            enumHeardListings.erase(enumHeardListings.begin());
        EnumHeardListing entry = {};
        memcpy(entry.root, root_hash, SFPP_HASH_SIZE);
        entry.bucket = list.scope.bucket;
        entry.peer = peer;
        entry.total = list.frameTotal;
        enumHeardListings.push_back(entry);
        heard = &enumHeardListings.back();
    }
    heard->heardMs = nowMs;
    if (heard->seen[list.frameIndex])
        return;
    heard->seen[list.frameIndex] = true;
    heard->seenCount++;
    heard->ids.insert(heard->ids.end(), list.ids.begin(), list.ids.end());
    if (heard->seenCount < heard->total)
        return;

    std::vector<pinsketch::Element> own;
    collectBucketIds(root_hash, SFPP_HASH_SIZE, list.scope.bucket, own);
    std::sort(own.begin(), own.end());
    std::vector<pinsketch::Element> listed = heard->ids;
    std::sort(listed.begin(), listed.end());
    listed.erase(std::unique(listed.begin(), listed.end()), listed.end());
    const bool holdsAll = std::includes(own.begin(), own.end(), listed.begin(), listed.end());

    char rootHex[9];
    formatRootHex(root_hash, SFPP_HASH_SIZE, rootHex);
    LOG_INFO("SFPP-ENUM overheard listing root=%s bucket=%u peer=0x%08x ids=%u holds_all=%d", rootHex, list.scope.bucket, peer,
             (unsigned)listed.size(), holdsAll ? 1 : 0);

    // The lister's own pending request for this bucket is superseded by the listing; other archives'
    // pulls for the same bucket survive.
    uint32_t count = (uint32_t)listed.size();
    uint64_t checksum = 0;
    bool summaryKnown = false;
    for (auto it = enumPending.begin(); it != enumPending.end();) {
        if (it->bucket == list.scope.bucket && it->authority == peer && memcmp(it->root, root_hash, SFPP_HASH_SIZE) == 0) {
            if (it->summaryKnown) {
                count = it->count;
                checksum = it->checksum;
                summaryKnown = true;
            }
            it = enumPending.erase(it);
        } else {
            ++it;
        }
    }
    if (holdsAll) {
        enumHeardListings.erase(enumHeardListings.begin() + (heard - enumHeardListings.data()));
        return;
    }
    if (portduino_config.sfpp_enum_listen_only)
        return; // keep the complete listing so overheard item frames can be accepted
    // The hole is pulled from the lister on the next pacer slot, asking for items only.
    addPendingBucket(root_hash, peer, list.scope.bucket, count, checksum, 0);
    for (auto &entry : enumPending) {
        if (entry.bucket == list.scope.bucket && entry.authority == peer && memcmp(entry.root, root_hash, SFPP_HASH_SIZE) == 0)
            entry.summaryKnown = summaryKnown;
    }
    setIntervalFromNow(10);
}

bool StoreForwardPlusPlusModule::takeHeardListing(const uint8_t *root_hash, uint32_t bucket, NodeNum &peer,
                                                  std::vector<pinsketch::Element> &ids)
{
    for (auto it = enumHeardListings.begin(); it != enumHeardListings.end(); ++it) {
        if (it->bucket != bucket || memcmp(it->root, root_hash, SFPP_HASH_SIZE) != 0)
            continue;
        const bool usable = it->seenCount == it->total && !Throttle::hasElapsed(it->heardMs, kEnumHeardListingMs);
        if (usable) {
            peer = it->peer;
            ids = it->ids;
        }
        enumHeardListings.erase(it);
        return usable;
    }
    return false;
}

bool StoreForwardPlusPlusModule::bucketHoldsShortId(const uint8_t *root_hash, uint32_t bucket, pinsketch::Element id)
{
    sqlite3_reset(getObjectByShortIdStmt);
    sqlite3_bind_int(getObjectByShortIdStmt, 1, SFPP_HASH_SIZE);
    sqlite3_bind_blob(getObjectByShortIdStmt, 2, root_hash, SFPP_HASH_SIZE, SQLITE_STATIC);
    sqlite3_bind_int(getObjectByShortIdStmt, 3, (int)bucket);
    sqlite3_bind_int64(getObjectByShortIdStmt, 4, (sqlite3_int64)id);
    const bool held = sqlite3_step(getObjectByShortIdStmt) == SQLITE_ROW;
    sqlite3_reset(getObjectByShortIdStmt);
    return held;
}

bool StoreForwardPlusPlusModule::itemInFlight(const uint8_t *root_hash, uint32_t bucket, pinsketch::Element id)
{
    const uint32_t now = Time::getMillis();
    for (const auto &entry : enumInFlight) {
        if (entry.id == id && entry.bucket == bucket && entry.expiresMs > now &&
            memcmp(entry.root, root_hash, SFPP_HASH_SIZE) == 0)
            return true;
    }
    return false;
}

void StoreForwardPlusPlusModule::noteItemInFlight(const uint8_t *root_hash, uint32_t bucket, pinsketch::Element id)
{
    const uint32_t now = Time::getMillis();
    enumInFlight.erase(std::remove_if(enumInFlight.begin(), enumInFlight.end(),
                                      [&](const EnumInFlight &entry) { return entry.expiresMs <= now; }),
                       enumInFlight.end());
    enumInFlight.push_back({id, {}, bucket, now + itemRetryIntervalMs()});
    memcpy(enumInFlight.back().root, root_hash, SFPP_HASH_SIZE);
    while (enumInFlight.size() > 256)
        enumInFlight.pop_front();
}

void StoreForwardPlusPlusModule::forgetItemInFlight(pinsketch::Element id)
{
    enumInFlight.erase(
        std::remove_if(enumInFlight.begin(), enumInFlight.end(), [&](const EnumInFlight &entry) { return entry.id == id; }),
        enumInFlight.end());
}

void StoreForwardPlusPlusModule::requestMissingItems(const uint8_t *root_hash, uint32_t bucket, NodeNum peer,
                                                     const std::vector<pinsketch::Element> &ids)
{
    if (ids.empty() || ids.size() > sfppsr::kMaxShortIds)
        return;

    std::vector<pinsketch::Element> fresh;
    if (enumCollisionProbe) {
        fresh = ids; // a probe deliberately asks for every object a short ID names
    } else {
        for (pinsketch::Element id : ids) {
            if (itemInFlight(root_hash, bucket, id) || std::find(enumWanted.begin(), enumWanted.end(), id) != enumWanted.end() ||
                std::find(enumLateWanted.begin(), enumLateWanted.end(), id) != enumLateWanted.end() ||
                bucketHoldsShortId(root_hash, bucket, id))
                continue;
            fresh.push_back(id);
        }
    }
    if (fresh.empty())
        return;

    sfppsr::Scope scope;
    copyScopeRoot(root_hash, SFPP_HASH_SIZE, scope, bucket, false);
    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildItemRequest(scope, fresh, msg))
        return;

    for (pinsketch::Element id : fresh) {
        if (std::find(enumWanted.begin(), enumWanted.end(), id) == enumWanted.end())
            enumWanted.push_back(id);
        noteItemInFlight(root_hash, bucket, id);
    }
    enumWantedRemaining = enumWanted.size();
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

    noteRequesterActivity(mp.from); // a re-request refreshes the requester's entry
    logEnum("rx", "item_req", list.scope, mp.decoded.payload.size, mp.from, (int)list.ids.size(), 0, 1);

    for (pinsketch::Element wanted : list.ids) {
        // Already queued for this requester: do not duplicate it, just refresh the entry.
        if (enumRequesterHasQueued(mp.from, wanted))
            continue;

        sqlite3_reset(getObjectByShortIdStmt);
        sqlite3_bind_int(getObjectByShortIdStmt, 1, SFPP_HASH_SIZE);
        sqlite3_bind_blob(getObjectByShortIdStmt, 2, root, SFPP_HASH_SIZE, NULL);
        sqlite3_bind_int(getObjectByShortIdStmt, 3, (int)list.scope.bucket);
        sqlite3_bind_int64(getObjectByShortIdStmt, 4, (sqlite3_int64)wanted);

        // A short ID can name more than one stored object; serve every one so a collided pair
        // cannot hide behind a single row.
        bool served = false;
        while (sqlite3_step(getObjectByShortIdStmt) == SQLITE_ROW) {
            served = true;
            const uint32_t to = (uint32_t)sqlite3_column_int64(getObjectByShortIdStmt, 0);
            const uint32_t from = (uint32_t)sqlite3_column_int64(getObjectByShortIdStmt, 1);
            const uint32_t id = (uint32_t)sqlite3_column_int64(getObjectByShortIdStmt, 2);
            const uint8_t *enc = (const uint8_t *)sqlite3_column_blob(getObjectByShortIdStmt, 3);
            const int encLen = sqlite3_column_bytes(getObjectByShortIdStmt, 3);
            RowTime stamp;
            stamp.stamp = (uint32_t)sqlite3_column_int64(getObjectByShortIdStmt, 5);
            stamp.basis = (uint8_t)sqlite3_column_int(getObjectByShortIdStmt, 6);
            stamp.boot_id = (uint32_t)sqlite3_column_int64(getObjectByShortIdStmt, 7);
            stamp.margin = (uint32_t)sqlite3_column_int64(getObjectByShortIdStmt, 8);
            std::vector<uint8_t> cipher(enc, enc + encLen);

            const uint32_t heardAgo = rowAge(stamp).age;
            std::vector<meshtastic_StoreForwardPlusPlus> frames;
            if (!sfppframe::split(cipher.data(), cipher.size(), to, from, id, heardAgo, false, frames))
                continue;

            sfppsr::Scope scope;
            copyScopeRoot(root, SFPP_HASH_SIZE, scope, list.scope.bucket, false);
            // Every frame names its bucket so an overhearing archive can place it.
            for (auto &frame : frames)
                frame.chain_count = list.scope.bucket;
            const NodeNum dest = portduino_config.sfpp_item_transport == SFPP_ITEM_DIRECT ? mp.from : NODENUM_BROADCAST;
            queueItemFrames(frames, mp.from, dest, scope, wanted);
        }
        sqlite3_reset(getObjectByShortIdStmt);

        if (!served)
            LOG_WARN("StoreForwardpp item request for an unknown short ID 0x%08x", wanted);
    }

    if (anyEnumQueuePending())
        setIntervalFromNow(10); // the first frame goes on the next tick, not after a full drain interval
}

void StoreForwardPlusPlusModule::resendEnumList()
{
    sfppsr::Scope scope;
    copyScopeRoot(enumExchangeRoot, SFPP_HASH_SIZE, scope, enumExchangeBucket, false);
    meshtastic_StoreForwardPlusPlus msg;
    if (sfppsr::buildEnumRequest(scope, msg)) {
        msg.encapsulated_to = enumExchangePeer;
        sendEnumMessage(msg, NODENUM_BROADCAST, "enum_req");
    }
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
            // Paced frames may still be in flight; keep accepting them for a while.
            beginEnumLateAccept();
            endEnumExchange();
        }
    }
}

void StoreForwardPlusPlusModule::handleItemFrame(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    // The derived root belongs to the primary channel; a decoded packet carries its channel index.
    if (mp.channel != 0)
        return;
    const bool addressed = (mp.to == nodeDB->getNodeNum() || mp.to == NODENUM_BROADCAST);
    if (!addressed)
        return;

    uint8_t root[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root) == 0)
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

    // 1. An object this exchange asked for, from any peer that holds it.
    auto wantedIt = std::find(enumWanted.begin(), enumWanted.end(), shortId);
    if (enumExchangeActive && wantedIt != enumWanted.end()) {
        const bool isNew = !isInDB(hash, sizeof(hash));
        if (!storeRecoveredObject(replay, enumExchangeRoot, enumExchangeBucket))
            return;
        forgetItemInFlight(shortId);
        enumLastItemMs = Time::getMillis();
        if (isNew)
            enumRecovered++;

        if (enumCollisionProbe) {
            if (bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
                enumExchangeSucceeded = true;
                endEnumExchange();
            }
            return;
        }

        enumWanted.erase(wantedIt);
        enumWantedRemaining = enumWanted.size();
        if (!enumWanted.empty())
            return;

        if (enumSketchAttempt) {
            enumSketchAttempt = false;
            if (bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
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
        if (!enumPromoted.empty() && !bucketMatchesPeer(enumExchangeRoot, enumExchangeBucket)) {
            repairPromotions();
            return;
        }
        enumExchangeSucceeded = true;
        endEnumExchange();
        return;
    }

    // 2. A late object for a recently ended exchange.
    auto lateIt = std::find(enumLateWanted.begin(), enumLateWanted.end(), shortId);
    if (enumLateActive && lateIt != enumLateWanted.end()) {
        storeRecoveredObject(replay, enumLateRoot, enumLateBucket);
        forgetItemInFlight(shortId);
        enumLateWanted.erase(lateIt);
        if (enumLateWanted.empty())
            enumLateActive = false;
        return;
    }

    // 3. An object named by a complete listing we hold, or one already in flight.
    const uint32_t now = Time::getMillis();
    const EnumInFlight *flight = nullptr;
    for (const auto &entry : enumInFlight) {
        if (entry.id == shortId && entry.expiresMs > now && memcmp(entry.root, root, SFPP_HASH_SIZE) == 0) {
            flight = &entry;
            break;
        }
    }
    const EnumHeardListing *heard = nullptr;
    for (const auto &entry : enumHeardListings) {
        if (entry.seenCount != entry.total || Throttle::hasElapsed(entry.heardMs, kEnumHeardListingMs))
            continue;
        if (memcmp(entry.root, root, SFPP_HASH_SIZE) != 0)
            continue;
        if (std::find(entry.ids.begin(), entry.ids.end(), shortId) == entry.ids.end())
            continue;
        heard = &entry;
        break;
    }
    if (!flight && !heard)
        return;
    const uint32_t bucket = heard ? heard->bucket : flight->bucket;
    if (isInDB(hash, sizeof(hash)))
        return;
    if (!storeRecoveredObject(replay, root, bucket))
        return;
    forgetItemInFlight(shortId);

    // Suppress our own pending request for an object we just overheard.
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

void StoreForwardPlusPlusModule::promoteScratchMatches(const uint8_t *root_hash, uint32_t bucket,
                                                       std::vector<pinsketch::Element> &ids)
{
    if (ids.empty())
        return;

    struct ScratchRow {
        uint32_t to;
        uint32_t from;
        uint32_t id;
        RowTime time;
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

        // A short ID only names a row in this bucket; a collision in another bucket is not a match.
        if (sfppsr::contentBucket(computed, SFPP_HASH_SIZE) != bucket)
            continue;
        const pinsketch::Element sid = sfppsr::shortId(computed, SFPP_HASH_SIZE);
        if (std::find(ids.begin(), ids.end(), sid) == ids.end())
            continue;

        ScratchRow row;
        row.to = to;
        row.from = from;
        row.id = id;
        row.cipher.assign(cipher, cipher + cipherLen);
        memcpy(row.hash, computed, SFPP_HASH_SIZE);
        row.time.stamp = (uint32_t)sqlite3_column_int64(fromScratchStmt, 5);
        row.time.basis = (uint8_t)sqlite3_column_int(fromScratchStmt, 7);
        row.time.boot_id = (uint32_t)sqlite3_column_int64(fromScratchStmt, 8);
        row.time.margin = (uint32_t)sqlite3_column_int64(fromScratchStmt, 9);
        matches.push_back(row);
    }
    sqlite3_reset(fromScratchStmt);

    for (const auto &bad : corrupt)
        removeFromScratch(bad.data(), SFPP_HASH_SIZE);

    for (const auto &row : matches) {
        insertCanonObject(row.to, row.from, row.id, row.cipher.data(), row.cipher.size(), row.hash, row.time, "", root_hash,
                          bucket, row.from == nodeDB->getNodeNum());
        removeFromScratch(row.hash, SFPP_HASH_SIZE);
        const pinsketch::Element sid = sfppsr::shortId(row.hash, SFPP_HASH_SIZE);
        enumPromoted.push_back({sid, {}});
        memcpy(enumPromoted.back().hash, row.hash, SFPP_HASH_SIZE);
        ids.erase(std::remove(ids.begin(), ids.end(), sid), ids.end());
    }
}

#endif // has include sqlite3
