// ideas:
// Track the nodenums we've gotten SFPP traffic from, to build a network graph
// store nodes, packet counts, and hops away in a new peers table

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

StoreForwardPlusPlusModule::StoreForwardPlusPlusModule()
    : ProtobufModule("StoreForwardpp",
                     portduino_config.sfpp_steal_port ? meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP
                                                      : meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP,
                     &meshtastic_StoreForwardPlusPlus_msg),
      concurrency::OSThread("StoreForwardpp")
{
    uint8_t derived[SFPP_HASH_SIZE] = {0};
    const bool haveDerived = deriveRootFromChannel(channels.getHash(0), derived);
    store_.openStore(derived, haveDerived);
    encryptedOk = true;
    this->setInterval(15 * 1000);
}

int32_t StoreForwardPlusPlusModule::runOnce()
{
    if (legacyHistoryBusy) {
        sendNextLegacyHistory();
        return legacyHistoryBusy ? 5 * 1000 : 10 * 1000;
    }
    // Promote the host's synchronized clock to NTP when we have no proper source yet, but never
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
    store_.updateTimeState();
    store_.healBootRelativeTimestamps();

    uint8_t root_hash_bytes[SFPP_HASH_SIZE] = {0};
    ChannelHash hash = channels.getHash(0);

    if (getOrDeriveRootFromChannel(hash, root_hash_bytes) == 0) {
        LOG_WARN("No root hash found, not sending");
        return 10 * 1000;
    }

    // Canon rows outside the retention window are gone even when the row cap is not reached.
    store_.trimCanonByRetention(root_hash_bytes, SFPP_HASH_SIZE);
    // evict old messages from scratch
    scratch_timed_out += store_.pruneScratchQueue();

    if (memfll(root_hash_bytes, '\0', SFPP_HASH_SIZE)) {
        LOG_WARN("No root hash found, not sending");
        return 10 * 1000;
    }

    // One bucket is advertised per tick; the next advert is the retry for an unfinished exchange.
    {
        expireEnumExchange();
        expireEnumLate();
        if (portduino_config.sfpp_enum_listen_only)
            return (int32_t)portduino_config.sfpp_enum_interval * 60 * 1000;
        pumpEnumRetries();
        if (drainOneEnumFrame())
            return enumDrainIntervalMs(); // serving a request takes priority over the advert cadence
        // Every archive advertises, but not in the middle of an exchange or with a pull queued.
        if (!enumExchangeActive && enumPending.empty() && !enumLateActive)
            advertiseWindow(root_hash_bytes, SFPP_HASH_SIZE);
        startPendingPull();
        if (enumExchangeActive)
            return 5 * 1000; // pump the probe idle and overall timeout while an exchange is in flight
        if (!enumPending.empty())
            return enumDrainIntervalMs(); // a paced pending pull is waiting
        if (enumLateActive)
            return 5 * 1000; // tighten the late-frame window expiry
        // Every archive advertises.
        const uint32_t advertMs = (uint32_t)portduino_config.sfpp_enum_interval * 60 * 1000;
        return (int32_t)advertMs;
    }
}

bool StoreForwardPlusPlusModule::buildBucketSummary(const uint8_t *root_hash, size_t root_hash_len, uint32_t bucket,
                                                    sfppsr::BucketSummary &out)
{
    return store_.buildBucketSummary(root_hash, root_hash_len, bucket, out);
}

bool StoreForwardPlusPlusModule::linkIsFresh(const stored_object &lo) const
{
    RowTime t;
    t.stamp = lo.rx_time;
    t.basis = kTimeBasisEpoch; // the wire's rxtime is an epoch claim; an unusable one counts as stale
    const RowAge age = store_.rowAge(t);
    return age.age + age.margin <= rebroadcastTimeout;
}

void StoreForwardPlusPlusModule::handleEncrypted(const meshtastic_MeshPacket *mp, const meshtastic_MeshPacket *p_encrypted)
{
    // This is intended to handle text messages from the local node
    if (mp->from != nodeDB->getNodeNum() && mp->from != 0) {
        return;
    }

    stored_object lo = ingestTextPacket(*mp, p_encrypted);

    if (lo.from == 0)
        lo.from = nodeDB->getNodeNum();
    const bool senderCopy = lo.from == nodeDB->getNodeNum() || mp->relay_node == nodeDB->getLastByteOfNodeNum(lo.from);

    if (lo.root_hash_len != 0) {
        store_.insertCanonObject(lo.to, lo.from, lo.id, lo.encrypted_bytes, lo.encrypted_len, lo.message_hash,
                                 RowTime{lo.rx_time, lo.time_basis, lo.boot_id, lo.age_margin}, lo.payload, lo.root_hash,
                                 sfppsr::contentBucket(lo.message_hash, lo.message_hash_len), senderCopy);
        enumWritesSinceAdvert++;
    }
    store_.fillPayloadIfEmpty(lo);
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
        stored_object lo = ingestTextPacket(mp, currentEncrypted);
        const bool senderCopy = mp.relay_node == nodeDB->getLastByteOfNodeNum(lo.from);

        if (store_.isInDB(lo.message_hash, lo.message_hash_len)) {
            LOG_DEBUG("StoreForwardpp Found text message in archive DB");
            // We may have this message already, but we may not have the payload.
            store_.fillPayloadIfEmpty(lo);
            return ProcessMessage::CONTINUE;
        }

        if (lo.root_hash_len != 0) {
            store_.insertCanonObject(lo.to, lo.from, lo.id, lo.encrypted_bytes, lo.encrypted_len, lo.message_hash,
                                     RowTime{lo.rx_time, lo.time_basis, lo.boot_id, lo.age_margin}, lo.payload, lo.root_hash,
                                     sfppsr::contentBucket(lo.message_hash, lo.message_hash_len), senderCopy);
            enumWritesSinceAdvert++;
        }
        store_.fillPayloadIfEmpty(lo);
        return ProcessMessage::CONTINUE;
    }

    const meshtastic_PortNum sfppPort = portduino_config.sfpp_steal_port ? meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP
                                                                         : meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    if (mp.decoded.portnum == sfppPort) {
        meshtastic_StoreForwardPlusPlus scratch;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_StoreForwardPlusPlus_fields,
                                 &scratch))
            handleReceivedProtobuf(mp, &scratch);
    }
    return ProcessMessage::CONTINUE;
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
    if (!store_.lookUpFullRootHash(derived, SFPP_HASH_SIZE, mapped)) {
        LOG_INFO("StoreForwardpp Deriving root hash from the channel");
        store_.addRootToMappings(_ch_hash, derived);
    }

    memcpy(_root_hash, derived, SFPP_HASH_SIZE);
    return SFPP_HASH_SIZE;
}

// should not need size considerations
StoreForwardPlusPlusModule::stored_object
StoreForwardPlusPlusModule::ingestTextPacket(const meshtastic_MeshPacket &mp, const meshtastic_MeshPacket *encrypted_meshpacket)
{
    stored_object lo;
    lo.to = mp.to;
    lo.from = mp.from;
    lo.id = mp.id;
    // Keep the age the packet arrived with; a live packet is normally zero, but a replay or a
    // queued API packet can be older. rx_time is never trusted when it points into the future.
    RowTime live = store_.nowTime();
    RowTime stamped;
    const uint32_t heardAgo = (mp.rx_time != 0 && mp.rx_time <= live.stamp) ? live.stamp - mp.rx_time : 0;
    if (!store_.timeFromAge(heardAgo, stamped))
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