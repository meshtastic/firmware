// Eventual TODO: non-stratum0 nodes need to be pointed at their upstream source? Maybe
#if __has_include("sqlite3.h")

#include "StoreForwardPlusPlus.h"
#include "MeshService.h"
#include "RTC.h"
#include "SHA256.h"
#include "meshUtils.h"
#include "modules/RoutingModule.h"

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
        PRIMARY KEY (message_hash)     \
        );",
                       NULL, NULL, &err);

    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    // mappings table -- connects the root hashes to channel hashes and DM identifiers
    res = sqlite3_exec(ppDb, "         \
        CREATE TABLE IF NOT EXISTS     \
        mappings(                      \
        chain_type INT NOT NULL,       \
        identifier INT NOT NULL,       \
        root_hash BLOB NOT NULL,       \
        count INT DEFAULT 0,           \
        PRIMARY KEY (identifier)       \
        );",
                       NULL, NULL, &err);

    if (res != SQLITE_OK) {
        LOG_ERROR("Failed to create table: %s", sqlite3_errmsg(ppDb));
    }
    if (err != nullptr)
        LOG_ERROR("%s", err);
    sqlite3_free(err);

    // store schema version somewhere

    // prepared statements *should* make this faster.
    sqlite3_prepare_v2(ppDb, "INSERT INTO channel_messages (destination, sender, packet_id, root_hash, \
        encrypted_bytes, message_hash, rx_time, commit_hash, payload, counter) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &chain_insert_stmt, NULL);

    sqlite3_prepare_v2(ppDb, "INSERT INTO local_messages (destination, sender, packet_id, root_hash, \
        encrypted_bytes, message_hash, rx_time, payload) VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
                       -1, &scratch_insert_stmt, NULL);

    sqlite3_prepare_v2(ppDb, "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, root_hash \
        from local_messages where root_hash=? order by rx_time asc LIMIT 1;", // earliest first
                       -1, &fromScratchStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, root_hash, payload \
        from local_messages where substr(message_hash,1,?)=? order by rx_time asc LIMIT 1;", // earliest first
                       -1, &fromScratchByHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "SELECT COUNT(*) from channel_messages where substr(message_hash,1,?)=?", -1, &checkDup, NULL);

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
        "select destination, sender, packet_id, encrypted_bytes, message_hash, rx_time, commit_hash, root_hash, counter, payload \
        from channel_messages where substr(commit_hash,1,?)=?;",
        -1, &getLinkStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select identifier from mappings where substr(root_hash,1,?)=?;", -1, &getHashFromRootStmt, NULL);

    sqlite3_prepare_v2(ppDb, "INSERT INTO mappings (chain_type, identifier, root_hash) VALUES(?, ?, ?);", -1,
                       &addRootToMappingsStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select root_hash from mappings where identifier=?;", -1, &getRootFromChannelHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "select root_hash from mappings where substr(root_hash,1,?)=?;", -1, &getFullRootHashStmt, NULL);

    sqlite3_prepare_v2(ppDb, "UPDATE mappings SET count=? WHERE substr(root_hash,1,?)=?;", -1, &setChainCountStmt, NULL);

    sqlite3_prepare_v2(ppDb, "SELECT count(*) FROM channel_messages WHERE substr(root_hash,1,?)=?;", -1, &getChainCountStmt,
                       NULL);

    sqlite3_prepare_v2(ppDb, "DELETE FROM local_messages WHERE rx_time < ?;", -1, &pruneScratchQueueStmt, NULL);

    sqlite3_prepare_v2(ppDb,
                       "DELETE FROM channel_messages WHERE commit_hash in ( select commit_hash from channel_messages where "
                       "substr(root_hash,1,?)=? ORDER BY rowid ASC LIMIT 1);",
                       -1, &trimOldestLinkStmt, NULL);

    encryptedOk = true;

    this->setInterval(portduino_config.sfpp_announce_interval * 60 * 1000);
}

int32_t StoreForwardPlusPlusModule::runOnce()
{
    if (pendingRun) {
        pendingRun = false;
        setIntervalFromNow(portduino_config.sfpp_announce_interval * 60 * 1000 - 60 * 1000);
    }
    if (getRTCQuality() < RTCQualityNTP) {
        RTCQuality ourQuality = RTCQualityDevice;

        std::string timeCommandResult = exec("timedatectl status | grep synchronized | grep yes -c");
        if (timeCommandResult[0] == '1') {
            ourQuality = RTCQualityNTP;

            struct timeval tv;
            tv.tv_sec = time(NULL);
            tv.tv_usec = 0;
            perhapsSetRTC(ourQuality, &tv);
        } else {
            LOG_WARN("StoreForward++ deferred due to time quality %u result:%s", getRTCQuality(), timeCommandResult.c_str());
            return portduino_config.sfpp_announce_interval * 60 * 1000;
        }
    }
    uint8_t root_hash_bytes[SFPP_HASH_SIZE] = {0};
    ChannelHash hash = channels.getHash(0);
    getOrAddRootFromChannelHash(hash, root_hash_bytes);
    uint32_t chain_count = getChainCount(root_hash_bytes, SFPP_HASH_SIZE);
    LOG_DEBUG("Chain count is %u", chain_count);
    while (chain_count > portduino_config.sfpp_max_chain) {
        LOG_DEBUG("Chain length %u exceeds max %u, evicting oldest", chain_count, portduino_config.sfpp_max_chain);
        trimOldestLink(root_hash_bytes, SFPP_HASH_SIZE);
        chain_count--;
    }
    // evict old messages from scratch
    pruneScratchQueue();

    if (memfll(root_hash_bytes, '\0', SFPP_HASH_SIZE)) {
        LOG_WARN("No root hash found, not sending");
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    if (doing_split_send) {
        LOG_DEBUG("Sending split second half");
        broadcastLink(split_link_out, true, true);
        split_link_out = link_object();
        split_link_out.validObject = false;
        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    // get tip of chain for this channel
    link_object chain_end = getLinkFromCount(0, root_hash_bytes, SFPP_HASH_SIZE);

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
        service->sendToMesh(p, RX_SRC_LOCAL, true);

        return portduino_config.sfpp_announce_interval * 60 * 1000;
    }

    // broadcast the tip of the chain
    canonAnnounce(chain_end.message_hash, chain_end.commit_hash, root_hash_bytes, chain_end.rx_time);

    // eventually timeout things on the scratch queue
    return portduino_config.sfpp_announce_interval * 60 * 1000;
}

ProcessMessage StoreForwardPlusPlusModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // To avoid terrible time problems, require NTP or GPS time
    if (getRTCQuality() < RTCQualityNTP) {
        return ProcessMessage::CONTINUE;
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
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want
    }
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && mp.to == NODENUM_BROADCAST) {
        link_object lo = ingestTextPacket(mp, router->p_encrypted);

        if (isInDB(lo.message_hash, lo.message_hash_len)) {
            LOG_DEBUG("Found text message in chain DB");
            // We may have this message already, but we may not have the payload
            // if we do, we can update the payload in the database
            if (lo.payload != "")
                updatePayload(lo.message_hash, lo.message_hash_len, lo.payload);
            return ProcessMessage::CONTINUE;
        }

        if (!portduino_config.sfpp_stratum0) {
            if (!isInDB(lo.message_hash, lo.message_hash_len)) {
                if (lo.root_hash_len == 0) {
                    LOG_DEBUG("Received text message, but no chain. Possibly no Stratum0 on local mesh.");
                    return ProcessMessage::CONTINUE;
                }
                addToScratch(lo);
                LOG_DEBUG("added message to scratch db");
                // send link to upstream?
            }
            return ProcessMessage::CONTINUE;
        }
        addToChain(lo);

        if (!pendingRun) {
            setIntervalFromNow(60 * 1000); // run again in 60 seconds to announce the new tip of chain
            pendingRun = true;
        }
        // canonAnnounce(lo.message_hash, lo.commit_hash, lo.root_hash, lo.rx_time);
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want
        // TODO: Block packets from self?
    } else if (mp.decoded.portnum == portduino_config.sfpp_steal_port ? meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP
                                                                      : meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP) {
        meshtastic_StoreForwardPlusPlus scratch;
        pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_StoreForwardPlusPlus_fields, &scratch);
        handleReceivedProtobuf(mp, &scratch);
        return ProcessMessage::CONTINUE;
    }
    return ProcessMessage::CONTINUE;
}

bool StoreForwardPlusPlusModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreForwardPlusPlus *t)
{
    LOG_DEBUG("StoreForwardpp node %u sent us sf++ packet", mp.from);
    printBytes("commit_hash ", t->commit_hash.bytes, t->commit_hash.size);
    printBytes("root_hash ", t->root_hash.bytes, t->root_hash.size);

    link_object incoming_link;
    incoming_link.validObject = false;

    if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_CANON_ANNOUNCE) {

        // TODO: Regardless of where we are in the chain, if we have a newer message, send it back.
        if (portduino_config.sfpp_stratum0) {
            LOG_WARN("Received a CANON_ANNOUNCE while stratum 0");
            uint8_t next_commit_hash[SFPP_HASH_SIZE] = {0};
            if (getNextHash(t->root_hash.bytes, t->root_hash.size, t->commit_hash.bytes, t->commit_hash.size, next_commit_hash)) {
                printBytes("next chain hash: ", next_commit_hash, SFPP_HASH_SIZE);
                if (airTime->isTxAllowedChannelUtil(true)) {
                    broadcastLink(next_commit_hash, SFPP_HASH_SIZE);
                }
            }
        } else {
            uint8_t tmp_root_hash_bytes[SFPP_HASH_SIZE] = {0};

            LOG_DEBUG("Received a CANON_ANNOUNCE");
            if (getRootFromChannelHash(router->p_encrypted->channel, tmp_root_hash_bytes)) {
                // we found the hash, check if it's the right one
                if (memcmp(tmp_root_hash_bytes, t->root_hash.bytes, t->root_hash.size) != 0) {
                    LOG_INFO("Root hash does not match. Possibly two stratum0 nodes on the mesh?");
                    return true;
                }
            } else {
                addRootToMappings(router->p_encrypted->channel, t->root_hash.bytes);
                LOG_DEBUG("Adding root hash to mappings");
            }
            if (t->encapsulated_rxtime == 0) {
                LOG_DEBUG("No encapsulated time, conclude the chain is empty");
                return true;
            }

            // get tip of chain for this channel
            link_object chain_end = getLinkFromCount(0, t->root_hash.bytes, t->root_hash.size);

            // get chain tip
            if (chain_end.rx_time != 0) {
                if (memcmp(chain_end.commit_hash, t->commit_hash.bytes, t->commit_hash.size) == 0) {
                    LOG_DEBUG("End of chain matches!");
                    sendFromScratch(chain_end.root_hash);
                } else {
                    LOG_DEBUG("End of chain does not match!");

                    // We just got an end of chain announce, checking if we have seen this message and have it in scratch.
                    if (isInScratch(t->message_hash.bytes, t->message_hash.size)) {
                        link_object scratch_object = getFromScratch(t->message_hash.bytes, t->message_hash.size);
                        // if this matches, we don't need to request the message
                        // we know exactly what it is
                        if (t->message_hash.size >= 8 &&
                            checkCommitHash(scratch_object, t->commit_hash.bytes, t->message_hash.size)) {
                            scratch_object.rx_time = t->encapsulated_rxtime;
                            addToChain(scratch_object);
                            removeFromScratch(scratch_object.message_hash, scratch_object.message_hash_len);
                            // short circuit and return
                            // falls through to a request for the message
                            return true;
                        }
                    }
                    if (airTime->isTxAllowedChannelUtil(true)) {
                        requestNextMessage(t->root_hash.bytes, t->root_hash.size, chain_end.commit_hash, SFPP_HASH_SIZE);
                    }
                }
            } else { // if chainEnd()
                if (airTime->isTxAllowedChannelUtil(true)) {
                    LOG_DEBUG("New chain, requesting last %u messages", portduino_config.sfpp_initial_sync);
                    requestMessageCount(t->root_hash.bytes, t->root_hash.size, portduino_config.sfpp_initial_sync);
                }
            }
        }
    } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_REQUEST) {
        uint8_t next_commit_hash[SFPP_HASH_SIZE] = {0};

        LOG_DEBUG("Received link request");

        // If chain_count is set, this is a request for x messages up the chain.
        if (t->chain_count != 0 && t->root_hash.size >= 8) {
            link_object link_from_count = getLinkFromCount(t->chain_count, t->root_hash.bytes, t->root_hash.size);
            LOG_DEBUG("Count requested %d", t->chain_count);
            if (link_from_count.validObject)
                broadcastLink(link_from_count, true);

        } else if (getNextHash(t->root_hash.bytes, t->root_hash.size, t->commit_hash.bytes, t->commit_hash.size,
                               next_commit_hash)) {
            printBytes("next chain hash: ", next_commit_hash, SFPP_HASH_SIZE);

            broadcastLink(next_commit_hash, SFPP_HASH_SIZE);
        }

        // if root and chain hashes are the same, grab the first message on the chain
        // if different, get the message directly after.

    } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE) {
        LOG_DEBUG("Link Provide received!");

        incoming_link = ingestLinkMessage(t);
    } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_FIRSTHALF) {
        LOG_DEBUG("Link Provide First Half received!");
        split_link_in = ingestLinkMessage(t, false);
        doing_split_receive = true;
        split_link_in.validObject = true;
        return true;

    } else if (t->sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE_SECONDHALF) {
        LOG_DEBUG("Link Provide Second Half received!");
        if (!doing_split_receive) {
            LOG_DEBUG("Received second half without first half, ignoring");
            return true;
        }
        if (!split_link_in.validObject) {
            LOG_WARN("No first half stored, cannot combine");
            doing_split_receive = false;
            return true;
        }
        link_object second_half = ingestLinkMessage(t, false);
        if (split_link_in.encrypted_len + second_half.encrypted_len > 256) {
            LOG_WARN("Combined link too large");
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

            // append the encrypted bytes

            // clear first half
            split_link_in = link_object();
            split_link_in.validObject = false;
            doing_split_receive = false;
            // do the recalcualte step we skipped
            // TODO put this in a function
            SHA256 message_hash;
            message_hash.reset();
            message_hash.update(incoming_link.encrypted_bytes, incoming_link.encrypted_len);
            message_hash.update(&incoming_link.to, sizeof(incoming_link.to));
            message_hash.update(&incoming_link.from, sizeof(incoming_link.from));
            message_hash.update(&incoming_link.id, sizeof(incoming_link.id));
            message_hash.finalize(incoming_link.message_hash, SFPP_HASH_SIZE);
            incoming_link.message_hash_len = SFPP_HASH_SIZE;

            // look up full root hash and copy over the partial if it matches
            if (lookUpFullRootHash(t->root_hash.bytes, t->root_hash.size, incoming_link.root_hash)) {
                printBytes("Found full root hash: 0x", incoming_link.root_hash, SFPP_HASH_SIZE);
                incoming_link.root_hash_len = SFPP_HASH_SIZE;
            } else {
                LOG_WARN("root hash does not match %d bytes", t->root_hash.size);
                incoming_link.root_hash_len = 0;
                incoming_link.validObject = false;
                return true;
            }

            if (t->commit_hash.size == SFPP_HASH_SIZE && getChainCount(t->root_hash.bytes, t->root_hash.size) == 0 &&
                portduino_config.sfpp_initial_sync != 0 && !portduino_config.sfpp_stratum0) {
                incoming_link.commit_hash_len = SFPP_HASH_SIZE;
                memcpy(incoming_link.commit_hash, t->commit_hash.bytes, SFPP_HASH_SIZE);

            } else if (t->commit_hash.size > 0) {
                // calculate the full commit hash and replace the partial if it matches
                if (checkCommitHash(incoming_link, t->commit_hash.bytes, t->commit_hash.size)) {
                    printBytes("commit hash matches: 0x", t->commit_hash.bytes, t->commit_hash.size);
                } else {
                    LOG_WARN("commit hash does not match, rejecting link.");
                    incoming_link.commit_hash_len = 0;
                    incoming_link.validObject = false;
                }
            }
        } else {
            LOG_WARN("No first half stored, cannot combine");
            return true;
        }
    }

    if (incoming_link.validObject) {
        if (incoming_link.root_hash_len == 0) {
            LOG_WARN("Hash bytes not found for incoming link");
            return true;
        }

        if (!incoming_link.validObject) {
            LOG_WARN("commit byte mismatch");
            return true;
        }

        if (portduino_config.sfpp_stratum0) {
            if (isInDB(incoming_link.message_hash, incoming_link.message_hash_len)) {
                LOG_INFO("Received link already in chain");
                // TODO: respond with next link?
                return true;
            }

            // calculate the commit_hash
            addToChain(incoming_link);
            if (!pendingRun) {
                setIntervalFromNow(60 * 1000); // run again in 60 seconds to announce the new tip of chain
                pendingRun = true;
            }
            // timebox to no more than an hour old
            if (incoming_link.rx_time > getValidTime(RTCQuality::RTCQualityNTP, true) - rebroadcastTimeout) {
                // if this packet is new to us, we rebroadcast it
                rebroadcastLinkObject(incoming_link);
            }

        } else {
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
                    if (incoming_link.rx_time > getValidTime(RTCQuality::RTCQualityNTP, true) - rebroadcastTimeout) {
                        rebroadcastLinkObject(incoming_link);
                    }
                }
                requestNextMessage(incoming_link.root_hash, incoming_link.root_hash_len, incoming_link.commit_hash,
                                   incoming_link.commit_hash_len);
            } else {
                if (!isInScratch(incoming_link.message_hash, incoming_link.message_hash_len) &&
                    !isInDB(incoming_link.message_hash, incoming_link.message_hash_len)) {
                    addToScratch(incoming_link);
                    LOG_INFO("added incoming non-canon message to scratch");
                    if (incoming_link.rx_time > getValidTime(RTCQuality::RTCQualityNTP, true) - rebroadcastTimeout) {
                        rebroadcastLinkObject(incoming_link);
                    }
                }
            }
        }
    }

    return true;
}

bool StoreForwardPlusPlusModule::getRootFromChannelHash(ChannelHash _ch_hash, uint8_t *_root_hash)
{
    bool found = false;
    sqlite3_bind_int(getRootFromChannelHashStmt, 1, _ch_hash);
    sqlite3_step(getRootFromChannelHashStmt);
    uint8_t *tmp_root_hash = (uint8_t *)sqlite3_column_blob(getRootFromChannelHashStmt, 0);
    if (tmp_root_hash) {
        memcpy(_root_hash, tmp_root_hash, SFPP_HASH_SIZE);
        found = true;
    }
    sqlite3_reset(getRootFromChannelHashStmt);
    return found;
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

// return code indicates bytes in root hash, or 0 if not found/added
size_t StoreForwardPlusPlusModule::getOrAddRootFromChannelHash(ChannelHash _ch_hash, uint8_t *_root_hash)
{
    bool wasFound = getRootFromChannelHash(_ch_hash, _root_hash);

    if (!wasFound) {
        if (portduino_config.sfpp_stratum0) {
            LOG_INFO("Generating Root hash!");
            SHA256 root_hash;
            root_hash.update(&_ch_hash, sizeof(_ch_hash));
            NodeNum ourNode = nodeDB->getNodeNum();
            root_hash.update(&ourNode, sizeof(ourNode));
            uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
            root_hash.update(&rtc_sec, sizeof(rtc_sec));
            root_hash.finalize(_root_hash, SFPP_HASH_SIZE);
            addRootToMappings(_ch_hash, _root_hash);
            wasFound = true;
        }
    }
    if (wasFound)
        return SFPP_HASH_SIZE;
    else
        return 0;
}

void StoreForwardPlusPlusModule::addRootToMappings(ChannelHash _ch_hash, uint8_t *_root_hash)
{
    int type = chain_types::channel_chain;
    sqlite3_bind_int(addRootToMappingsStmt, 1, type);
    sqlite3_bind_int(addRootToMappingsStmt, 2, _ch_hash);
    sqlite3_bind_blob(addRootToMappingsStmt, 3, _root_hash, SFPP_HASH_SIZE, NULL);
    auto rc = sqlite3_step(addRootToMappingsStmt);
    LOG_WARN("result %u, %s", rc, sqlite3_errmsg(ppDb));
    sqlite3_reset(addRootToMappingsStmt);
}

// TODO: make DM?
void StoreForwardPlusPlusModule::requestNextMessage(uint8_t *_root_hash, size_t _root_hash_len, uint8_t *_commit_hash,
                                                    size_t _commit_hash_len)
{

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
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

void StoreForwardPlusPlusModule::requestMessageCount(uint8_t *_root_hash, size_t _root_hash_len, uint32_t count)
{

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
    service->sendToMesh(p, RX_SRC_LOCAL, true);
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
            LOG_WARN("Get Hash error %u, %s", rc, sqlite3_errmsg(ppDb));
        }
        uint8_t *tmp_commit_hash = (uint8_t *)sqlite3_column_blob(getNextHashStmt, 0);
        if (tmp_commit_hash == nullptr) {
            sqlite3_reset(getNextHashStmt);
            return false;
        }
        printBytes("commit_hash", tmp_commit_hash, SFPP_HASH_SIZE);
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

    sqlite3_reset(getLinkStmt);

    LOG_INFO("Send link to mesh");
    broadcastLink(lo, false);
}

// TODO if stratum0, send the chain count
void StoreForwardPlusPlusModule::broadcastLink(link_object &lo, bool full_commit_hash, bool is_split_second_half)
{
    meshtastic_StoreForwardPlusPlus storeforward = meshtastic_StoreForwardPlusPlus_init_zero;
    storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_LINK_PROVIDE;
    if (lo.encrypted_len > 180) {
        LOG_WARN("Link too large to send (%u bytes)", lo.encrypted_len);
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
    LOG_INFO("Send link to mesh");
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

//
StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::getLink(uint8_t *_commit_hash, size_t _commit_hash_len)
{
    link_object lo;
    sqlite3_bind_int(getLinkStmt, 1, _commit_hash_len);
    sqlite3_bind_blob(getLinkStmt, 2, _commit_hash, _commit_hash_len, NULL);
    int res = sqlite3_step(getLinkStmt);

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
    lo.commit_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.commit_hash, _tmp_commit_hash, lo.commit_hash_len);

    uint8_t *_root_hash = (uint8_t *)sqlite3_column_blob(getLinkStmt, 7);
    lo.root_hash_len = SFPP_HASH_SIZE;
    memcpy(lo.root_hash, _root_hash, lo.root_hash_len);

    lo.counter = sqlite3_column_int(getLinkStmt, 8);

    lo.payload = std::string((char *)sqlite3_column_text(getLinkStmt, 9));

    lo.channel_hash = getChannelHashFromRoot(lo.root_hash, lo.root_hash_len);

    sqlite3_reset(getLinkStmt);

    return lo;
}

bool StoreForwardPlusPlusModule::sendFromScratch(uint8_t *root_hash)
{
    link_object lo;
    sqlite3_bind_blob(fromScratchStmt, 1, root_hash, SFPP_HASH_SIZE, NULL);
    if (sqlite3_step(fromScratchStmt) == SQLITE_DONE) {
        return false;
    }
    lo.to = sqlite3_column_int(fromScratchStmt, 0);
    lo.from = sqlite3_column_int(fromScratchStmt, 1);
    lo.id = sqlite3_column_int(fromScratchStmt, 2);

    uint8_t *_encrypted = (uint8_t *)sqlite3_column_blob(fromScratchStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(fromScratchStmt, 3);
    memcpy(lo.encrypted_bytes, _encrypted, lo.encrypted_len);

    uint8_t *_message_hash = (uint8_t *)sqlite3_column_blob(fromScratchStmt, 4);

    lo.rx_time = sqlite3_column_int(fromScratchStmt, 5);

    lo.root_hash_len = SFPP_SHORT_HASH_SIZE;
    memcpy(lo.root_hash, root_hash, lo.root_hash_len);

    sqlite3_reset(fromScratchStmt);

    printBytes("Send link to mesh ", _message_hash, 8);
    LOG_WARN("Size: %d", lo.encrypted_len);
    printBytes("encrypted ", lo.encrypted_bytes, lo.encrypted_len);
    broadcastLink(lo, false);

    return true;
}

bool StoreForwardPlusPlusModule::addToChain(link_object &lo)
{
    link_object chain_end = getLinkFromCount(0, lo.root_hash, lo.root_hash_len);

    // we may need to calculate the full commit hash at this point
    if (lo.commit_hash_len != SFPP_HASH_SIZE) {
        SHA256 commit_hash;

        commit_hash.reset();

        if (chain_end.commit_hash_len == SFPP_HASH_SIZE) {
            printBytes("last message: 0x", chain_end.commit_hash, SFPP_HASH_SIZE);
            commit_hash.update(chain_end.commit_hash, SFPP_HASH_SIZE);
        } else {
            printBytes("new chain root: 0x", lo.root_hash, SFPP_HASH_SIZE);
            commit_hash.update(lo.root_hash, SFPP_HASH_SIZE);
        }

        commit_hash.update(lo.message_hash, SFPP_HASH_SIZE);
        commit_hash.finalize(lo.commit_hash, SFPP_HASH_SIZE);
    }
    lo.counter = chain_end.counter + 1;
    // push a message into the local chain DB
    // destination
    sqlite3_bind_int(chain_insert_stmt, 1, lo.to);
    // sender
    sqlite3_bind_int(chain_insert_stmt, 2, lo.from);
    // packet_id
    sqlite3_bind_int(chain_insert_stmt, 3, lo.id);
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
    int res = sqlite3_step(chain_insert_stmt);
    if (res != SQLITE_OK) {
        LOG_ERROR("Cannot step: %s", sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(chain_insert_stmt);
    setChainCount(lo.root_hash, SFPP_HASH_SIZE, lo.counter);
    return true;
}

bool StoreForwardPlusPlusModule::addToScratch(link_object &lo)
{
    // push a message into the local chain DB
    // destination
    sqlite3_bind_int(scratch_insert_stmt, 1, lo.to);
    // sender
    sqlite3_bind_int(scratch_insert_stmt, 2, lo.from);
    // packet_id
    sqlite3_bind_int(scratch_insert_stmt, 3, lo.id);
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

    int res = sqlite3_step(scratch_insert_stmt);
    if (res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(ppDb);
        LOG_WARN("step %u, %s", res, _error_mesg);
    }
    sqlite3_reset(scratch_insert_stmt);
    return true;
}

void StoreForwardPlusPlusModule::canonAnnounce(uint8_t *_message_hash, uint8_t *_commit_hash, uint8_t *_root_hash,
                                               uint32_t _rx_time)
{
    meshtastic_StoreForwardPlusPlus storeforward = meshtastic_StoreForwardPlusPlus_init_zero;
    storeforward.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_CANON_ANNOUNCE;
    // set root hash

    // set message hash
    storeforward.message_hash.size = 8;
    memcpy(storeforward.message_hash.bytes, _message_hash, 8);

    // set chain hash
    storeforward.commit_hash.size = 8;
    memcpy(storeforward.commit_hash.bytes, _commit_hash, 8);

    // set root hash
    // needs to be the full hash to bootstrap
    storeforward.root_hash.size = SFPP_HASH_SIZE;
    memcpy(storeforward.root_hash.bytes, _root_hash, SFPP_HASH_SIZE);

    storeforward.encapsulated_rxtime = _rx_time;
    // storeforward.
    meshtastic_MeshPacket *p = allocDataProtobuf(storeforward);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->channel = 0;
    p->hop_limit = portduino_config.sfpp_hops;
    p->hop_start = portduino_config.sfpp_hops;
    LOG_INFO("Send packet to mesh payload size %u", p->decoded.payload.size);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

bool StoreForwardPlusPlusModule::isInDB(uint8_t *message_hash_bytes, size_t message_hash_len)
{
    sqlite3_bind_int(checkDup, 1, message_hash_len);
    sqlite3_bind_blob(checkDup, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(checkDup);
    int numberFound = sqlite3_column_int(checkDup, 0);
    sqlite3_reset(checkDup);
    if (numberFound > 0)
        return true;
    return false;
}

bool StoreForwardPlusPlusModule::isInScratch(uint8_t *message_hash_bytes, size_t message_hash_len)
{
    sqlite3_bind_int(checkScratch, 1, message_hash_len);
    sqlite3_bind_blob(checkScratch, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(checkScratch);
    int numberFound = sqlite3_column_int(checkScratch, 0);
    sqlite3_reset(checkScratch);
    if (numberFound > 0)
        return true;
    return false;
}

void StoreForwardPlusPlusModule::removeFromScratch(uint8_t *message_hash_bytes, size_t message_hash_len)
{
    printBytes("removing from scratch: ", message_hash_bytes, message_hash_len);
    sqlite3_bind_int(removeScratch, 1, message_hash_len);
    sqlite3_bind_blob(removeScratch, 2, message_hash_bytes, message_hash_len, NULL);
    sqlite3_step(removeScratch);
    sqlite3_reset(removeScratch);
}

void StoreForwardPlusPlusModule::updatePayload(uint8_t *message_hash_bytes, size_t message_hash_len, std::string payload)
{
    LOG_WARN("updatePayload");
    sqlite3_bind_text(updatePayloadStmt, 1, payload.c_str(), payload.length(), NULL);
    sqlite3_bind_int(updatePayloadStmt, 2, message_hash_len);
    sqlite3_bind_blob(updatePayloadStmt, 3, message_hash_bytes, message_hash_len, NULL);
    auto res = sqlite3_step(updatePayloadStmt);
    if (res != SQLITE_OK) {
        const char *_error_mesg = sqlite3_errmsg(ppDb);
        LOG_WARN("step error %u, %s", res, _error_mesg);
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
        LOG_WARN("step error %u, %s", res, _error_mesg);
    }
    lo.to = sqlite3_column_int(fromScratchByHashStmt, 0);
    lo.from = sqlite3_column_int(fromScratchByHashStmt, 1);
    lo.id = sqlite3_column_int(fromScratchByHashStmt, 2);

    uint8_t *encrypted_bytes = (uint8_t *)sqlite3_column_blob(fromScratchByHashStmt, 3);
    lo.encrypted_len = sqlite3_column_bytes(fromScratchByHashStmt, 3);
    memcpy(lo.encrypted_bytes, encrypted_bytes, lo.encrypted_len);
    uint8_t *message_hash = (uint8_t *)sqlite3_column_blob(fromScratchByHashStmt, 4);
    memcpy(lo.message_hash, message_hash, SFPP_HASH_SIZE);
    lo.rx_time = sqlite3_column_int(fromScratchByHashStmt, 5);
    uint8_t *root_hash = (uint8_t *)sqlite3_column_blob(fromScratchByHashStmt, 6);
    memcpy(lo.root_hash, root_hash, SFPP_HASH_SIZE);
    lo.payload =
        std::string((char *)sqlite3_column_text(fromScratchByHashStmt, 7), sqlite3_column_bytes(fromScratchByHashStmt, 7));
    lo.message_hash_len = hash_len;
    memcpy(lo.message_hash, message_hash_bytes, hash_len);
    sqlite3_reset(fromScratchByHashStmt);
    return lo;
}

// should not need size considerations
StoreForwardPlusPlusModule::link_object
StoreForwardPlusPlusModule::ingestTextPacket(const meshtastic_MeshPacket &mp, const meshtastic_MeshPacket *encrypted_meshpacket)
{
    link_object lo;
    SHA256 message_hash;
    lo.to = mp.to;
    lo.from = mp.from;
    lo.id = mp.id;
    lo.rx_time = mp.rx_time;
    lo.channel_hash = encrypted_meshpacket->channel;
    memcpy(lo.encrypted_bytes, encrypted_meshpacket->encrypted.bytes, encrypted_meshpacket->encrypted.size);
    lo.encrypted_len = encrypted_meshpacket->encrypted.size;
    lo.payload = std::string((char *)mp.decoded.payload.bytes, mp.decoded.payload.size);

    message_hash.reset();
    message_hash.update(encrypted_meshpacket->encrypted.bytes, encrypted_meshpacket->encrypted.size);
    message_hash.update(&mp.to, sizeof(mp.to));
    message_hash.update(&mp.from, sizeof(mp.from));
    message_hash.update(&mp.id, sizeof(mp.id));
    message_hash.finalize(lo.message_hash, SFPP_HASH_SIZE);
    lo.message_hash_len = SFPP_HASH_SIZE;

    lo.root_hash_len = getOrAddRootFromChannelHash(encrypted_meshpacket->channel, lo.root_hash);
    return lo;
}

StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::ingestLinkMessage(meshtastic_StoreForwardPlusPlus *t,
                                                                                      bool recalc)
{
    // TODO: If not stratum0, injest the chain count
    link_object lo;

    lo.to = t->encapsulated_to;
    if (lo.to == 0) {
        lo.to = NODENUM_BROADCAST;
    }
    lo.from = t->encapsulated_from;
    lo.id = t->encapsulated_id;
    lo.rx_time = t->encapsulated_rxtime;

    // What if we don't have this root hash? Should drop this packet before this point.
    lo.channel_hash = getChannelHashFromRoot(t->root_hash.bytes, t->root_hash.size);
    SHA256 message_hash;

    memcpy(lo.encrypted_bytes, t->message.bytes, t->message.size);
    lo.encrypted_len = t->message.size;
    if (recalc) {
        message_hash.reset();
        message_hash.update(lo.encrypted_bytes, lo.encrypted_len);
        message_hash.update(&lo.to, sizeof(lo.to));
        message_hash.update(&lo.from, sizeof(lo.from));
        message_hash.update(&lo.id, sizeof(lo.id));
        message_hash.finalize(lo.message_hash, SFPP_HASH_SIZE);
        lo.message_hash_len = SFPP_HASH_SIZE;

        // look up full root hash and copy over the partial if it matches
        if (lookUpFullRootHash(t->root_hash.bytes, t->root_hash.size, lo.root_hash)) {
            printBytes("Found full root hash: 0x", lo.root_hash, SFPP_HASH_SIZE);
            lo.root_hash_len = SFPP_HASH_SIZE;
        } else {
            LOG_WARN("root hash does not match %d bytes", t->root_hash.size);
            lo.root_hash_len = 0;
            lo.validObject = false;
            return lo;
        }

        if (t->commit_hash.size == SFPP_HASH_SIZE && getChainCount(t->root_hash.bytes, t->root_hash.size) == 0 &&
            portduino_config.sfpp_initial_sync != 0 && !portduino_config.sfpp_stratum0) {
            lo.commit_hash_len = SFPP_HASH_SIZE;
            memcpy(lo.commit_hash, t->commit_hash.bytes, SFPP_HASH_SIZE);

        } else if (t->commit_hash.size > 0) {
            // calculate the full commit hash and replace the partial if it matches
            if (checkCommitHash(lo, t->commit_hash.bytes, t->commit_hash.size)) {
                printBytes("commit hash matches: 0x", t->commit_hash.bytes, t->commit_hash.size);
            } else {
                LOG_WARN("commit hash does not match, rejecting link.");
                lo.commit_hash_len = 0;
                lo.validObject = false;
            }
        }
    } else {
        memcpy(lo.message_hash, t->message_hash.bytes, t->message_hash.size);
        lo.message_hash_len = t->message_hash.size;
        memcpy(lo.root_hash, t->root_hash.bytes, t->root_hash.size);
        lo.root_hash_len = t->root_hash.size;
        memcpy(lo.commit_hash, t->commit_hash.bytes, t->commit_hash.size);
        lo.commit_hash_len = t->commit_hash.size;
    }

    // we don't ever get the payload here, so it's always an empty string
    lo.payload = "";
    lo.validObject = true;

    return lo;
}

void StoreForwardPlusPlusModule::rebroadcastLinkObject(link_object &lo)
{
    LOG_INFO("Attempting to Rebroadcast a message received over SF++");
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
    service->sendToMesh(p, RX_SRC_RADIO, true);                                       // Send to mesh, cc to phone
}

bool StoreForwardPlusPlusModule::checkCommitHash(StoreForwardPlusPlusModule::link_object &lo, uint8_t *commit_hash_bytes,
                                                 size_t hash_len)
{
    SHA256 commit_hash;

    link_object chain_end = getLinkFromCount(0, lo.root_hash, lo.root_hash_len);

    commit_hash.reset();

    if (chain_end.commit_hash_len == SFPP_HASH_SIZE) {
        printBytes("last message: 0x", chain_end.commit_hash, SFPP_HASH_SIZE);
        commit_hash.update(chain_end.commit_hash, SFPP_HASH_SIZE);
    } else {
        if (lo.root_hash_len != SFPP_HASH_SIZE) {
            LOG_ERROR("Short root hash in link object, cannot create new chain");
            return false;
        }
        printBytes("new chain root: 0x", lo.root_hash, SFPP_HASH_SIZE);
        commit_hash.update(lo.root_hash, SFPP_HASH_SIZE);
    }

    commit_hash.update(lo.message_hash, SFPP_HASH_SIZE);
    commit_hash.finalize(lo.commit_hash, SFPP_HASH_SIZE);
    lo.commit_hash_len = SFPP_HASH_SIZE;

    if (hash_len == 0 || memcmp(commit_hash_bytes, lo.commit_hash, hash_len) == 0) {
        return true;
    }
    return false;
}

bool StoreForwardPlusPlusModule::lookUpFullRootHash(uint8_t *partial_root_hash, size_t partial_root_hash_len,
                                                    uint8_t *full_root_hash)
{
    printBytes("partial_root_hash", partial_root_hash, partial_root_hash_len);
    sqlite3_bind_int(getFullRootHashStmt, 1, partial_root_hash_len);
    sqlite3_bind_blob(getFullRootHashStmt, 2, partial_root_hash, partial_root_hash_len, NULL);
    sqlite3_step(getFullRootHashStmt);
    uint8_t *tmp_root_hash = (uint8_t *)sqlite3_column_blob(getFullRootHashStmt, 0);
    if (tmp_root_hash) {
        LOG_DEBUG("Found full root hash!");
        memcpy(full_root_hash, tmp_root_hash, SFPP_HASH_SIZE);
        sqlite3_reset(getFullRootHashStmt);
        return true;
    }
    sqlite3_reset(getFullRootHashStmt);
    return false;
}

void StoreForwardPlusPlusModule::setChainCount(uint8_t *root_hash, size_t root_hash_len, uint32_t count)
{
    sqlite3_bind_int(setChainCountStmt, 1, root_hash_len);
    sqlite3_bind_blob(setChainCountStmt, 2, root_hash, root_hash_len, NULL);
    sqlite3_bind_int(setChainCountStmt, 3, count);
    sqlite3_step(setChainCountStmt);
    sqlite3_reset(setChainCountStmt);
}

uint32_t StoreForwardPlusPlusModule::getChainCount(uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_bind_int(getChainCountStmt, 1, root_hash_len);
    sqlite3_bind_blob(getChainCountStmt, 2, root_hash, root_hash_len, NULL);

    int res = sqlite3_step(getChainCountStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE && res != SQLITE_ROW) {
        LOG_ERROR("getChainCount sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    uint32_t count = sqlite3_column_int(getChainCountStmt, 0);
    sqlite3_reset(getChainCountStmt);
    return count;
}

StoreForwardPlusPlusModule::link_object StoreForwardPlusPlusModule::getLinkFromCount(uint32_t _count, uint8_t *_root_hash,
                                                                                     size_t _root_hash_len)
{
    link_object lo;
    int step = 0;
    uint32_t _rx_time = 0;

    uint8_t last_message_commit_hash[SFPP_HASH_SIZE] = {0};
    uint8_t last_message_hash[SFPP_HASH_SIZE] = {0};

    sqlite3_bind_int(getChainEndStmt, 1, _root_hash_len);
    sqlite3_bind_blob(getChainEndStmt, 2, _root_hash, _root_hash_len, NULL);
    // this needs to handle a count of 0, indicating the latest
    while (sqlite3_step(getChainEndStmt) == SQLITE_ROW) {
        // get the data from the row while it is still valid
        uint8_t *last_message_commit_hash_ptr = (uint8_t *)sqlite3_column_blob(getChainEndStmt, 0);
        uint8_t *last_message_hash_ptr = (uint8_t *)sqlite3_column_blob(getChainEndStmt, 1);
        _rx_time = sqlite3_column_int(getChainEndStmt, 2);
        memcpy(last_message_commit_hash, last_message_commit_hash_ptr, SFPP_HASH_SIZE);
        memcpy(last_message_hash, last_message_hash_ptr, SFPP_HASH_SIZE);
        if (_count == step)
            break;

        step++;
    }
    if (last_message_commit_hash != nullptr && _rx_time != 0) {
        lo = getLink(last_message_commit_hash, SFPP_HASH_SIZE);
    } else {
        LOG_WARN("Failed to get link from count");
        lo.validObject = false;
    }

    sqlite3_reset(getChainEndStmt);
    return lo;
}

void StoreForwardPlusPlusModule::pruneScratchQueue()
{
    sqlite3_bind_int(pruneScratchQueueStmt, 1, time(nullptr) - 60 * 60 * 24);
    int res = sqlite3_step(pruneScratchQueueStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("Prune Scratch sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(pruneScratchQueueStmt);
}

void StoreForwardPlusPlusModule::trimOldestLink(uint8_t *root_hash, size_t root_hash_len)
{
    sqlite3_bind_int(trimOldestLinkStmt, 1, root_hash_len);
    sqlite3_bind_blob(trimOldestLinkStmt, 2, root_hash, root_hash_len, NULL);
    int res = sqlite3_step(trimOldestLinkStmt);
    if (res != SQLITE_OK && res != SQLITE_DONE) {
        LOG_ERROR("Trim Oldest Link sqlite error %u, %s", res, sqlite3_errmsg(ppDb));
    }
    sqlite3_reset(trimOldestLinkStmt);
}

#endif // has include sqlite3