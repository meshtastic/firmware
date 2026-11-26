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

// One item frame per drain keeps the radio's 16-entry TX queue from overflowing on a large request.
static constexpr size_t kEnumTxQueueMax = 256;
static constexpr size_t kEnumMaxRequesters = 16;

static constexpr uint32_t kEnumRequesterTtlMs = 2 * kEnumExchangeTimeoutMs;

// D11's one-pacer budget, shared by every serving path: 30 frames in any rolling 10 min.
static constexpr uint32_t kServeWindowMs = 10 * 60 * 1000;
static constexpr uint8_t kServeWindowFrames = 30;

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

#endif // has include sqlite3
