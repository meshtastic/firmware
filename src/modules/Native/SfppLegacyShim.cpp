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

// A follower's canonical archive rows carry the ciphertext but not the plaintext. Decrypt the primary channel's
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

bool StoreForwardPlusPlusModule::legacyStoreForwardServes()
{
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
    return storeForwardModule && storeForwardModule->isServer();
#else
    return false;
#endif
}

void StoreForwardPlusPlusModule::sendLegacyReply(NodeNum to, const meshtastic_StoreAndForward &payload)
{
    if (!consumeServeBudget())
        return; // the one-pacer covers every legacy reply, not only the text frames
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
    constexpr uint32_t retentionS = 48 * 60 * 60; // retention window
    constexpr uint32_t pageMax = 50;
    // A row with no epoch cannot advance an epoch cursor, so page those by rowid with a tagged cursor.
    constexpr uint32_t kRowidCursorBase = 0xF0000000;

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

    uint32_t cursor = 0;
    if (req->which_variant == meshtastic_StoreAndForward_history_tag)
        cursor = req->variant.history.last_request;
    const bool rowidCursor = cursor >= kRowidCursorBase;
    const uint32_t cursorRowid = rowidCursor ? cursor - kRowidCursorBase : 0;
    uint32_t lastServedRxTime = cursor;
    uint32_t count = 0;

    struct Candidate {
        legacy_history_item item;
        uint32_t destination;
        uint32_t display;
        sqlite3_int64 rowid;
    };
    std::vector<Candidate> page;

    uint8_t root_hash[SFPP_HASH_SIZE] = {0};
    if (getOrDeriveRootFromChannel(channels.getHash(0), root_hash) != 0) {
        const std::vector<SfppStore::LegacyRow> rows = store_.collectLegacyHistory(root_hash, requester);
        for (const auto &row : rows) {
            const RowAge age = store_.rowAge(row.time);
            if (age.age > windowS)
                continue; // outside the requested window, by the best age we can give it

            Candidate candidate;
            candidate.item.from = (NodeNum)row.from;
            candidate.item.id = (PacketId)row.id;
            candidate.destination = row.destination;
            candidate.display = age.has_epoch ? age.epoch : age.estimate_epoch;
            candidate.rowid = row.rowid;
            if (rowidCursor) {
                if (candidate.rowid <= (sqlite3_int64)cursorRowid)
                    continue;
            } else if (cursor > 0 && candidate.display <= cursor) {
                continue;
            }
            candidate.item.payload = row.payload;
            if (candidate.item.payload.empty()) {
                if (!decodeStoredText(row.encrypted.data(), row.encrypted.size(), candidate.destination, candidate.item.from,
                                      candidate.item.id, candidate.item.payload))
                    continue;
            }
            if (!age.has_epoch) {
                LOG_INFO("SFPP legacy shim serves a boot-relative row age=%u margin=%u estimated_epoch=%u", age.age, age.margin,
                         candidate.display);
            }
            page.push_back(std::move(candidate));
        }
    }

    std::stable_sort(page.begin(), page.end(), [](const Candidate &a, const Candidate &b) { return a.display < b.display; });
    bool servedEpoch = false;
    uint32_t maxDisplay = 0;
    uint32_t maxRowid = 0;
    for (auto &candidate : page) {
        if (count >= pageMax)
            break;
        legacy_history_item item = std::move(candidate.item);
        item.rx_time = candidate.display;
        if (candidate.display > 0) {
            servedEpoch = true;
            if (candidate.display > maxDisplay)
                maxDisplay = candidate.display;
        }
        if ((uint32_t)candidate.rowid > maxRowid)
            maxRowid = (uint32_t)candidate.rowid;
        item.to = requester;
        item.broadcast = candidate.destination == NODENUM_BROADCAST;
        legacyHistory.push_back(std::move(item));
        count++;
    }
    if (count > 0) {
        if (servedEpoch)
            lastServedRxTime = maxDisplay;
        else
            lastServedRxTime = kRowidCursorBase + maxRowid;
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

#endif // has include sqlite3
