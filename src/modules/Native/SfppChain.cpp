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
    sqlite3_bind_int64(chain_insert_stmt, 13, (sqlite3_int64)lo.time_basis);
    sqlite3_bind_int64(chain_insert_stmt, 14, (sqlite3_int64)lo.boot_id);
    sqlite3_bind_int64(chain_insert_stmt, 15, (sqlite3_int64)lo.age_margin);
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
    lo.time_basis = (uint8_t)sqlite3_column_int(fromScratchStmt, 7);
    lo.boot_id = (uint32_t)sqlite3_column_int64(fromScratchStmt, 8);
    lo.age_margin = (uint32_t)sqlite3_column_int64(fromScratchStmt, 9);

    lo.root_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.root_hash, root_hash, lo.root_hash_len);

    sqlite3_reset(fromScratchStmt);

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
    // The wire carries an absolute rxtime; treat it as an epoch and fall back to our own stamp when
    // the sender had none, so a clockless peer's uptime value is never filed as an ancient epoch.
    if (t->encapsulated_rxtime != 0) {
        lo.rx_time = t->encapsulated_rxtime;
        lo.time_basis = kTimeBasisEpoch;
    } else {
        const RowTime live = nowTime();
        lo.rx_time = live.stamp;
        lo.time_basis = live.basis;
        lo.boot_id = live.boot_id;
        lo.age_margin = live.margin;
    }
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

#endif // has include sqlite3
