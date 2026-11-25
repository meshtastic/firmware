/*
 * Native coverage for arm C, the detector + short-ID enumeration repair path in
 * src/modules/Native/StoreForwardPlusPlus.cpp and the exchange helpers in SketchWire/SfppFrame.
 *
 * Two in-process archives share one sqlite root and counter space: the authority is stratum-0 and
 * ingests texts; the follower is built from a copy of the authority's store with rows removed, so
 * the surviving counters match. The test routes the captured packets between the two module
 * instances, which is the on-air exchange without a radio.
 *
 * What is pinned:
 *   - a matching advert sends nothing (d = 0);
 *   - a follower missing d = 1, 4, 16, a whole bucket or three middle rows converges through
 *     ENUM_REQUEST -> ENUM_PROVIDE -> ITEM_REQUEST -> ITEM_FRAME, with the exact message counts;
 *   - a short-ID collision (two objects, one short ID) is resolved by requesting the shared IDs and
 *     serving every stored row for them;
 *   - a collision that cannot resolve is probed once, logged, and suppressed until the advert
 *     changes, instead of re-running forever.
 *
 * The regression guarded: with the walk off, enumeration is the only repair path. If the advert,
 * the list exchange, the by-short-ID object lookup or the in-bucket placement stops working, an
 * archive silently never catches up while every other suite still passes.
 */

#include "configuration.h" // first: the portduino runner links setup()/loop()

#include "TestUtil.h"
#include "UptimeClock.h"
#include "gps/RTC.h"
#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/Router.h"
#include "modules/Native/SfppFrame.h"
#include "modules/Native/SketchIndex.h"
#include "modules/Native/SketchWire.h"
#include "modules/Native/StoreForwardPlusPlus.h"
#include "modules/RoutingModule.h"
#include "support/MockMeshService.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sqlite3.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unity.h>
#include <vector>

#if ARCH_PORTDUINO && !ARCH_PORTDUINO_WASM && !MESHTASTIC_EXCLUDE_SFPP

namespace
{
constexpr NodeNum AUTHORITY_NODE = 0x11112222;
constexpr NodeNum FOLLOWER_NODE = 0x33334444;
constexpr NodeNum FOLLOWER2_NODE = 0x44445555;
constexpr NodeNum SENDER_NODE = 0x55556666;
constexpr NodeNum UNKNOWN_NODE = 0x88889999;
constexpr PacketId BASE_ID = 0x1000;

class TestNodeDB : public NodeDB
{
};

class TestRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 0;
    }
};

class CapturingRouter : public Router
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        if (rejectNext > 0) {
            rejectNext--;
            rejected++;
            packetPool.release(p);
            return ERRNO_NO_INTERFACES;
        }
        sent.push_back(*p);
        if (freeOnSend) {
            // A real radio takes ownership and may free the packet during send.
            packetPool.release(p);
            return ERRNO_OK;
        }
        owned.push_back(p);
        return ERRNO_OK;
    }

    std::vector<meshtastic_MeshPacket> sent;
    std::vector<meshtastic_MeshPacket *> owned;
    int rejectNext = 0;
    uint32_t rejected = 0;
    bool freeOnSend = false;
};

class TestRoutingModule : public RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit = 0,
                    bool ackWantsAck = false, const meshtastic_MeshPacket *relaySource = nullptr) override
    {
        (void)err;
        (void)to;
        (void)idFrom;
        (void)chIndex;
        (void)hopLimit;
        (void)ackWantsAck;
        (void)relaySource;
    }

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        (void)p;
        return false;
    }
};

class TestSfppModule : public StoreForwardPlusPlusModule
{
  public:
    using StoreForwardPlusPlusModule::handleReceived;
    using StoreForwardPlusPlusModule::handleReceivedProtobuf;
    using StoreForwardPlusPlusModule::runOnce;

    ProcessMessage ingest(const meshtastic_MeshPacket &mp, const meshtastic_MeshPacket *encrypted)
    {
        currentEncrypted = encrypted;
        ProcessMessage result = handleReceived(mp);
        currentEncrypted = nullptr;
        return result;
    }
};

TestNodeDB *testNodeDB;
CapturingRouter *testRouter;
MockMeshService *testService;
TestRoutingModule *testRoutingModule;
TestSfppModule *authority;
TestSfppModule *follower;
TestSfppModule *follower2;
std::string dirA;
std::string dirB;
std::string dirC;

std::string dbFile(const std::string &dir)
{
    return dir + "/storeforwardpp.db";
}

meshtastic_MeshPacket makeText(NodeNum from, PacketId id, const char *text, uint32_t rxTime)
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.to = NODENUM_BROADCAST;
    mp.id = id;
    mp.channel = 0;
    mp.rx_time = rxTime;
    mp.hop_limit = 3;
    mp.hop_start = 3;
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t len = strlen(text);
    memcpy(mp.decoded.payload.bytes, text, len);
    mp.decoded.payload.size = len;
    return mp;
}

meshtastic_MeshPacket makeEncryptedCopy(const meshtastic_MeshPacket &mp)
{
    uint8_t plain[256];
    const size_t len = pb_encode_to_bytes(plain, sizeof(plain), &meshtastic_Data_msg, &mp.decoded);
    crypto->setKey(channels.getKey(0));
    crypto->encryptPacket(mp.from, mp.id, len, plain);

    meshtastic_MeshPacket enc = meshtastic_MeshPacket_init_zero;
    enc.from = mp.from;
    enc.to = mp.to;
    enc.id = mp.id;
    enc.channel = channels.getHash(0);
    enc.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    enc.encrypted.size = len;
    memcpy(enc.encrypted.bytes, plain, len);
    return enc;
}

meshtastic_MeshPacket ingestText(TestSfppModule *module, NodeNum from, PacketId id, const char *text)
{
    meshtastic_MeshPacket mp = makeText(from, id, text, getValidTime(RTCQualityNTP, true));
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, module->ingest(mp, &enc));
    return mp;
}

bool openDb(const std::string &dir, sqlite3 **db)
{
    return sqlite3_open(dbFile(dir).c_str(), db) == SQLITE_OK;
}

bool copyDb(const std::string &fromDir, const std::string &toDir)
{
    sqlite3 *src = nullptr;
    sqlite3 *dst = nullptr;
    bool ok = openDb(fromDir, &src) && openDb(toDir, &dst);
    if (ok) {
        sqlite3_backup *backup = sqlite3_backup_init(dst, "main", src, "main");
        ok = backup && sqlite3_backup_step(backup, -1) == SQLITE_DONE;
        if (backup)
            sqlite3_backup_finish(backup);
    }
    if (dst)
        sqlite3_close(dst);
    if (src)
        sqlite3_close(src);
    return ok;
}

bool execSql(const std::string &dir, const char *sql)
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    const bool ok = sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

bool deleteCounter(const std::string &dir, uint32_t counter)
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db, "delete from channel_messages where counter=?;", -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(stmt, 1, (int)counter);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool deleteCounterRange(const std::string &dir, uint32_t first, uint32_t last)
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok =
        sqlite3_prepare_v2(db, "delete from channel_messages where counter>=? and counter<=?;", -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(stmt, 1, (int)first);
        sqlite3_bind_int(stmt, 2, (int)last);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool readRoot(const std::string &dir, uint8_t out[SFPP_HASH_SIZE]);

struct CollisionCandidate {
    uint8_t cipher[16] = {0};
    uint8_t hash[SFPP_HASH_SIZE] = {0};
    uint32_t shortId = 0;
    uint32_t id = 0;
};

void makeCandidate(const uint8_t cipher[16], uint32_t id, CollisionCandidate &out)
{
    memcpy(out.cipher, cipher, sizeof(out.cipher));
    out.id = id;
    sfppframe::messageHash(out.cipher, sizeof(out.cipher), NODENUM_BROADCAST, SENDER_NODE, out.id, out.hash);
    out.shortId = sfppsr::shortId(out.hash, sizeof(out.hash));
}

// A real pair sharing a truncated short ID, computed once offline and checked in: the reassembler
// recomputes the short ID from the ciphertext, so a forced column could not survive the trip.
// Both hash to shortId 0x0985bdf3.
const uint8_t kCollisionACipher[16] = {0xc0, 0x5f, 0x32, 0x33, 0xc4, 0x5b, 0x36, 0x37,
                                       0xc8, 0x57, 0x3a, 0x3b, 0xcc, 0x53, 0x3e, 0x3f};
const uint8_t kCollisionBCipher[16] = {0x01, 0x49, 0x32, 0x33, 0x05, 0x4d, 0x36, 0x37,
                                       0x09, 0x41, 0x3a, 0x3b, 0x0d, 0x45, 0x3e, 0x3f};
constexpr uint32_t kCollisionAId = 0x0000bef0;
constexpr uint32_t kCollisionBId = 0x0000c831;

bool insertCandidateRow(const std::string &dir, uint32_t counter, const CollisionCandidate &candidate)
{
    uint8_t root[SFPP_HASH_SIZE];
    if (!readRoot(dir, root))
        return false;

    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db,
                                 "insert into channel_messages (destination, sender, packet_id, root_hash, encrypted_bytes, "
                                 "message_hash, commit_hash, payload, rx_time, counter, short_id, checksum) "
                                 "values (?,?,?,?,?,?,?,?,?,?,?,?);",
                                 -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)NODENUM_BROADCAST);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)SENDER_NODE);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)candidate.id);
        sqlite3_bind_blob(stmt, 4, root, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 5, candidate.cipher, sizeof(candidate.cipher), SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 6, candidate.hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 7, candidate.hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, "", 0, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 9, (sqlite3_int64)getValidTime(RTCQualityNTP, true));
        sqlite3_bind_int(stmt, 10, (int)counter);
        sqlite3_bind_int64(stmt, 11, (sqlite3_int64)candidate.shortId);
        sqlite3_bind_int64(stmt, 12, (sqlite3_int64)sfppsr::checksumContribution(candidate.hash, sizeof(candidate.hash)));
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool readRoot(const std::string &dir, uint8_t out[SFPP_HASH_SIZE])
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db, "select root_hash from channel_messages limit 1;", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t *root = (const uint8_t *)sqlite3_column_blob(stmt, 0);
        const int len = sqlite3_column_bytes(stmt, 0);
        if (root && len == (int)SFPP_HASH_SIZE) {
            memcpy(out, root, SFPP_HASH_SIZE);
            ok = true;
        }
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

// The root mapped to the primary channel, whether or not the store holds any rows yet.
bool readMappingRoot(const std::string &dir, uint8_t out[SFPP_HASH_SIZE])
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db, "select root_hash from mappings limit 1;", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t *root = (const uint8_t *)sqlite3_column_blob(stmt, 0);
        const int len = sqlite3_column_bytes(stmt, 0);
        if (root && len == (int)SFPP_HASH_SIZE) {
            memcpy(out, root, SFPP_HASH_SIZE);
            ok = true;
        }
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

// A fabricated extra row in the bucket with a chosen short ID, used to prove an unrecoverable
// collision is detected rather than retried forever.
bool insertScratchRow(const std::string &dir, uint32_t to, uint32_t from, uint32_t id, const uint8_t *cipher, size_t len,
                      const uint8_t *hash)
{
    uint8_t root[SFPP_HASH_SIZE];
    if (!readRoot(dir, root))
        return false;
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db,
                                 "insert into local_messages (destination, sender, packet_id, root_hash, encrypted_bytes, "
                                 "message_hash, payload, rx_time) values (?,?,?,?,?,?,?,?);",
                                 -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)to);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)from);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)id);
        sqlite3_bind_blob(stmt, 4, root, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 5, cipher, (int)len, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 6, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, "", 0, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 8, (sqlite3_int64)getValidTime(RTCQualityNTP, true));
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool insertExtraRow(const std::string &dir, uint32_t counter, uint32_t shortId)
{
    uint8_t root[SFPP_HASH_SIZE];
    if (!readRoot(dir, root))
        return false;

    uint8_t hash[SFPP_HASH_SIZE];
    for (size_t i = 0; i < sizeof(hash); i++)
        hash[i] = (uint8_t)(0xE0 + i);
    const uint8_t cipher[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db,
                                 "insert into channel_messages (destination, sender, packet_id, root_hash, encrypted_bytes, "
                                 "message_hash, commit_hash, payload, rx_time, counter, short_id, checksum) "
                                 "values (?,?,?,?,?,?,?,?,?,?,?,?);",
                                 -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)NODENUM_BROADCAST);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)SENDER_NODE);
        sqlite3_bind_int64(stmt, 3, 0x7777);
        sqlite3_bind_blob(stmt, 4, root, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 5, cipher, sizeof(cipher), SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 6, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 7, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, "", 0, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 9, (sqlite3_int64)getValidTime(RTCQualityNTP, true));
        sqlite3_bind_int(stmt, 10, (int)counter);
        sqlite3_bind_int64(stmt, 11, (sqlite3_int64)shortId);
        sqlite3_bind_int64(stmt, 12, (sqlite3_int64)sfppsr::checksumContribution(hash, sizeof(hash)));
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

struct BucketView {
    uint32_t count = 0;
    uint64_t checksum = 0;
};

BucketView readBucket(const std::string &dir, uint32_t first, uint32_t last)
{
    BucketView view;
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return view;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db,
                           "select short_id, checksum from channel_messages where counter>=? and counter<=? and short_id is not "
                           "null;",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, (int)first);
        sqlite3_bind_int(stmt, 2, (int)last);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            view.count++;
            view.checksum ^= (uint64_t)sqlite3_column_int64(stmt, 1);
        }
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return view;
}

// Builds an advert from a directory's stored bucket, optionally with a corrupted checksum, so a
// test can force the sketch decode to be contradicted.
bool buildAdvertFromDir(const std::string &dir, uint32_t bucket, bool corruptChecksum, meshtastic_MeshPacket &out,
                        bool fullRoot = false, uint32_t tip = 0)
{
    uint8_t root[SFPP_HASH_SIZE];
    if (!readRoot(dir, root))
        return false;

    sfppsr::BucketSummary summary(4);
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    const uint32_t first = bucket * sfppsr::kBucketObjects + 1;
    const uint32_t last = first + sfppsr::kBucketObjects - 1;
    if (sqlite3_prepare_v2(db,
                           "select short_id, checksum from channel_messages where counter>=? and counter<=? and short_id is not "
                           "null;",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, (int)first);
        sqlite3_bind_int(stmt, 2, (int)last);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            summary.add((pinsketch::Element)sqlite3_column_int64(stmt, 0), (uint64_t)sqlite3_column_int64(stmt, 1));
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);

    sfppsr::Scope scope;
    scope.rootPrefixLen = fullRoot ? SFPP_HASH_SIZE : 4;
    memcpy(scope.rootPrefix, root, scope.rootPrefixLen);
    scope.bucket = bucket;

    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildAdvert(scope, summary, msg))
        return false;
    if (corruptChecksum)
        msg.sr_checksum ^= 1;
    msg.encapsulated_rxtime = tip; // Stage 8f: the sender's tip counter

    out = meshtastic_MeshPacket_init_zero;
    out.from = AUTHORITY_NODE;
    out.to = NODENUM_BROADCAST;
    out.channel = 0;
    out.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    out.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    out.decoded.payload.size = pb_encode_to_bytes(out.decoded.payload.bytes, sizeof(out.decoded.payload.bytes),
                                                  &meshtastic_StoreForwardPlusPlus_msg, &msg);
    return out.decoded.payload.size > 0;
}

// A full-root advert naming an arbitrary root and a non-empty bucket, used to prove a foreign root
// is ignored and our own is accepted.
bool buildAdvertWithRoot(const uint8_t root[SFPP_HASH_SIZE], uint32_t bucket, uint32_t tip, meshtastic_MeshPacket &out)
{
    sfppsr::BucketSummary summary(4);
    summary.add(0x1234, 0x5678);

    sfppsr::Scope scope;
    scope.rootPrefixLen = SFPP_HASH_SIZE;
    memcpy(scope.rootPrefix, root, SFPP_HASH_SIZE);
    scope.bucket = bucket;

    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildAdvert(scope, summary, msg))
        return false;
    msg.encapsulated_rxtime = tip;

    out = meshtastic_MeshPacket_init_zero;
    out.from = AUTHORITY_NODE;
    out.to = NODENUM_BROADCAST;
    out.channel = 0;
    out.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    out.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    out.decoded.payload.size = pb_encode_to_bytes(out.decoded.payload.bytes, sizeof(out.decoded.payload.bytes),
                                                  &meshtastic_StoreForwardPlusPlus_msg, &msg);
    return out.decoded.payload.size > 0;
}

// A one-frame provide built straight from the store: the authority's paced drain advances the
// clock, which would hide a pacer regression when the same advert is delivered twice.
meshtastic_MeshPacket buildProvideFromDir(const std::string &dir, uint32_t bucket)
{
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dir, root));

    std::vector<pinsketch::Element> ids;
    sqlite3 *db = nullptr;
    TEST_ASSERT_TRUE(openDb(dir, &db));
    sqlite3_stmt *stmt = nullptr;
    const uint32_t first = bucket * sfppsr::kBucketObjects + 1;
    const uint32_t last = first + sfppsr::kBucketObjects - 1;
    TEST_ASSERT_TRUE(sqlite3_prepare_v2(db,
                                        "select short_id from channel_messages where counter>=? and counter<=? and short_id is "
                                        "not null order by short_id;",
                                        -1, &stmt, nullptr) == SQLITE_OK);
    sqlite3_bind_int(stmt, 1, (int)first);
    sqlite3_bind_int(stmt, 2, (int)last);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        ids.push_back((pinsketch::Element)sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    sfppsr::Scope scope;
    scope.rootPrefixLen = 4;
    memcpy(scope.rootPrefix, root, scope.rootPrefixLen);
    scope.bucket = bucket;
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildEnumProvide(scope, ids, 0, 1, msg));

    meshtastic_MeshPacket out = meshtastic_MeshPacket_init_zero;
    out.from = AUTHORITY_NODE;
    out.to = FOLLOWER_NODE;
    out.channel = 0;
    out.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    out.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    out.decoded.payload.size = pb_encode_to_bytes(out.decoded.payload.bytes, sizeof(out.decoded.payload.bytes),
                                                  &meshtastic_StoreForwardPlusPlus_msg, &msg);
    return out;
}

bool readShortIds(const std::string &dir, uint32_t bucket, std::vector<pinsketch::Element> &out)
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    const uint32_t first = bucket * sfppsr::kBucketObjects + 1;
    const uint32_t last = first + sfppsr::kBucketObjects - 1;
    bool ok = sqlite3_prepare_v2(db,
                                 "select short_id from channel_messages where counter>=? and counter<=? and short_id is not "
                                 "null order by counter;",
                                 -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(stmt, 1, (int)first);
        sqlite3_bind_int(stmt, 2, (int)last);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            out.push_back((pinsketch::Element)sqlite3_column_int64(stmt, 0));
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

uint32_t countRows(const std::string &dir, PacketId id)
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return 0;
    sqlite3_stmt *stmt = nullptr;
    uint32_t count = 0;
    if (sqlite3_prepare_v2(db, "select count(*) from channel_messages where packet_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = (uint32_t)sqlite3_column_int64(stmt, 0);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

std::vector<meshtastic_MeshPacket> takeSent()
{
    std::vector<meshtastic_MeshPacket> out = testRouter->sent;
    testRouter->sent.clear();
    for (meshtastic_MeshPacket *p : testRouter->owned)
        packetPool.release(p);
    testRouter->owned.clear();
    return out;
}

uint32_t drainPhoneQueueCount()
{
    uint32_t count = 0;
    while (meshtastic_MeshPacket *p = testService->getForPhone()) {
        testService->releaseToPool(p);
        count++;
    }
    return count;
}

void ingestAuthority(uint32_t count)
{
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    portduino_config.sfpp_stratum0 = true;
    for (uint32_t i = 0; i < count; i++) {
        char text[16];
        snprintf(text, sizeof(text), "msg %u", i + 1);
        ingestText(authority, SENDER_NODE, BASE_ID + i, text);
    }
}

void ingestAuthorityMore(uint32_t start, uint32_t count)
{
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    portduino_config.sfpp_stratum0 = true;
    for (uint32_t i = start; i < start + count; i++) {
        char text[16];
        snprintf(text, sizeof(text), "msg %u", i + 1);
        ingestText(authority, SENDER_NODE, BASE_ID + i, text);
    }
}

void buildFollowerFromCopy()
{
    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    follower = new TestSfppModule();
}

void buildSecondFollowerFromCopy()
{
    TEST_ASSERT_TRUE(copyDb(dirA, dirC));
    portduino_config.sfpp_db_path = dirC + "/";
    portduino_config.sfpp_stratum0 = false;
    follower2 = new TestSfppModule();
}

bool deliver(TestSfppModule *target, NodeNum targetNode, bool stratum0, const meshtastic_MeshPacket &p)
{
    myNodeInfo.my_node_num = targetNode;
    portduino_config.sfpp_stratum0 = stratum0;
    meshtastic_StoreForwardPlusPlus msg;
    if (!pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_StoreForwardPlusPlus_msg, &msg))
        return false;
    return target->handleReceivedProtobuf(p, &msg);
}

meshtastic_StoreForwardPlusPlus decodeSfpp(const meshtastic_MeshPacket &p)
{
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_zero;
    pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_StoreForwardPlusPlus_msg, &msg);
    return msg;
}

meshtastic_MeshPacket runAuthorityOnce()
{
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    portduino_config.sfpp_stratum0 = true;
    testRouter->sent.clear();
    TEST_ASSERT_GREATER_THAN(0, authority->runOnce());
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT, decodeSfpp(sent[0]).sfpp_message_type);
    return sent[0];
}

// Runs one full pull from the advert to the last item frame. Returns the number of item frames the
// authority sent and leaves the follower's sends in the returned vectors for count assertions.
struct Exchange {
    std::vector<meshtastic_MeshPacket> followerSends;
    uint32_t enumRequests = 0;
    uint32_t itemRequests = 0;
    uint32_t itemFrames = 0;
    uint32_t maxFramesPerDrain = 0;
};

std::vector<meshtastic_MeshPacket> drainAuthorityOne(uint32_t advanceMs);

Exchange runExchangeFor(TestSfppModule *requester, NodeNum node)
{
    Exchange result;
    meshtastic_MeshPacket advert = runAuthorityOnce();

    TEST_ASSERT_TRUE(deliver(requester, node, false, advert));
    result.followerSends = takeSent();
    if (result.followerSends.empty())
        return result; // in sync, nothing to pull

    TEST_ASSERT_EQUAL_UINT(1, result.followerSends.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST,
                      decodeSfpp(result.followerSends[0]).sfpp_message_type);
    result.enumRequests = (uint32_t)result.followerSends.size();

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, result.followerSends[0]));
    std::vector<meshtastic_MeshPacket> provides = drainAuthorityOne(2000); // N6: the provide is queued too
    TEST_ASSERT_EQUAL_UINT(1, provides.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provides[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(requester, node, false, provides[0]));
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    for (const auto &request : requests)
        if (decodeSfpp(request).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            result.itemRequests++;
    TEST_ASSERT_EQUAL_UINT(1, result.itemRequests);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, requests[0]));
    // The authority queues the item frames and answers runOnce one frame at a time, so a large
    // request never fills the radio's 16-entry TX queue.
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    for (uint32_t guard = 0; guard < 512; guard++) {
        Time::setTestMillis(Time::getMillis() + 2000); // one drain interval
        myNodeInfo.my_node_num = AUTHORITY_NODE;
        portduino_config.sfpp_stratum0 = true;
        testRouter->sent.clear();
        authority->runOnce();
        std::vector<meshtastic_MeshPacket> sent = takeSent();
        if (sent.empty())
            continue;
        if (decodeSfpp(sent[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break; // the queue drained and the advert cadence resumed
        if (sent.size() > result.maxFramesPerDrain)
            result.maxFramesPerDrain = (uint32_t)sent.size();
        for (const auto &frame : sent) {
            TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME, decodeSfpp(frame).sfpp_message_type);
            TEST_ASSERT_TRUE(deliver(requester, node, false, frame));
            result.itemFrames++;
        }
    }
    return result;
}

Exchange runExchange()
{
    return runExchangeFor(follower, FOLLOWER_NODE);
}

void assertConverged()
{
    const BucketView a = readBucket(dirA, 1, sfppsr::kBucketObjects);
    const BucketView b = readBucket(dirB, 1, sfppsr::kBucketObjects);
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, b.checksum);
}

// Runs the exchange up to (and including) the authority accepting an ITEM_REQUEST, which queues
// the item frames without sending them.
void driveToItemRequest(TestSfppModule *requester, NodeNum requesterNode)
{
    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(requester, requesterNode, false, advert));
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, requests[0]));

    std::vector<meshtastic_MeshPacket> provides = drainAuthorityOne(2000); // N6: the provide is queued
    TEST_ASSERT_EQUAL_UINT(1, provides.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provides[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(requester, requesterNode, false, provides[0]));

    std::vector<meshtastic_MeshPacket> wanted = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, wanted.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, wanted[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // queued, not sent in one burst
}

std::vector<meshtastic_MeshPacket> drainAuthorityOne(uint32_t advanceMs)
{
    Time::setTestMillis(Time::getMillis() + advanceMs);
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    portduino_config.sfpp_stratum0 = true;
    testRouter->sent.clear();
    authority->runOnce();
    return takeSent();
}

} // namespace

void ingestScratchText(TestSfppModule *module, NodeNum node, NodeNum from, PacketId id, const char *text, uint32_t ageS);

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    channelFile = meshtastic_ChannelFile_init_zero;
    owner = meshtastic_User_init_zero;

    testNodeDB = new TestNodeDB();
    nodeDB = testNodeDB;
    myNodeInfo.my_node_num = AUTHORITY_NODE;

    channels.initDefaults();
    channels.onConfigChanged();

    testService = new MockMeshService();
    service = testService;

    testRouter = new CapturingRouter();
    testRouter->addInterface(std::unique_ptr<RadioInterface>(new TestRadio()));
    router = testRouter;

    testRoutingModule = new TestRoutingModule();
    routingModule = testRoutingModule;

    char tmplA[] = "/tmp/meshtastic-sfpp-enum-a-XXXXXX";
    char tmplB[] = "/tmp/meshtastic-sfpp-enum-b-XXXXXX";
    char tmplC[] = "/tmp/meshtastic-sfpp-enum-c-XXXXXX";
    char *a = mkdtemp(tmplA);
    char *b = mkdtemp(tmplB);
    char *c = mkdtemp(tmplC);
    dirA = a ? a : "/tmp";
    dirB = b ? b : "/tmp";
    dirC = c ? c : "/tmp";

    portduino_config.sfpp_enabled = true;
    portduino_config.sfpp_stratum0 = true;
    portduino_config.sfpp_hops = 3;
    portduino_config.sfpp_steal_port = false;
    portduino_config.sfpp_announce_interval = 5;
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    portduino_config.sfpp_enum_interval = 1;
    portduino_config.sfpp_enum_drain_ms = 2000;
    portduino_config.sfpp_scratch_sweep = 5;
    portduino_config.sfpp_item_transport = SFPP_ITEM_DIRECT; // existing cases drive direct exchanges
    portduino_config.sfpp_enum_listen_only = false;
    portduino_config.sfpp_pull_retry = false;
    portduino_config.sfpp_db_path = dirA + "/";

    Time::setTestMillis(1000);

    authority = new TestSfppModule();
    follower = nullptr;
    follower2 = nullptr;
}

void tearDown(void)
{
    delete follower2;
    follower2 = nullptr;
    delete follower;
    follower = nullptr;
    delete authority;
    authority = nullptr;

    for (const std::string &dir : {dirA, dirB, dirC}) {
        if (dir.empty() || dir == "/tmp")
            continue;
        std::remove(dbFile(dir).c_str());
        std::remove((dbFile(dir) + "-journal").c_str());
        rmdir(dir.c_str());
    }

    delete testRoutingModule;
    testRoutingModule = nullptr;
    routingModule = nullptr;

    while (meshtastic_MeshPacket *p = testService->getForPhone())
        testService->releaseToPool(p);
    while (meshtastic_QueueStatus *q = testService->getQueueStatusForPhone())
        testService->releaseQueueStatusToPool(q);
    for (meshtastic_MeshPacket *p : testRouter->owned)
        packetPool.release(p);
    testRouter->owned.clear();

    delete testRouter;
    testRouter = nullptr;
    router = nullptr;

    delete testService;
    testService = nullptr;
    service = nullptr;

    delete testNodeDB;
    testNodeDB = nullptr;
    nodeDB = nullptr;

    Time::useRealClock();
}

// d = 0: the follower already agrees, so the advert triggers no exchange at all.
void test_matchingAdvertSendsNothing()
{
    ingestAuthority(20);
    buildFollowerFromCopy();

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

void runTailCase(uint32_t total, uint32_t missing)
{
    ingestAuthority(total);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, total - missing + 1, total));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.enumRequests);
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);
    TEST_ASSERT_EQUAL_UINT(missing, exchange.itemFrames);
    // Pacing: a request for many objects is answered one frame per drain, never in one burst that
    // would overflow the 16-entry TX queue.
    TEST_ASSERT_TRUE(exchange.maxFramesPerDrain <= 1);
    assertConverged();

    // A second identical advert is a no-op once converged.
    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// A missing tail of 1, 4 and 16 rows converges, with the exact one-request-per-kind exchange.
void test_missingTail1Converges()
{
    runTailCase(20, 1);
}

void test_missingTail4Converges()
{
    runTailCase(20, 4);
}

void test_missingTail16Converges()
{
    runTailCase(20, 16);
}

// The follower was away while the authority filled a whole new bucket: its local bucket is empty,
// which must classify as count 0 / checksum 0 and pull rather than be skipped.
void test_wholeBucketMissingConverges()
{
    ingestAuthority(8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 1, 8));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(8, exchange.itemFrames);
    assertConverged();
}

// A gap in the middle, not just the tail: rows 5, 9 and 17 missing. Recovered rows land in the
// bucket by range even though their exact counter differs from the authority's.
void test_middleGapConverges()
{
    ingestAuthority(20);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 5));
    TEST_ASSERT_TRUE(deleteCounter(dirB, 9));
    TEST_ASSERT_TRUE(deleteCounter(dirB, 17));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(3, exchange.itemFrames);
    assertConverged();
}

// Two messages really share a truncated short ID. The follower holds one and lacks the other, so
// the short-ID lists match but the checksum differs. The probe requests the shared IDs, the
// authority serves every row for them, and the unknown hash is stored.
void test_shortIdCollisionConverges()
{
    ingestAuthority(1); // establishes the root and a non-colliding filler row
    CollisionCandidate held;
    CollisionCandidate missingObject;
    makeCandidate(kCollisionACipher, kCollisionAId, held);
    makeCandidate(kCollisionBCipher, kCollisionBId, missingObject);
    TEST_ASSERT_EQUAL_UINT32(held.shortId, missingObject.shortId);
    TEST_ASSERT_TRUE(memcmp(held.hash, missingObject.hash, SFPP_HASH_SIZE) != 0);
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, 2, held));
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, 3, missingObject));
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 3)); // the follower holds counter 2, not counter 3

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.enumRequests);
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);             // the probe, not a normal missing request
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, missingObject.id)); // the collided object arrived
    assertConverged();
}

// The follower holds an extra object under the same short ID that the authority does not have.
// The probe cannot make the checksums agree, so it must stop after one attempt and add no traffic
// for the unchanged advert.
void test_unresolvableCollisionIsSuppressed()
{
    ingestAuthority(1);
    CollisionCandidate held;
    CollisionCandidate missingObject;
    makeCandidate(kCollisionACipher, kCollisionAId, held);
    makeCandidate(kCollisionBCipher, kCollisionBId, missingObject);
    TEST_ASSERT_EQUAL_UINT32(held.shortId, missingObject.shortId);
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, 2, held));
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, 3, missingObject));
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 3)); // follower lacks counter 3
    // ... and holds an extra row whose short ID is the same, keeping the lists equal while the
    // checksums cannot converge.
    TEST_ASSERT_TRUE(insertExtraRow(dirB, 4, held.shortId));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);             // one probe
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, missingObject.id)); // the collided object arrived
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, 0x7777));           // the extra row is still there
    TEST_ASSERT_TRUE(readBucket(dirB, 1, sfppsr::kBucketObjects).count != readBucket(dirA, 1, sfppsr::kBucketObjects).count);

    // Let the probe's idle timer expire, then present the unchanged advert again.
    Time::setTestMillis(Time::getMillis() + 31000);
    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// An advert whose scope is not a root this node knows must be dropped without a reply.
void test_unknownScopeAdvertIsDropped()
{
    ingestAuthority(8);
    buildFollowerFromCopy();

    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = SFPP_HASH_SIZE;
    memset(msg.root_hash.bytes, 0x42, SFPP_HASH_SIZE);
    msg.chain_count = 0;
    msg.sr_count = 4;
    msg.sr_checksum = 0x1234;
    msg.message.size = 16; // a capacity-4 sketch of zeros still parses

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = AUTHORITY_NODE;
    mp.to = NODENUM_BROADCAST;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                 &meshtastic_StoreForwardPlusPlus_msg, &msg);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, mp));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// An oversized short-ID list is refused at the wire boundary, so nothing is emitted for it. The
// struct is handed to the handler directly, because encoding a 41-entry list would read past the
// generated 40-entry array; the parser refuses on the count before touching the array.
void test_oversizedItemRequestIsRefused()
{
    ingestAuthority(8);
    buildFollowerFromCopy();

    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = 4;
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));
    memcpy(msg.root_hash.bytes, root, 4);
    msg.chain_count = 0;
    for (pb_size_t i = 0; i < sfppsr::kMaxShortIds; i++)
        msg.sr_short_ids[i] = 0x1000 + i;
    msg.sr_short_ids_count = sfppsr::kMaxShortIds + 1;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = FOLLOWER_NODE;
    mp.to = AUTHORITY_NODE;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    mp.decoded.payload.size = 0;

    myNodeInfo.my_node_num = AUTHORITY_NODE;
    portduino_config.sfpp_stratum0 = true;
    TEST_ASSERT_TRUE(authority->handleReceivedProtobuf(mp, &msg));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// An object request for a short ID that the bucket does not hold serves nothing and stores nothing.
void test_unknownShortIdRequestServesNothing()
{
    ingestAuthority(8);
    buildFollowerFromCopy();

    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = 4;
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));
    memcpy(msg.root_hash.bytes, root, 4);
    msg.chain_count = 0;
    msg.sr_short_ids[0] = 0xDEADBEEF;
    msg.sr_short_ids_count = 1;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = FOLLOWER_NODE;
    mp.to = AUTHORITY_NODE;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                 &meshtastic_StoreForwardPlusPlus_msg, &msg);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, mp));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// --- Stage 8a robustness ---

// G14: the advert carries only the 4-byte root prefix.
void test_advertUsesFourByteRootPrefix()
{
    ingestAuthority(4);
    buildFollowerFromCopy();

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_EQUAL_UINT(4, decodeSfpp(advert).root_hash.size);
}

// G21: a queued frame goes on the next drain tick without waiting out a full interval.
// G21 + N7: the first item frame goes on the next paced drain step, sharing the one last-send
// timestamp with the provide that was just served.
void test_firstItemFrameDrainsOnNextStep()
{
    ingestAuthority(2);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 2));
    driveToItemRequest(follower, FOLLOWER_NODE);

    std::vector<meshtastic_MeshPacket> sent = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME, decodeSfpp(sent[0]).sfpp_message_type);
}

// G12: a rejected send leaves the frame queued and the next drain sends it.
void test_rejectedItemFrameIsRequeued()
{
    ingestAuthority(2);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 2));
    driveToItemRequest(follower, FOLLOWER_NODE);

    testRouter->rejectNext = 1;
    std::vector<meshtastic_MeshPacket> rejected = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, testRouter->rejected);
    TEST_ASSERT_EQUAL_UINT(0, rejected.size());

    std::vector<meshtastic_MeshPacket> retry = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, retry.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, retry[0]));
    assertConverged();
}

// G10: a lost ITEM_FRAME is recovered by the bounded item retry.
void test_lostItemFrameIsRetried()
{
    ingestAuthority(2);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 2));
    driveToItemRequest(follower, FOLLOWER_NODE);

    std::vector<meshtastic_MeshPacket> lost = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, lost.size());
    // Do not deliver it; the follower still wants the object.

    Time::setTestMillis(Time::getMillis() + 31000); // past the retry interval
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    testRouter->sent.clear();
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retries = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retries.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(retries[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, retries[0]));
    std::vector<meshtastic_MeshPacket> frame = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, frame.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame[0]));
    assertConverged();
}

// G11: two requesters each get their frames; the per-requester queues are served round-robin.
void test_twoRequestersBothComplete()
{
    ingestAuthority(4);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 3));
    TEST_ASSERT_TRUE(deleteCounter(dirC, 4));

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> bRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bRequest.size());
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> cRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, cRequest.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, bRequest[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, cRequest[0]));
    // N6: both provides are queued and drained round-robin, one per step.
    std::vector<meshtastic_MeshPacket> bProvide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, bProvide.size());
    TEST_ASSERT_EQUAL(FOLLOWER_NODE, bProvide[0].to);
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(bProvide[0]).sfpp_message_type);
    std::vector<meshtastic_MeshPacket> cProvide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, cProvide.size());
    TEST_ASSERT_EQUAL(FOLLOWER2_NODE, cProvide[0].to);
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(cProvide[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, bProvide[0]));
    std::vector<meshtastic_MeshPacket> bItem = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bItem.size());
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, cProvide[0]));
    std::vector<meshtastic_MeshPacket> cItem = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, cItem.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, bItem[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, cItem[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint32_t bFrames = 0;
    uint32_t cFrames = 0;
    for (uint32_t guard = 0; guard < 8 && (bFrames == 0 || cFrames == 0); guard++) {
        std::vector<meshtastic_MeshPacket> sent = drainAuthorityOne(2000);
        for (const auto &frame : sent) {
            if (decodeSfpp(frame).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                continue; // the advert that follows the drained queues
            if (frame.to == FOLLOWER_NODE) {
                TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
                bFrames++;
            } else if (frame.to == FOLLOWER2_NODE) {
                TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, frame));
                cFrames++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(1, bFrames);
    TEST_ASSERT_EQUAL_UINT(1, cFrames);

    const BucketView a = readBucket(dirA, 1, sfppsr::kBucketObjects);
    const BucketView b = readBucket(dirB, 1, sfppsr::kBucketObjects);
    const BucketView c = readBucket(dirC, 1, sfppsr::kBucketObjects);
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, b.checksum);
    TEST_ASSERT_EQUAL_UINT32(a.count, c.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, c.checksum);
}

// --- Stage 8b: wrong sketch decode ---

// Builds an advert whose sketch is the local bucket XOR a few fake IDs, so it decodes to IDs the
// real authority does not hold. `addCountMismatch` makes the advertised count inconsistent.
bool buildCraftedAdvertFromDir(const std::string &dir, uint32_t bucket, uint32_t fakeCount, bool addCountMismatch,
                               meshtastic_MeshPacket &out)
{
    uint8_t root[SFPP_HASH_SIZE];
    if (!readRoot(dir, root))
        return false;

    sfppsr::BucketSummary summary(4);
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    const uint32_t first = bucket * sfppsr::kBucketObjects + 1;
    const uint32_t last = first + sfppsr::kBucketObjects - 1;
    if (sqlite3_prepare_v2(db,
                           "select short_id, checksum from channel_messages where counter>=? and counter<=? and short_id is not "
                           "null;",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, (int)first);
        sqlite3_bind_int(stmt, 2, (int)last);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            summary.add((pinsketch::Element)sqlite3_column_int64(stmt, 0), (uint64_t)sqlite3_column_int64(stmt, 1));
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);

    for (uint32_t i = 0; i < fakeCount; i++) {
        uint8_t fake[SFPP_HASH_SIZE];
        memset(fake, 0xA0 + (int)i, sizeof(fake));
        summary.add((pinsketch::Element)(0x0BAD0000u + i), sfppsr::checksumContribution(fake, sizeof(fake)));
    }

    sfppsr::Scope scope;
    scope.rootPrefixLen = 4;
    memcpy(scope.rootPrefix, root, 4);
    scope.bucket = bucket;

    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildAdvert(scope, summary, msg))
        return false;
    if (addCountMismatch)
        msg.sr_count += 1;

    out = meshtastic_MeshPacket_init_zero;
    out.from = AUTHORITY_NODE;
    out.to = NODENUM_BROADCAST;
    out.channel = 0;
    out.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    out.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    out.decoded.payload.size = pb_encode_to_bytes(out.decoded.payload.bytes, sizeof(out.decoded.payload.bytes),
                                                  &meshtastic_StoreForwardPlusPlus_msg, &msg);
    return out.decoded.payload.size > 0;
}

// A decode that names IDs the authority does not hold serves nothing; after the item retries run
// out the exchange must fall back to the list rather than ending and repeating forever.
void test_sketchWrongDecodeFallsBackAfterRetries()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_SKETCH;
    ingestAuthority(8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 8));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildCraftedAdvertFromDir(dirB, 0, 2, false, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, sent[0])); // the authority holds none of them
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    meshtastic_MeshPacket last = meshtastic_MeshPacket_init_zero;
    for (uint32_t i = 0; i < 3; i++) {
        Time::setTestMillis(Time::getMillis() + 31000);
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        testRouter->sent.clear();
        follower->runOnce();
        std::vector<meshtastic_MeshPacket> out = takeSent();
        if (!out.empty())
            last = out[0];
    }
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(last).sfpp_message_type);
}

// The cardinality pre-check rejects a decode whose own-only and missing counts do not reconstruct
// the advertised count.
void test_sketchCountMismatchFallsBackImmediately()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_SKETCH;
    ingestAuthority(8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 8));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildCraftedAdvertFromDir(dirB, 0, 2, true, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);
}

// --- Stage 8c robustness ---

// N1: sendToMesh may free the packet during send; the module must not touch it afterwards. Run
// this under -e coverage (ASan) to catch a use-after-free.
void test_sendFreesPacketSafely()
{
    ingestAuthority(2);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 2));
    testRouter->freeOnSend = true; // every send frees the packet, as a real radio may

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemFrames);
    assertConverged();
}

// N3: a blank follower now derives the channel root itself, so a full-root advert naming that same
// root lets it join and pull normally.
void test_blankFollowerPullsFromFullRootAdvert()
{
    ingestAuthority(3);

    // A fresh store: no rows and no root.
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    follower = new TestSfppModule();

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, true));
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, decodeSfpp(advert).root_hash.size);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));

    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(requests[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, requests[0]));

    std::vector<meshtastic_MeshPacket> provides = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provides.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provides[0]));

    std::vector<meshtastic_MeshPacket> items = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, items.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, items[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 32; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(3, frames);
    assertConverged();
}

// F3.6: an unknown pusher is accepted on the root, bounded to a few rows per sweep window.
void test_unknownPusherIsBudgeted()
{
    ingestAuthority(1);

    for (uint32_t i = 0; i < 9; i++) {
        uint8_t cipher[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        cipher[0] = (uint8_t)i;
        std::vector<meshtastic_StoreForwardPlusPlus> frames;
        TEST_ASSERT_TRUE(sfppframe::split(cipher, sizeof(cipher), NODENUM_BROADCAST, UNKNOWN_NODE, 0x7000 + i, 0, false, frames));
        for (const auto &frame : frames) {
            meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
            mp.from = UNKNOWN_NODE;
            mp.to = AUTHORITY_NODE;
            mp.channel = 0;
            mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
            mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                         &meshtastic_StoreForwardPlusPlus_msg, &frame);
            TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, mp));
        }
    }
    // One existing row plus eight accepted; the ninth is over the per-window budget.
    TEST_ASSERT_EQUAL_UINT32(9, readBucket(dirA, 1, sfppsr::kBucketObjects).count);
}

// Stage 8e: at production pacing (20 s) with two requesters, a follower missing 20 converges,
// receiving exactly its 20 frames and no duplicates from retries it should not have made.
void test_pacedDeliveryConverges()
{
    portduino_config.sfpp_enum_drain_ms = 20000;
    ingestAuthority(20);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 1, 20));  // empty
    TEST_ASSERT_TRUE(deleteCounterRange(dirC, 17, 20)); // missing 4

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> bRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bRequest.size());
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> cRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, cRequest.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, bRequest[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, cRequest[0]));

    // Two provides, one per requester. Take each requester's reply before the next drain clears it.
    std::vector<meshtastic_MeshPacket> bItem;
    std::vector<meshtastic_MeshPacket> cItem;
    for (uint32_t i = 0; i < 2; i++) {
        std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(20000);
        TEST_ASSERT_EQUAL_UINT(1, provide.size());
        TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE,
                          decodeSfpp(provide[0]).sfpp_message_type);
        if (provide[0].to == FOLLOWER_NODE) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
            bItem = takeSent();
        } else {
            TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, provide[0]));
            cItem = takeSent();
        }
    }
    TEST_ASSERT_EQUAL_UINT(1, bItem.size());
    TEST_ASSERT_EQUAL_UINT(1, cItem.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(bItem[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(cItem[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT(20, (uint32_t)decodeSfpp(bItem[0]).sr_short_ids_count);
    TEST_ASSERT_EQUAL_UINT(4, (uint32_t)decodeSfpp(cItem[0]).sr_short_ids_count);
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, bItem[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, cItem[0]));

    uint32_t bFrames = 0;
    uint32_t cFrames = 0;
    for (uint32_t guard = 0; guard < 96; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(20000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break; // the advert after the queues drained
        for (const auto &frame : out) {
            if (frame.to == FOLLOWER_NODE) {
                TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
                bFrames++;
            } else if (frame.to == FOLLOWER2_NODE) {
                TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, frame));
                cFrames++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(20, bFrames); // no retry-induced duplicates
    TEST_ASSERT_EQUAL_UINT(4, cFrames);

    const BucketView a = readBucket(dirA, 1, sfppsr::kBucketObjects);
    const BucketView b = readBucket(dirB, 1, sfppsr::kBucketObjects);
    const BucketView c = readBucket(dirC, 1, sfppsr::kBucketObjects);
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, b.checksum);
    TEST_ASSERT_EQUAL_UINT32(a.count, c.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, c.checksum);
}

// Stage 8e items 5, 8, 9: broadcast item frames serve a listener that never requests; a
// listen-only archive converges from another follower's exchange and sends nothing.
void test_broadcastServesListenOnlyFollower()
{
    portduino_config.sfpp_item_transport = SFPP_ITEM_BROADCAST;
    ingestAuthority(20);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 1, 20));
    TEST_ASSERT_TRUE(deleteCounterRange(dirC, 1, 20));

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> bRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bRequest.size());

    portduino_config.sfpp_enum_listen_only = true;
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // listen-only: learns the authority, sends nothing
    portduino_config.sfpp_enum_listen_only = false;

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, bRequest[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    std::vector<meshtastic_MeshPacket> itemRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, itemRequest.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, itemRequest[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 64; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, frame.to);
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            portduino_config.sfpp_enum_listen_only = true;
            TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, frame));
            TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // still sends nothing
            portduino_config.sfpp_enum_listen_only = false;
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(20, frames);

    const BucketView a = readBucket(dirA, 1, sfppsr::kBucketObjects);
    const BucketView b = readBucket(dirB, 1, sfppsr::kBucketObjects);
    const BucketView c = readBucket(dirC, 1, sfppsr::kBucketObjects);
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    TEST_ASSERT_EQUAL_UINT32(a.count, c.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, c.checksum);
}

// Stage 8e item 8: an object from a non-authority sender is not accepted.
void test_objectFromNonAuthorityIsIgnored()
{
    ingestAuthority(3);
    buildFollowerFromCopy();

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert)); // learns the authority
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint8_t cipher[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    TEST_ASSERT_TRUE(sfppframe::split(cipher, sizeof(cipher), NODENUM_BROADCAST, UNKNOWN_NODE, 0x7100, 0, false, frames));
    TEST_ASSERT_EQUAL_UINT(1, frames.size());
    frames[0].chain_count = 0;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = UNKNOWN_NODE;
    mp.to = NODENUM_BROADCAST;
    mp.channel = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                 &meshtastic_StoreForwardPlusPlus_msg, &frames[0]);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, mp));

    TEST_ASSERT_EQUAL_UINT32(3, readBucket(dirB, 1, sfppsr::kBucketObjects).count);
}

// Stage 8e item 10: texts heard live sit in scratch; promote them instead of re-fetching.
void test_heardLiveObjectsArePromoted()
{
    ingestAuthority(20);
    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 1, 20)); // canon empty, root retained
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    follower = new TestSfppModule();

    for (uint32_t i = 0; i < 15; i++) {
        char text[16];
        snprintf(text, sizeof(text), "msg %u", i + 1);
        ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, BASE_ID + i, text, 0);
    }

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);
    TEST_ASSERT_EQUAL_UINT(5, exchange.itemFrames); // the 15 scratch rows are not re-fetched
    assertConverged();
}

// --- Stage 8h ---

// A burst of adverts with rising tips starts at most one pull (one exchange, one pacer slot).
void test_risingTipsProduceOnePull()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 39, 40));

    for (uint32_t tip = 40; tip < 45; tip++) {
        meshtastic_MeshPacket advert;
        TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, tip));
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        follower->runOnce();
    }
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(requests[0]).sfpp_message_type);
}

// The confirmed tip is per (root, authority): a new authority with a lower tip still gets its tail
// pulled instead of being suppressed by the old high-water mark.
void test_newAuthorityLowerTipPullsTail()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 39, 40));
    Exchange first = runExchange(); // converge; known tip becomes 40
    TEST_ASSERT_EQUAL_UINT(2, first.itemFrames);

    Time::setTestMillis(Time::getMillis() + 2000); // clear any pacer slot

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 39));
    advert.from = UNKNOWN_NODE;
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();

    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(request[0]).chain_count);
}

// Two valid objects with the same 32-bit short ID: the scratch copy must not stand in for the
// peer's object. The unconfirmed promotion is undone and the peer's object is fetched.
void test_genuineShortIdCollisionIsFetched()
{
    ingestAuthority(1);
    CollisionCandidate held;
    CollisionCandidate missingObject;
    makeCandidate(kCollisionACipher, kCollisionAId, held);
    makeCandidate(kCollisionBCipher, kCollisionBId, missingObject);
    TEST_ASSERT_EQUAL_UINT32(held.shortId, missingObject.shortId);
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, 2, missingObject));

    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteCounter(dirB, 2));
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    follower = new TestSfppModule();
    TEST_ASSERT_TRUE(
        insertScratchRow(dirB, NODENUM_BROADCAST, SENDER_NODE, held.id, held.cipher, sizeof(held.cipher), held.hash));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemFrames);
    TEST_ASSERT_EQUAL_UINT(0, countRows(dirB, held.id));          // unconfirmed promotion removed
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, missingObject.id)); // peer's object fetched
    assertConverged();
}

// --- Stage 8g ---

// EnumListenOnly must be silent on every path, including the legacy CLIENT_HISTORY shim, because
// the guard is at the module's send boundary.
void test_listenOnlyBlocksLegacyShim()
{
    ingestAuthority(1);
    portduino_config.sfpp_enum_listen_only = true;

    meshtastic_StoreAndForward request = meshtastic_StoreAndForward_init_zero;
    request.rr = meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY;
    request.which_variant = meshtastic_StoreAndForward_history_tag;
    request.variant.history.window = 60;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0;
    mp.to = AUTHORITY_NODE;
    mp.channel = 0;
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_API;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;
    mp.decoded.payload.size =
        pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &request);

    myNodeInfo.my_node_num = AUTHORITY_NODE;
    portduino_config.sfpp_stratum0 = true;
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, authority->handleReceived(mp));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    testRouter->sent.clear();
    authority->runOnce(); // the shim tries to answer; the send boundary drops it
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
    TEST_ASSERT_EQUAL_UINT(0, drainPhoneQueueCount());
    portduino_config.sfpp_enum_listen_only = false;
}

// A scratch row whose stored hash does not match its own object must not be promoted, even when
// its truncated short ID collides with a real peer object.
void test_corruptScratchRowIsNotPromoted()
{
    ingestAuthority(1);
    CollisionCandidate held;
    CollisionCandidate missingObject;
    makeCandidate(kCollisionACipher, kCollisionAId, held);
    makeCandidate(kCollisionBCipher, kCollisionBId, missingObject);
    TEST_ASSERT_EQUAL_UINT32(held.shortId, missingObject.shortId);
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, 2, missingObject));

    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteCounter(dirB, 2));
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    follower = new TestSfppModule();

    // `held`'s ciphertext filed under `missingObject`'s hash: same short ID, different object.
    TEST_ASSERT_TRUE(
        insertScratchRow(dirB, NODENUM_BROADCAST, SENDER_NODE, held.id, held.cipher, sizeof(held.cipher), missingObject.hash));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemFrames); // only the real object is fetched
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, missingObject.id));
    TEST_ASSERT_EQUAL_UINT(0, countRows(dirB, held.id)); // the corrupt row was not promoted
    assertConverged();
}

// A behind-bucket advert that arrives mid-exchange is remembered and pulled when the exchange ends,
// not dropped until its next rotation turn.
void test_midExchangeAdvertIsPulledAfter()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 31));
    TEST_ASSERT_TRUE(deleteCounter(dirB, 32)); // bucket 0 short 2
    TEST_ASSERT_TRUE(deleteCounter(dirB, 39));
    TEST_ASSERT_TRUE(deleteCounter(dirB, 40)); // bucket 1 short 2

    meshtastic_MeshPacket advert1;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 1, false, advert1, false, 40));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert1));
    std::vector<meshtastic_MeshPacket> req1 = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, req1.size());
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(req1[0]).chain_count);

    // Bucket 0's advert arrives mid-exchange; it must be remembered, not dropped.
    meshtastic_MeshPacket advert0;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert0, false, 40));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert0));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    // Complete bucket 1.
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, req1[0]));
    std::vector<meshtastic_MeshPacket> prov1 = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, prov1.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, prov1[0]));
    std::vector<meshtastic_MeshPacket> item1 = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, item1.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, item1[0]));
    uint32_t frames1 = 0;
    for (uint32_t guard = 0; guard < 16; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            frames1++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, frames1);

    // The exchange ended; the remembered bucket is pulled on the next run.
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    testRouter->sent.clear();
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> req0 = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, req0.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(req0[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(0, decodeSfpp(req0[0]).chain_count);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, req0[0]));
    std::vector<meshtastic_MeshPacket> prov0 = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, prov0.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, prov0[0]));
    std::vector<meshtastic_MeshPacket> item0 = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, item0.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, item0[0]));
    uint32_t frames0 = 0;
    for (uint32_t guard = 0; guard < 16; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            frames0++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, frames0);

    const BucketView a0 = readBucket(dirA, 1, 32);
    const BucketView b0 = readBucket(dirB, 1, 32);
    const BucketView a1 = readBucket(dirA, 33, 64);
    const BucketView b1 = readBucket(dirB, 33, 64);
    TEST_ASSERT_EQUAL_UINT32(a0.count, b0.count);
    TEST_ASSERT_EQUAL_UINT64(a0.checksum, b0.checksum);
    TEST_ASSERT_EQUAL_UINT32(a1.count, b1.count);
    TEST_ASSERT_EQUAL_UINT64(a1.checksum, b1.checksum);
}

// --- Stage 8f: advert rotation ---

// A bucket that gained rows since its last advert goes ahead of the round-robin, so a follower
// behind in the last bucket catches up on the next advert, not buckets x interval later.
void test_rotationServesChangedBucketFirst()
{
    ingestAuthority(40); // buckets 0 (32 rows) and 1 (8)
    runAuthorityOnce();  // mark bucket 1 seen
    runAuthorityOnce();  // mark bucket 0 seen

    buildFollowerFromCopy();    // B has 40
    ingestAuthorityMore(40, 2); // A's bucket 1 gains two rows (now 10)

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(advert).chain_count); // changed bucket first
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));

    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(request[0]).chain_count);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, request[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    std::vector<meshtastic_MeshPacket> item = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, item.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, item[0]));
    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 16; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, frames);

    const BucketView a = readBucket(dirA, 33, 64);
    const BucketView b = readBucket(dirB, 33, 64);
    TEST_ASSERT_EQUAL_UINT32(10, a.count);
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, b.checksum);
}

// A follower that learns from any advert that the sender's tip is ahead pulls the newest bucket at
// once, even if the advert named another bucket; at most one exchange is open.
void test_onDemandPullsNewestBucketAndCapsPacing()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 39, 40)); // B tip 38, missing the last two

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40)); // names bucket 0, tip 40
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // queued for the pacer, not sent inline

    // startPendingPull (from runOnce) sends the newest bucket on demand.
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(request[0]).chain_count); // newest bucket on demand

    // The one-exchange cap: a second advert while active starts nothing.
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, request[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    std::vector<meshtastic_MeshPacket> item = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, item.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, item[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 16; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, frames);

    const BucketView a = readBucket(dirA, 33, 64);
    const BucketView b = readBucket(dirB, 33, 64);
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, b.checksum);
}

void enablePullRetry()
{
    portduino_config.sfpp_pull_retry = true;
}

// Drives a paced pull to a failed exchange with the provide dropped, leaving a remembered hole.
void failPendingPull(const meshtastic_MeshPacket &advert)
{
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, request[0]));
    TEST_ASSERT_EQUAL_UINT(1, drainAuthorityOne(2000).size()); // the provide is lost
    for (uint32_t i = 0; i < 2; i++) {                         // one list resend, then failure
        Time::setTestMillis(Time::getMillis() + 31000);
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        follower->runOnce();
        takeSent();
    }
}

// --- Q6 groundwork: failed-pull retry and the shared pacer ---

// The 8f on-demand tail must obey the same drain pacer as everything else. The parent started it
// inline, so a second advert inside the interval sent a request and this assertion failed there.
void test_secondAdvertInsideDrainIntervalSendsNoRequest()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounter(dirB, 40);
    ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, BASE_ID + 39, "msg 40", 100); // too young to push

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> first = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, first.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(first[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, buildProvideFromDir(dirA, 1)));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // the scratch row filled the only hole

    ingestAuthorityMore(40, 1);
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 41));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // inside the 2 s drain interval set in setUp()

    Time::setTestMillis(Time::getMillis() + 2000); // one enumDrainIntervalMs()
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> second = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, second.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(second[0]).sfpp_message_type);
}

// A lost provide is remembered and retried on a later advert that names a different bucket, after
// the backoff and before a full rotation; a burst of adverts inside the backoff sends nothing.
void test_lostEnumProvideRetriesWithinBackoff()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> first = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, first.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, first[0]));
    TEST_ASSERT_EQUAL_UINT(1, drainAuthorityOne(2000).size()); // the provide is lost

    for (uint32_t i = 0; i < 2; i++) { // one list resend, then the exchange fails
        Time::setTestMillis(Time::getMillis() + 31000);
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        follower->runOnce();
        takeSent();
    }

    for (uint32_t i = 0; i < 6; i++) { // no retry storm inside the 30 s backoff
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
        follower->runOnce();
        TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
    }
    Time::setTestMillis(Time::getMillis() + 29000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    Time::setTestMillis(Time::getMillis() + 1000); // 30 s after the failure, under a 60 s rotation
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retry = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retry.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(retry[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(retry[0]).chain_count); // the hole's bucket, not the advert's

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, retry[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    std::vector<meshtastic_MeshPacket> item = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, item.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, item[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 16; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, frames);
    assertConverged();
}

// W is D19's proposed 24 h service window; a hole older than it is accepted as lost instead of
// being chased by every advert from the peer.
void test_failedPullStopsPastServiceWindow()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> first = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, first.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, first[0]));
    TEST_ASSERT_EQUAL_UINT(1, drainAuthorityOne(2000).size());
    for (uint32_t i = 0; i < 2; i++) {
        Time::setTestMillis(Time::getMillis() + 31000);
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        follower->runOnce();
        takeSent();
    }

    Time::setTestMillis(Time::getMillis() + 24ULL * 60 * 60 * 1000 + 1000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// The attempt count must survive the exchange: the second failure waits 60 s, not another 30 s.
// On 28900150 the entry is erased at launch, so the parent retries at +30 s and this fails.
void test_failedPullBackoffDoublesAcrossFailures()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    failPendingPull(advert); // first failure: 30 s backoff

    Time::setTestMillis(Time::getMillis() + 30000);
    failPendingPull(advert); // second failure: 60 s backoff

    Time::setTestMillis(Time::getMillis() + 30000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    Time::setTestMillis(Time::getMillis() + 31000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retry = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retry.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(retry[0]).sfpp_message_type);
}

// A failed tail pull must stay a tail pull: the retried list is the completion check, so a hole
// filled in the meantime ends the exchange with no item request. On 28900150 the retry is stored
// as (tip, 0), takes the checksum path and falls into a collision probe instead.
void test_failedTailPullRetriesAsTail()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounter(dirB, 40);
    ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, BASE_ID + 39, "msg 40", 100); // too young to push

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    failPendingPull(advert);

    // No new advert: the remembered entry itself must retain its tail-pull type. A fresh advert
    // would overwrite the summary and mask the parent's (tip, 0) misclassification.
    Time::setTestMillis(Time::getMillis() + 30000);
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retry = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retry.size());
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(retry[0]).chain_count); // the tail bucket

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, retry[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // the promoted scratch row completed the list
    assertConverged();
}

// Eight retries (nine failures) park the hole; a changed advertised summary revives it.
// On 3cd7de9e7 the parked entry's attempt state survives the new summary, so the revival fails.
void test_failedPullCapParksUntilSummaryChanges()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    for (uint8_t i = 0; i < 9; i++) {
        if (i > 0)
            Time::setTestMillis(Time::getMillis() + 6 * 60 * 1000); // past the 5 min backoff cap
        failPendingPull(advert);
    }
    Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000);
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // parked after the eighth retry

    ingestAuthorityMore(40, 1);
    meshtastic_MeshPacket changed;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, changed, false, 41));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, changed));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
}

// The service window must start at the first launch-and-fail, and a revived hole restarts it.
// On 3cd7de9e7 the changed summary cannot revive inside the backoff, so this fails before W.
void test_failedPullServiceWindowAfterLaunchAndRevival()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    failPendingPull(advert);

    ingestAuthorityMore(40, 1);
    meshtastic_MeshPacket changed;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, changed, false, 41));
    Time::setTestMillis(Time::getMillis() + 5000); // past the pacer, inside the old 30 s backoff
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, changed));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> revived = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, revived.size());

    // Lose the revived provide and let that exchange fail, so firstFailedMs starts at the revival.
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, revived[0]));
    TEST_ASSERT_EQUAL_UINT(1, drainAuthorityOne(2000).size());
    for (uint32_t i = 0; i < 2; i++) {
        Time::setTestMillis(Time::getMillis() + 31000);
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        follower->runOnce();
        takeSent();
    }
    Time::setTestMillis(Time::getMillis() + 24ULL * 60 * 60 * 1000 + 1000);
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// The retry is opt-in; the default must not re-air a hole after a failed exchange.
// On 3cd7de9e7 there is no setting, so the remembered hole retries at +30 s and this fails.
void test_pullRetryDisabledByDefault()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    failPendingPull(advert);

    Time::setTestMillis(Time::getMillis() + 30000);
    follower->runOnce();
    // Leaderless: the follower may advertise its own buckets, but it must not ask for the hole again.
    for (const auto &p : takeSent()) {
        const auto type = decodeSfpp(p).sfpp_message_type;
        TEST_ASSERT_NOT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, type);
        TEST_ASSERT_NOT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, type);
    }
}

// --- Retry semantics still open (work plan, "Re-check of the final Q6 fix", items 1, 2 and 4) ---
// These pin today's behaviour, not a decision. Whichever way Tom settles each item, the matching
// assertion is meant to change, so the change is deliberate and visible.

// Sets up a hole (39-40) inside bucket 1 while the follower holds the tip, so only an advert that
// names bucket 1 shows the mismatch: the named-mismatch path, not the tail path.
void failNamedBucketPull(meshtastic_MeshPacket &advert)
{
    ingestAuthority(64);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 39, 40));
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 1, false, advert, false, 64));
    failPendingPull(advert);
}

// Item 1: with PullRetry off, the failed hole's record is dropped, but the next advert naming the
// same mismatching bucket starts a new exchange at once. "Off" stops the timer, not the re-pull.
void test_pullRetryOffNamedAdvertRePullsAtOnce()
{
    meshtastic_MeshPacket advert;
    failNamedBucketPull(advert);

    Time::setTestMillis(Time::getMillis() + 2000); // one drain interval, well inside a 30 s backoff
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> again = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, again.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(again[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(1, decodeSfpp(again[0]).chain_count);
}

// Item 4: with PullRetry on, a named-mismatch advert also bypasses the backoff; the backoff only
// paces the remembered entry's own timer and tail pulls.
void test_pullRetryOnNamedAdvertBypassesBackoff()
{
    enablePullRetry();
    meshtastic_MeshPacket advert;
    failNamedBucketPull(advert);

    Time::setTestMillis(Time::getMillis() + 2000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> again = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, again.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(again[0]).sfpp_message_type);
}

// Item 2, first half: a parked tail hole is revived by any new write, because a tail entry's only
// summary is the tip. A hole no peer can serve is therefore re-parked and revived once per write.
void test_parkedTailHoleRevivesOnEveryWrite()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    uint32_t tip = 40;
    for (uint8_t round = 0; round < 2; round++) {
        meshtastic_MeshPacket advert;
        TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, tip));
        for (uint8_t i = 0; i < 9; i++) {
            if (i > 0)
                Time::setTestMillis(Time::getMillis() + 6 * 60 * 1000); // past the 5 min backoff cap
            failPendingPull(advert);
        }
        Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000);
        follower->runOnce();
        TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // parked

        ingestAuthorityMore(tip, 1); // one new write moves the tip, and nothing else the hole needs
        tip++;
    }
    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, tip));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(1, takeSent().size()); // revived a second time
}

// Item 2, second half: a revival resets firstFailedMs, so W counts from the last revival, not from
// the hole's first failure. A hole in a bucket that keeps changing is never given up.
void test_revivalRestartsServiceWindow()
{
    enablePullRetry();
    ingestAuthority(40);
    buildFollowerFromCopy();
    deleteCounterRange(dirB, 39, 40);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    failPendingPull(advert); // first failure: W starts here
    const uint32_t firstFailure = Time::getMillis();

    Time::setTestMillis(firstFailure + 23ULL * 60 * 60 * 1000); // inside W
    ingestAuthorityMore(40, 1);
    meshtastic_MeshPacket changed;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, changed, false, 41));
    failPendingPull(changed); // revived, then failed again

    Time::setTestMillis(firstFailure + 24ULL * 60 * 60 * 1000 + 1000); // past W from the first failure
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(1, takeSent().size()); // still retried: W restarted at the revival
}

// --- Q9 gap fixes ---

// G3: the one pacer has a 30-frame/10-min window across all sends, not just drain spacing.
void test_windowBudgetCapsServedFrames()
{
    portduino_config.sfpp_enum_drain_ms = 100;
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 1, 40));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, false, advert, false, 40));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> initial = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, initial.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, initial[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(100);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    std::vector<meshtastic_MeshPacket> wanted = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, wanted.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, wanted[0]));

    uint32_t served = 0;
    for (uint32_t i = 0; i < 64; i++) {
        for (const auto &out : drainAuthorityOne(100))
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                served++;
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, served);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(30, served);
}

// G11/F3.8: a single requester gets a per-peer window budget, not the whole global pacer.
void test_oneRequesterCannotHoldThePacer()
{
    portduino_config.sfpp_enum_drain_ms = 100;
    ingestAuthority(40);
    std::vector<pinsketch::Element> ids;
    TEST_ASSERT_TRUE(readShortIds(dirA, 0, ids));
    TEST_ASSERT_EQUAL_UINT(sfppsr::kBucketObjects, ids.size());

    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
    msg.sr_version = sfppsr::kDerivationVersion;
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));
    msg.root_hash.size = 4;
    memcpy(msg.root_hash.bytes, root, 4);
    msg.chain_count = 0;
    for (pb_size_t i = 0; i < ids.size(); i++)
        msg.sr_short_ids[i] = ids[i];
    msg.sr_short_ids_count = (pb_size_t)ids.size();

    meshtastic_MeshPacket request = meshtastic_MeshPacket_init_zero;
    request.from = FOLLOWER_NODE;
    request.to = AUTHORITY_NODE;
    request.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    request.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    portduino_config.sfpp_stratum0 = true;
    TEST_ASSERT_TRUE(authority->handleReceivedProtobuf(request, &msg));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // paced, not sent inline

    uint32_t frames = 0;
    for (uint32_t i = 0; i < 40; i++) {
        for (const auto &out : drainAuthorityOne(100))
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME &&
                out.to == FOLLOWER_NODE)
                frames++;
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(24, frames);
}

// F3.4: a re-noted peer is most-recently-used, so a flood of other requesters cannot evict it.
void test_knownPeerTableIsLeastRecentlyUsed()
{
    ingestAuthority(1);
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));

    auto requestFrom = [&](NodeNum node) {
        meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
        msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
        msg.sr_version = sfppsr::kDerivationVersion;
        msg.root_hash.size = 4;
        memcpy(msg.root_hash.bytes, root, 4);
        msg.chain_count = 0;
        msg.sr_short_ids[0] = 0xDEADBEEF;
        msg.sr_short_ids_count = 1;
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        mp.from = node;
        mp.to = AUTHORITY_NODE;
        mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
        myNodeInfo.my_node_num = AUTHORITY_NODE;
        portduino_config.sfpp_stratum0 = true;
        TEST_ASSERT_TRUE(authority->handleReceivedProtobuf(mp, &msg));
        takeSent();
    };

    constexpr NodeNum firstPeer = 0x90000000;
    for (uint32_t i = 0; i < 16; i++)
        requestFrom(firstPeer + i);
    requestFrom(firstPeer);      // refresh the oldest entry
    requestFrom(firstPeer + 16); // one more peer; the refreshed one must survive

    for (uint32_t i = 0; i < 9; i++) {
        uint8_t cipher[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        cipher[0] = (uint8_t)i;
        std::vector<meshtastic_StoreForwardPlusPlus> frames;
        TEST_ASSERT_TRUE(sfppframe::split(cipher, sizeof(cipher), NODENUM_BROADCAST, firstPeer, 0x8000 + i, 0, false, frames));
        for (const auto &frame : frames) {
            meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
            mp.from = firstPeer;
            mp.to = AUTHORITY_NODE;
            mp.channel = 0;
            mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
            mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                         &meshtastic_StoreForwardPlusPlus_msg, &frame);
            TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, mp));
        }
    }
    TEST_ASSERT_EQUAL_UINT32(10, readBucket(dirA, 1, sfppsr::kBucketObjects).count);
}

// --- Stage 8: sketch mode ---

void runSketchCase(uint32_t total, uint32_t missing)
{
    portduino_config.sfpp_repair = SFPP_REPAIR_SKETCH;
    ingestAuthority(total);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, total - missing + 1, total));

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT(missing, (uint32_t)decodeSfpp(sent[0]).sr_short_ids_count);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, sent[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 64; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break; // the advert that follows the drained queue
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(missing, frames);
    assertConverged();
}

// A small difference resolves from the capacity-4 sketch with no list exchange.
void test_sketchResolvesOne()
{
    runSketchCase(20, 1);
}

void test_sketchResolvesFour()
{
    runSketchCase(20, 4);
}

// More than four differences cannot decode, so the list exchange takes over.
void test_sketchFallsBackAtSixteen()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_SKETCH;
    ingestAuthority(20);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounterRange(dirB, 5, 20));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.enumRequests);
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);
    TEST_ASSERT_EQUAL_UINT(16, exchange.itemFrames);
    assertConverged();
}

// A sketch that decodes to the right IDs but whose advert checksum lies is caught after the fetch
// and falls back to the list exchange.
void test_sketchWrongChecksumFallsBack()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_SKETCH;
    ingestAuthority(8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteCounter(dirB, 8));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, 0, true, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, sent[0]));
    std::vector<meshtastic_MeshPacket> frame = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, frame.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, frame[0]));

    std::vector<meshtastic_MeshPacket> fallback = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, fallback.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(fallback[0]).sfpp_message_type);
}

// --- Stage 9: writer-missed recovery ---

void ingestScratchText(TestSfppModule *module, NodeNum node, NodeNum from, PacketId id, const char *text, uint32_t ageS)
{
    meshtastic_MeshPacket mp = makeText(from, id, text, getValidTime(RTCQualityNTP, true) - ageS);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    myNodeInfo.my_node_num = node;
    portduino_config.sfpp_stratum0 = false;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, module->ingest(mp, &enc));
}

void test_writerMissedIsPushedAndSecondFollowerConverges()
{
    ingestAuthority(1); // the authority has one text and a root

    // The follower is a copy missing that text; a normal exchange makes it a known peer and gives
    // it the root.
    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteCounter(dirB, 1));
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    follower = new TestSfppModule();
    Exchange first = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, first.itemFrames);

    // A second follower taken before the writer-missed texts.
    TEST_ASSERT_TRUE(copyDb(dirA, dirC));
    portduino_config.sfpp_db_path = dirC + "/";
    portduino_config.sfpp_stratum0 = false;
    follower2 = new TestSfppModule();

    // The follower hears five texts the authority did not.
    for (uint32_t i = 0; i < 5; i++) {
        char text[24];
        snprintf(text, sizeof(text), "writer missed %u", i);
        ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, 0x6000 + i, text, 301);
    }

    // An advert teaches the follower the authority's node; it is in sync, so it does not pull.
    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint32_t pushed = 0;
    for (uint32_t guard = 0; guard < 64 && pushed < 5; guard++) {
        Time::setTestMillis(Time::getMillis() + 2000);
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        testRouter->sent.clear();
        follower->runOnce();
        std::vector<meshtastic_MeshPacket> sent = takeSent();
        for (const auto &frame : sent) {
            if (decodeSfpp(frame).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                continue;
            TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, frame));
            pushed++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(5, pushed);
    TEST_ASSERT_EQUAL_UINT32(6, readBucket(dirA, 1, sfppsr::kBucketObjects).count); // 1 live + 5 pushed

    // The second follower now converges through the ordinary exchange.
    Exchange second = runExchangeFor(follower2, FOLLOWER2_NODE);
    TEST_ASSERT_EQUAL_UINT(1, second.itemRequests);
    TEST_ASSERT_EQUAL_UINT(5, second.itemFrames);

    const BucketView a = readBucket(dirA, 1, sfppsr::kBucketObjects);
    const BucketView c = readBucket(dirC, 1, sfppsr::kBucketObjects);
    TEST_ASSERT_EQUAL_UINT32(a.count, c.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, c.checksum);
}

void test_two_archives_derive_the_same_root_without_stratum0()
{
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    Time::setTestMillis(1000);
    follower->runOnce();

    uint8_t rootB[SFPP_HASH_SIZE] = {0};
    TEST_ASSERT_TRUE_MESSAGE(readMappingRoot(dirB, rootB), "a follower with no stratum 0 must derive a root");

    portduino_config.sfpp_db_path = dirC + "/";
    follower2 = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER2_NODE;
    Time::setTestMillis(987654321);
    follower2->runOnce();

    uint8_t rootC[SFPP_HASH_SIZE] = {0};
    TEST_ASSERT_TRUE_MESSAGE(readMappingRoot(dirC, rootC), "a second follower must derive a root too");
    TEST_ASSERT_EQUAL_MEMORY(rootB, rootC, SFPP_HASH_SIZE);
}

// A full-root advert naming a root we did not derive is a different scope: ignore it, do not adopt it,
// and do not pull from it.
void test_full_root_advert_with_a_foreign_root_is_ignored()
{
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;

    uint8_t foreign[SFPP_HASH_SIZE];
    memset(foreign, 0xAB, sizeof(foreign));
    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertWithRoot(foreign, 0, 1, advert));

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, takeSent().size(), "a foreign full-root advert must not start a pull");

    uint8_t mapped[SFPP_HASH_SIZE] = {0};
    if (readMappingRoot(dirB, mapped))
        TEST_ASSERT_TRUE_MESSAGE(memcmp(mapped, foreign, SFPP_HASH_SIZE) != 0, "the foreign root must not be adopted");
}

// A follower with no stratum 0 advertises its own bucket; the flag no longer gates advertising.
// A non-stratum-0 archive accepts a hash-verified writer-missed push for its own root, and refuses
// one from a peer known to advertise a different root.
void test_follower_accepts_push_but_refuses_a_foreign_root_pusher()
{
    constexpr NodeNum PUSHER_A = 0x66667777;
    constexpr NodeNum PUSHER_B = 0x77778888;
    constexpr PacketId FOREIGN_PUSH_ID = 0x1E1A1001;
    constexpr PacketId OK_PUSH_ID = 0x1E1A1002;

    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    follower->runOnce(); // derive the root
    takeSent();

    uint8_t foreign[SFPP_HASH_SIZE];
    memset(foreign, 0xAB, sizeof(foreign));
    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertWithRoot(foreign, 0, 1, advert));
    advert.from = PUSHER_A;
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert)); // records A on a foreign root
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    auto push = [&](NodeNum pusher, PacketId id) {
        uint8_t cipher[32];
        memset(cipher, 0x5A, sizeof(cipher));
        std::vector<meshtastic_StoreForwardPlusPlus> frames;
        TEST_ASSERT_TRUE(sfppframe::split(cipher, sizeof(cipher), FOLLOWER_NODE, SENDER_NODE, id, 100, false, frames));
        for (const auto &frame : frames) {
            meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
            mp.from = pusher;
            mp.to = FOLLOWER_NODE;
            mp.channel = 0;
            mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
            mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                         &meshtastic_StoreForwardPlusPlus_msg, &frame);
            TEST_ASSERT_TRUE(mp.decoded.payload.size > 0);
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, mp));
        }
    };

    push(PUSHER_A, FOREIGN_PUSH_ID);
    TEST_ASSERT_EQUAL_UINT(0, countRows(dirB, FOREIGN_PUSH_ID));

    push(PUSHER_B, OK_PUSH_ID);
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, OK_PUSH_ID));
}

void test_follower_advertises_without_stratum0()
{
    ingestAuthority(2);
    buildFollowerFromCopy();
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    takeSent();

    follower->runOnce();

    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT, decodeSfpp(sent[0]).sfpp_message_type);
}

// A stratum-0 labelled node pulls from an advert like any other; the flag no longer gates pulling.
void test_stratum0_node_pulls_from_an_advert()
{
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = true;
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    follower->runOnce(); // derive the root
    takeSent();

    uint8_t ours[SFPP_HASH_SIZE] = {0};
    TEST_ASSERT_TRUE(readMappingRoot(dirB, ours));
    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertWithRoot(ours, 0, 1, advert));

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, true, advert));
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);
}

// A full-root advert naming our own derived root is the same scope: accept it and pull.
void test_full_root_advert_with_our_root_is_pulled()
{
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    follower->runOnce();

    uint8_t ours[SFPP_HASH_SIZE] = {0};
    TEST_ASSERT_TRUE_MESSAGE(readMappingRoot(dirB, ours), "the follower must have derived a root");

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertWithRoot(ours, 0, 1, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();

    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(0, decodeSfpp(sent[0]).chain_count);
}

void loop() {}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_matchingAdvertSendsNothing);
    RUN_TEST(test_missingTail1Converges);
    RUN_TEST(test_missingTail4Converges);
    RUN_TEST(test_missingTail16Converges);
    RUN_TEST(test_wholeBucketMissingConverges);
    RUN_TEST(test_middleGapConverges);
    RUN_TEST(test_shortIdCollisionConverges);
    RUN_TEST(test_unresolvableCollisionIsSuppressed);
    RUN_TEST(test_unknownScopeAdvertIsDropped);
    RUN_TEST(test_oversizedItemRequestIsRefused);
    RUN_TEST(test_unknownShortIdRequestServesNothing);
    RUN_TEST(test_advertUsesFourByteRootPrefix);
    RUN_TEST(test_firstItemFrameDrainsOnNextStep);
    RUN_TEST(test_rejectedItemFrameIsRequeued);
    RUN_TEST(test_lostItemFrameIsRetried);
    RUN_TEST(test_twoRequestersBothComplete);
    RUN_TEST(test_sendFreesPacketSafely);
    RUN_TEST(test_blankFollowerPullsFromFullRootAdvert);
    RUN_TEST(test_unknownPusherIsBudgeted);
    RUN_TEST(test_pacedDeliveryConverges);
    RUN_TEST(test_broadcastServesListenOnlyFollower);
    RUN_TEST(test_objectFromNonAuthorityIsIgnored);
    RUN_TEST(test_heardLiveObjectsArePromoted);
    RUN_TEST(test_risingTipsProduceOnePull);
    RUN_TEST(test_newAuthorityLowerTipPullsTail);
    RUN_TEST(test_genuineShortIdCollisionIsFetched);
    RUN_TEST(test_listenOnlyBlocksLegacyShim);
    RUN_TEST(test_corruptScratchRowIsNotPromoted);
    RUN_TEST(test_midExchangeAdvertIsPulledAfter);
    RUN_TEST(test_rotationServesChangedBucketFirst);
    RUN_TEST(test_onDemandPullsNewestBucketAndCapsPacing);
    RUN_TEST(test_secondAdvertInsideDrainIntervalSendsNoRequest);
    RUN_TEST(test_windowBudgetCapsServedFrames);
    RUN_TEST(test_oneRequesterCannotHoldThePacer);
    RUN_TEST(test_knownPeerTableIsLeastRecentlyUsed);
    RUN_TEST(test_lostEnumProvideRetriesWithinBackoff);
    RUN_TEST(test_failedPullStopsPastServiceWindow);
    RUN_TEST(test_failedPullBackoffDoublesAcrossFailures);
    RUN_TEST(test_failedTailPullRetriesAsTail);
    RUN_TEST(test_failedPullCapParksUntilSummaryChanges);
    RUN_TEST(test_failedPullServiceWindowAfterLaunchAndRevival);
    RUN_TEST(test_pullRetryDisabledByDefault);
    RUN_TEST(test_pullRetryOffNamedAdvertRePullsAtOnce);
    RUN_TEST(test_pullRetryOnNamedAdvertBypassesBackoff);
    RUN_TEST(test_parkedTailHoleRevivesOnEveryWrite);
    RUN_TEST(test_revivalRestartsServiceWindow);
    RUN_TEST(test_sketchResolvesOne);
    RUN_TEST(test_sketchResolvesFour);
    RUN_TEST(test_sketchFallsBackAtSixteen);
    RUN_TEST(test_sketchWrongDecodeFallsBackAfterRetries);
    RUN_TEST(test_sketchCountMismatchFallsBackImmediately);
    RUN_TEST(test_sketchWrongChecksumFallsBack);
    RUN_TEST(test_writerMissedIsPushedAndSecondFollowerConverges);
    RUN_TEST(test_two_archives_derive_the_same_root_without_stratum0);
    RUN_TEST(test_full_root_advert_with_a_foreign_root_is_ignored);
    RUN_TEST(test_full_root_advert_with_our_root_is_pulled);
    RUN_TEST(test_follower_advertises_without_stratum0);
    RUN_TEST(test_stratum0_node_pulls_from_an_advert);
    RUN_TEST(test_follower_accepts_push_but_refuses_a_foreign_root_pusher);
    UNITY_END();
}

#else

void setUp(void) {}
void tearDown(void) {}
void loop() {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    UNITY_END();
}

#endif
