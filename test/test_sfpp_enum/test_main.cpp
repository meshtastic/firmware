/*
 * Native coverage for arm C, the detector + short-ID enumeration repair path in
 * src/modules/Native/StoreForwardPlusPlus.cpp and the exchange helpers in SketchWire/SfppFrame.
 *
 * Two in-process archives share one sqlite root: the authority is stratum-0 and ingests texts; the
 * follower is built from a file copy of the authority's store with objects removed. A bucket is a
 * content bucket (first message-hash byte), so the cases choose packet IDs that land their texts in
 * a known bucket. The test routes the captured packets between the module instances, which is the
 * on-air exchange without a radio.
 *
 * What is pinned:
 *   - a matching advert sends nothing (d = 0);
 *   - a follower missing d = 1, 4, 16, a whole bucket or three middle objects converges through
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

// Runs one statement with up to two integer binds; -1 leaves a bind unset.
bool execBound(const std::string &dir, const char *sql, int64_t first = -1, int64_t second = -1)
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        if (first >= 0)
            sqlite3_bind_int64(stmt, 1, (sqlite3_int64)first);
        if (second >= 0)
            sqlite3_bind_int64(stmt, 2, (sqlite3_int64)second);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool deleteObject(const std::string &dir, const uint8_t hash[SFPP_HASH_SIZE])
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db, "delete from channel_messages where message_hash=?;", -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_blob(stmt, 1, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) == 1;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

// The `count` most recently inserted objects of one content bucket. The follower is a file copy of
// the authority, so rowid order is the authority's insertion order.
bool deleteNewest(const std::string &dir, uint32_t bucket, uint32_t count)
{
    return execBound(dir,
                     "delete from channel_messages where rowid in (select rowid from channel_messages where bucket=? "
                     "order by rowid desc limit ?);",
                     bucket, count);
}

bool deleteBucket(const std::string &dir, uint32_t bucket)
{
    return execBound(dir, "delete from channel_messages where bucket=?;", bucket);
}

// The content bucket the store filed a packet under, or -1 when it is not stored.
int64_t storedBucket(const std::string &dir, PacketId id)
{
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return -1;
    sqlite3_stmt *stmt = nullptr;
    int64_t bucket = -1;
    if (sqlite3_prepare_v2(db, "select bucket from channel_messages where packet_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            bucket = sqlite3_column_int64(stmt, 0);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return bucket;
}

// The content bucket (first message-hash byte) a text would be filed under.
uint32_t textBucket(NodeNum from, PacketId id, const char *text)
{
    meshtastic_MeshPacket enc = makeEncryptedCopy(makeText(from, id, text, 0));
    uint8_t hash[SFPP_HASH_SIZE];
    sfppframe::messageHash(enc.encrypted.bytes, enc.encrypted.size, enc.to, enc.from, enc.id, hash);
    return sfppsr::contentBucket(hash, sizeof(hash));
}

// The first packet ID at or after `start` whose text lands in `bucket`; about 256 tries.
PacketId idForBucket(NodeNum from, const char *text, uint32_t bucket, PacketId start)
{
    for (PacketId id = start; id < start + 0x10000; id++)
        if (textBucket(from, id, text) == bucket)
            return id;
    TEST_FAIL_MESSAGE("no packet ID reaches the bucket");
    return 0;
}

// Content buckets used by the cases. kBucket is the collision pair's own bucket; kLowBucket and
// kHighBucket sit below and above it, because the advert rotation walks buckets highest first.
constexpr uint32_t kLowBucket = 0x05;
constexpr uint32_t kBucket = 0x1D;
constexpr uint32_t kHighBucket = 0xC0;

struct IngestedText {
    PacketId id;
    char text[16];
    uint32_t bucket;
};
std::vector<IngestedText> ingested; // the authority's texts in insertion order
PacketId nextTextId = BASE_ID;

// The authority ingests `count` texts, each with a packet ID chosen so it lands in `bucket`, and the
// stored row's bucket column is checked rather than assumed.
void ingestIntoBucket(uint32_t bucket, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        IngestedText t;
        snprintf(t.text, sizeof(t.text), "msg %u", (unsigned)ingested.size() + 1);
        t.id = idForBucket(SENDER_NODE, t.text, bucket, nextTextId);
        t.bucket = bucket;
        nextTextId = t.id + 1;
        portduino_config.sfpp_stratum0 = true;
        ingestText(authority, SENDER_NODE, t.id, t.text);
        TEST_ASSERT_EQUAL_INT64((int64_t)bucket, storedBucket(dirA, t.id));
        ingested.push_back(t);
    }
}

// The n-th (1-based) text the authority ingested, removed from `dir` by its message hash.
bool deleteIngested(const std::string &dir, uint32_t n)
{
    if (n == 0 || n > ingested.size())
        return false;
    const IngestedText &t = ingested[n - 1];
    meshtastic_MeshPacket enc = makeEncryptedCopy(makeText(SENDER_NODE, t.id, t.text, 0));
    uint8_t hash[SFPP_HASH_SIZE];
    sfppframe::messageHash(enc.encrypted.bytes, enc.encrypted.size, enc.to, enc.from, enc.id, hash);
    return deleteObject(dir, hash);
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

// A real pair sharing a truncated short ID and a content bucket, found offline by a birthday search
// and checked in: the reassembler recomputes both from the ciphertext, so forced columns could not
// survive the trip. Both hash to shortId 0x598a726a in content bucket 0x1D.
const uint8_t kCollisionACipher[16] = {0xeb, 0x34, 0x02, 0x00, 0x34, 0x35, 0x36, 0x37,
                                       0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};
const uint8_t kCollisionBCipher[16] = {0x24, 0x55, 0x1e, 0x00, 0x34, 0x35, 0x36, 0x37,
                                       0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};
constexpr uint32_t kCollisionAId = 0x000094eb;
constexpr uint32_t kCollisionBId = 0x00009524;

// Inserts a canonical row with the current columns; the bucket is the hash's own content bucket.
bool insertCanonRow(const std::string &dir, uint32_t id, const uint8_t *cipher, size_t cipherLen,
                    const uint8_t hash[SFPP_HASH_SIZE], uint32_t shortId)
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
                                 "message_hash, payload, rx_time, short_id, checksum, time_basis, boot_id, age_margin, bucket) "
                                 "values (?,?,?,?,?,?,?,?,?,?,0,0,0,?);",
                                 -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)NODENUM_BROADCAST);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)SENDER_NODE);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)id);
        sqlite3_bind_blob(stmt, 4, root, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 5, cipher, (int)cipherLen, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 6, hash, SFPP_HASH_SIZE, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, "", 0, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 8, (sqlite3_int64)getValidTime(RTCQualityNTP, true));
        sqlite3_bind_int64(stmt, 9, (sqlite3_int64)shortId);
        sqlite3_bind_int64(stmt, 10, (sqlite3_int64)sfppsr::checksumContribution(hash, SFPP_HASH_SIZE));
        sqlite3_bind_int64(stmt, 11, (sqlite3_int64)sfppsr::contentBucket(hash, SFPP_HASH_SIZE));
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool insertCandidateRow(const std::string &dir, const CollisionCandidate &candidate)
{
    return insertCanonRow(dir, candidate.id, candidate.cipher, sizeof(candidate.cipher), candidate.hash, candidate.shortId);
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

// A fabricated row whose hash starts with kBucket, so it files under kBucket by content, carrying a
// forced short ID that its hash does not produce.
bool insertExtraRow(const std::string &dir, uint32_t shortId)
{
    uint8_t hash[SFPP_HASH_SIZE];
    for (size_t i = 0; i < sizeof(hash); i++)
        hash[i] = (uint8_t)(0xE0 + i);
    hash[0] = (uint8_t)kBucket;
    const uint8_t cipher[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    return insertCanonRow(dir, 0x7777, cipher, sizeof(cipher), hash, shortId);
}

struct BucketView {
    uint32_t count = 0;
    uint64_t checksum = 0;
};

// One content bucket's count and XOR checksum; a negative bucket reads the whole store.
BucketView readBucket(const std::string &dir, int64_t bucket)
{
    BucketView view;
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return view;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = bucket < 0 ? "select short_id, checksum from channel_messages where short_id is not null;"
                                 : "select short_id, checksum from channel_messages where bucket=? and short_id is not null;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (bucket >= 0)
            sqlite3_bind_int64(stmt, 1, (sqlite3_int64)bucket);
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

BucketView readStore(const std::string &dir)
{
    return readBucket(dir, -1);
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
    if (sqlite3_prepare_v2(db, "select short_id, checksum from channel_messages where bucket=? and short_id is not null;", -1,
                           &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)bucket);
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
    msg.encapsulated_rxtime = tip ? tip : summary.count(); // Q2e R3: the bucket's content count, as the module sends

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
meshtastic_MeshPacket buildProvideFromDir(const std::string &dir, uint32_t bucket, uint8_t frameIndex = 0, uint8_t frameTotal = 1)
{
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dir, root));

    std::vector<pinsketch::Element> ids;
    sqlite3 *db = nullptr;
    TEST_ASSERT_TRUE(openDb(dir, &db));
    sqlite3_stmt *stmt = nullptr;
    TEST_ASSERT_TRUE(sqlite3_prepare_v2(db,
                                        "select short_id from channel_messages where bucket=? and short_id is not null order by "
                                        "short_id;",
                                        -1, &stmt, nullptr) == SQLITE_OK);
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)bucket);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        ids.push_back((pinsketch::Element)sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    sfppsr::Scope scope;
    scope.rootPrefixLen = 4;
    memcpy(scope.rootPrefix, root, scope.rootPrefixLen);
    scope.bucket = bucket;
    meshtastic_StoreForwardPlusPlus msg;
    TEST_ASSERT_TRUE(sfppsr::buildEnumProvide(scope, ids, frameIndex, frameTotal, msg));

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
    bool ok =
        sqlite3_prepare_v2(db, "select short_id from channel_messages where bucket=? and short_id is not null order by rowid;",
                           -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)bucket);
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

// The authority's texts, all in kBucket, so one advert covers every object.
void ingestAuthority(uint32_t count)
{
    ingestIntoBucket(kBucket, count);
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

void assertConverged(uint32_t bucket = kBucket, const std::string &dir = dirB)
{
    const BucketView a = readBucket(dirA, bucket);
    const BucketView b = readBucket(dir, bucket);
    TEST_ASSERT_GREATER_THAN_UINT32(0, a.count);
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
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    portduino_config.sfpp_enum_interval = 1;
    portduino_config.sfpp_enum_drain_ms = 2000;
    portduino_config.sfpp_scratch_sweep = 5;
    portduino_config.sfpp_item_transport = SFPP_ITEM_DIRECT; // existing cases drive direct exchanges
    portduino_config.sfpp_enum_listen_only = false;
    portduino_config.sfpp_pull_retry = false;
    portduino_config.sfpp_db_path = dirA + "/";

    Time::setTestMillis(1000);
    ingested.clear();
    nextTextId = BASE_ID;

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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, missing));

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

// The 1, 4 and 16 most recently inserted objects are missing; each converges with the exact
// one-request-per-kind exchange.
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

// The follower lacks every object of one content bucket while holding another bucket: its local
// bucket is empty, which must classify as count 0 / checksum 0 and pull rather than be skipped.
void test_wholeBucketMissingConverges()
{
    ingestIntoBucket(kLowBucket, 4);
    ingestAuthority(8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket));
    TEST_ASSERT_EQUAL_UINT32(0, readBucket(dirB, kBucket).count);
    TEST_ASSERT_EQUAL_UINT32(4, readBucket(dirB, kLowBucket).count);

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(8, exchange.itemFrames);
    assertConverged();
    assertConverged(kLowBucket);
}

// A gap in the middle, not just the tail: the 5th, 9th and 17th of 20 objects are missing, none of
// them first or last. Recovered objects file under their content bucket on arrival.
void test_middleGapConverges()
{
    ingestAuthority(20);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteIngested(dirB, 5));
    TEST_ASSERT_TRUE(deleteIngested(dirB, 9));
    TEST_ASSERT_TRUE(deleteIngested(dirB, 17));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(3, exchange.itemFrames);
    assertConverged();
}

// Two messages really share a truncated short ID. The follower holds one and lacks the other, so
// the short-ID lists match but the checksum differs. The probe requests the shared IDs, the
// authority serves every row for them, and the unknown hash is stored.
void test_shortIdCollisionConverges()
{
    ingestAuthority(1); // establishes the root and a non-colliding filler row in the pair's bucket
    CollisionCandidate held;
    CollisionCandidate missingObject;
    makeCandidate(kCollisionACipher, kCollisionAId, held);
    makeCandidate(kCollisionBCipher, kCollisionBId, missingObject);
    TEST_ASSERT_EQUAL_UINT32(held.shortId, missingObject.shortId);
    TEST_ASSERT_TRUE(memcmp(held.hash, missingObject.hash, SFPP_HASH_SIZE) != 0);
    TEST_ASSERT_EQUAL_UINT32(kBucket, sfppsr::contentBucket(held.hash, SFPP_HASH_SIZE));
    TEST_ASSERT_EQUAL_UINT32(kBucket, sfppsr::contentBucket(missingObject.hash, SFPP_HASH_SIZE));
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, held));
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, missingObject));
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteObject(dirB, missingObject.hash)); // the follower holds `held` only

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
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, held));
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, missingObject));
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteObject(dirB, missingObject.hash)); // follower lacks the second of the pair
    // ... and holds an extra row in the same bucket whose short ID is the same, keeping the lists
    // equal while the checksums cannot converge.
    TEST_ASSERT_TRUE(insertExtraRow(dirB, held.shortId));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);             // one probe
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, missingObject.id)); // the collided object arrived
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, 0x7777));           // the extra row is still there
    TEST_ASSERT_TRUE(readBucket(dirB, kBucket).count != readBucket(dirA, kBucket).count);

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
    msg.chain_count = kBucket; // the populated bucket, so only the ID is unknown
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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));
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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));
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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));
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
    TEST_ASSERT_TRUE(deleteIngested(dirB, 3));
    TEST_ASSERT_TRUE(deleteIngested(dirC, 4));

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
    TEST_ASSERT_EQUAL(NODENUM_BROADCAST, bProvide[0].to); // the listing is broadcast; requester order is kept
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(bProvide[0]).sfpp_message_type);
    std::vector<meshtastic_MeshPacket> cProvide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, cProvide.size());
    TEST_ASSERT_EQUAL(NODENUM_BROADCAST, cProvide[0].to);
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

    assertConverged(kBucket, dirB);
    assertConverged(kBucket, dirC);
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
    if (sqlite3_prepare_v2(db, "select short_id, checksum from channel_messages where bucket=? and short_id is not null;", -1,
                           &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)bucket);
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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildCraftedAdvertFromDir(dirB, kBucket, 2, false, advert));
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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildCraftedAdvertFromDir(dirB, kBucket, 2, true, advert));
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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));
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
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advert, true));
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
    // One existing row plus eight accepted; the ninth is over the per-window budget. The pushed
    // objects file under their own content buckets, so the whole store is counted.
    TEST_ASSERT_EQUAL_UINT32(9, readStore(dirA).count);
}

// Stage 8e: at production pacing (20 s) with two requesters, a follower missing 20 converges,
// receiving exactly its 20 frames and no duplicates from retries it should not have made.
void test_pacedDeliveryConverges()
{
    portduino_config.sfpp_enum_drain_ms = 20000;
    ingestAuthority(20);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket));    // empty
    TEST_ASSERT_TRUE(deleteNewest(dirC, kBucket, 4)); // missing the newest 4

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> bRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bRequest.size());
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> cRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, cRequest.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, bRequest[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, cRequest[0]));

    // Two provides, one per requester, both broadcast: each follower hears both. The second copy
    // names IDs already wanted, so it adds no request. Take each reply before the next drain clears it.
    std::vector<meshtastic_MeshPacket> bItem;
    std::vector<meshtastic_MeshPacket> cItem;
    for (uint32_t i = 0; i < 2; i++) {
        std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(20000);
        TEST_ASSERT_EQUAL_UINT(1, provide.size());
        TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE,
                          decodeSfpp(provide[0]).sfpp_message_type);
        TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, provide[0].to);
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
        for (const auto &out : takeSent())
            bItem.push_back(out);
        TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, provide[0]));
        for (const auto &out : takeSent())
            cItem.push_back(out);
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

    assertConverged(kBucket, dirB);
    assertConverged(kBucket, dirC);
}

// Stage 8e items 5, 8, 9: broadcast item frames serve a listener that never requests; a
// listen-only archive converges from another follower's exchange and sends nothing.
void test_broadcastServesListenOnlyFollower()
{
    portduino_config.sfpp_item_transport = SFPP_ITEM_BROADCAST;
    ingestAuthority(20);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket));
    TEST_ASSERT_TRUE(deleteBucket(dirC, kBucket));

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

    assertConverged(kBucket, dirB);
    assertConverged(kBucket, dirC);
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

    TEST_ASSERT_EQUAL_UINT32(3, readStore(dirB).count); // the frame's object would file under its own bucket
}

// Stage 8e item 10: texts heard live sit in scratch; promote them instead of re-fetching.
void test_heardLiveObjectsArePromoted()
{
    ingestAuthority(20);
    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket)); // canon empty, root retained
    TEST_ASSERT_EQUAL_UINT32(0, readStore(dirB).count);
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_stratum0 = false;
    follower = new TestSfppModule();

    for (uint32_t i = 0; i < 15; i++) // the first 15 of the authority's 20 texts, heard live
        ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, ingested[i].id, ingested[i].text, 0);

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);
    TEST_ASSERT_EQUAL_UINT(5, exchange.itemFrames); // the 15 scratch rows are not re-fetched
    assertConverged();
}

// --- Stage 8h ---

// A burst of adverts whose tip (the bucket's content count) rises with each new write starts at most
// one pull: the first opens the exchange, the rest name the bucket already being pulled.
void test_risingTipsProduceOnePull()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 2));

    for (uint32_t i = 0; i < 5; i++) {
        if (i > 0)
            ingestAuthority(1); // one more write, so the next advert carries a higher tip
        meshtastic_MeshPacket advert;
        TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advert));
        TEST_ASSERT_EQUAL_UINT32(40 + i, decodeSfpp(advert).encapsulated_rxtime);
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        portduino_config.sfpp_stratum0 = false;
        follower->runOnce();
    }
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(requests[0]).sfpp_message_type);
}

// Pull state is per (root, authority): after converging with one authority, a new authority whose
// advert carries a lower tip than the old one's still gets its differing bucket pulled. The content
// model has no tip high-water mark, so this pins that none came back.
void test_newAuthorityLowerTipPullsTail()
{
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 2));
    Exchange first = runExchange(); // converge with the first authority
    TEST_ASSERT_EQUAL_UINT(2, first.itemFrames);
    assertConverged();

    Time::setTestMillis(Time::getMillis() + 2000); // clear any pacer slot
    ingestAuthority(1);                            // the new authority's bucket differs by one object

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advert, false, 1)); // tip 1, under the old 40
    advert.from = UNKNOWN_NODE;
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();

    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(kBucket, decodeSfpp(request[0]).chain_count);
    TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, request[0].to); // the listing request is broadcast
    TEST_ASSERT_EQUAL_UINT32(UNKNOWN_NODE, decodeSfpp(request[0]).encapsulated_to);
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
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, missingObject));

    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteObject(dirB, missingObject.hash));
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
    TEST_ASSERT_TRUE(insertCandidateRow(dirA, missingObject));

    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteObject(dirB, missingObject.hash));
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

// Finishes a pull the requester opened: the authority answers `request` with a provide, the
// requester asks for its missing objects, and the paced item frames are delivered. Returns the
// number of item frames.
uint32_t finishPull(TestSfppModule *requester, NodeNum node, const meshtastic_MeshPacket &request)
{
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, request));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provide[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(requester, node, false, provide[0]));
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
            TEST_ASSERT_TRUE(deliver(requester, node, false, frame));
            frames++;
        }
    }
    return frames;
}

// A behind-bucket advert that arrives mid-exchange is remembered and pulled when the exchange ends,
// not dropped until its next rotation turn.
void test_midExchangeAdvertIsPulledAfter()
{
    ingestAuthority(32);
    ingestIntoBucket(kHighBucket, 8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 2));     // kBucket short 2
    TEST_ASSERT_TRUE(deleteNewest(dirB, kHighBucket, 2)); // kHighBucket short 2

    meshtastic_MeshPacket advertHigh;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, advertHigh));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advertHigh));
    std::vector<meshtastic_MeshPacket> reqHigh = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, reqHigh.size());
    TEST_ASSERT_EQUAL_UINT32(kHighBucket, decodeSfpp(reqHigh[0]).chain_count);

    // kBucket's advert arrives mid-exchange; it must be remembered, not dropped.
    meshtastic_MeshPacket advertLow;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advertLow));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advertLow));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    TEST_ASSERT_EQUAL_UINT(2, finishPull(follower, FOLLOWER_NODE, reqHigh[0]));

    // The exchange ended; the remembered bucket is pulled on the next run.
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    testRouter->sent.clear();
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> reqLow = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, reqLow.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(reqLow[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(kBucket, decodeSfpp(reqLow[0]).chain_count);

    TEST_ASSERT_EQUAL_UINT(2, finishPull(follower, FOLLOWER_NODE, reqLow[0]));
    assertConverged(kBucket);
    assertConverged(kHighBucket);
}

// --- Stage 8f: advert rotation ---

// A bucket that gained objects since its last advert goes ahead of the round-robin, so a follower
// behind in it catches up on the next advert, not buckets x interval later. The changed bucket is
// the lower one, so walking highest-first or round-robin would both name the other.
void test_rotationServesChangedBucketFirst()
{
    ingestIntoBucket(kHighBucket, 32);
    ingestAuthority(8);
    TEST_ASSERT_EQUAL_UINT32(kHighBucket, decodeSfpp(runAuthorityOnce()).chain_count); // mark it seen
    TEST_ASSERT_EQUAL_UINT32(kBucket, decodeSfpp(runAuthorityOnce()).chain_count);     // mark it seen

    buildFollowerFromCopy(); // B has both buckets
    ingestAuthority(2);      // A's kBucket gains two objects (now 10); the round-robin turn is kHighBucket's

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_EQUAL_UINT32(kBucket, decodeSfpp(advert).chain_count); // changed bucket first
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));

    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(kBucket, decodeSfpp(request[0]).chain_count);

    TEST_ASSERT_EQUAL_UINT(2, finishPull(follower, FOLLOWER_NODE, request[0]));
    TEST_ASSERT_EQUAL_UINT32(10, readBucket(dirA, kBucket).count);
    assertConverged(kBucket);
}

// The content model has no tip-driven pull (removed in Q10 step 4): an advert for a bucket the
// follower agrees on starts nothing, whatever its tip. An advert for the bucket it lacks starts one
// exchange inline, and at most one exchange is open.
void test_onDemandPullsNewestBucketAndCapsPacing()
{
    ingestAuthority(32);
    ingestIntoBucket(kHighBucket, 8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kHighBucket, 2)); // B lacks kHighBucket's newest two

    meshtastic_MeshPacket inSync;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, inSync));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, inSync));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    for (const auto &p : takeSent()) // the follower may advertise, but nothing was queued to pull
        TEST_ASSERT_NOT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(p).sfpp_message_type);

    meshtastic_MeshPacket behind;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, behind));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, behind));
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(kHighBucket, decodeSfpp(request[0]).chain_count);

    // The one-exchange cap: further adverts while active start nothing.
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, behind));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, inSync));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    TEST_ASSERT_EQUAL_UINT(2, finishPull(follower, FOLLOWER_NODE, request[0]));
    assertConverged(kHighBucket);
}

void enablePullRetry()
{
    portduino_config.sfpp_pull_retry = true;
}

// Drives a pull to a failed exchange with the provide dropped, leaving a remembered hole. The pull
// starts inline from an advert naming the hole, or from the remembered entry after its backoff.
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

// The follower agrees on kBucket and lacks the two newest objects of kHighBucket. `hole` names the
// hole's bucket and always pulls at once; `other` names kBucket, so it only nudges a remembered retry.
void setupHighBucketHole(meshtastic_MeshPacket &hole, meshtastic_MeshPacket &other)
{
    ingestAuthority(32);
    ingestIntoBucket(kHighBucket, 8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kHighBucket, 2));
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, hole));
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, other));
}

// --- Q6 groundwork: failed-pull retry and the shared pacer ---

// A remembered pull obeys the same drain pacer as everything else. Three buckets each lack their
// newest object, held in scratch so each list completes with no item request; the two remembered
// mid-exchange are pulled one drain interval apart, never back to back.
void test_secondAdvertInsideDrainIntervalSendsNoRequest()
{
    ingestIntoBucket(kLowBucket, 2);
    ingestIntoBucket(kBucket, 2);
    ingestIntoBucket(kHighBucket, 2);
    buildFollowerFromCopy();
    for (uint32_t bucket : {kLowBucket, kBucket, kHighBucket})
        TEST_ASSERT_TRUE(deleteNewest(dirB, bucket, 1));
    for (size_t i : {1, 3, 5}) // each bucket's newest object, too young to push
        ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, ingested[i].id, ingested[i].text, 100);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kLowBucket, false, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> first = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, first.size());
    TEST_ASSERT_EQUAL_UINT32(kLowBucket, decodeSfpp(first[0]).chain_count);
    for (uint32_t bucket : {kBucket, kHighBucket}) { // remembered, not pulled, while busy
        TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, bucket, false, advert));
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    }
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, buildProvideFromDir(dirA, kLowBucket)));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // the scratch row filled the only hole

    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> second = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, second.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(second[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(kHighBucket, decodeSfpp(second[0]).chain_count); // newest bucket first

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, buildProvideFromDir(dirA, kHighBucket)));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // inside the 2 s drain interval set in setUp()

    Time::setTestMillis(Time::getMillis() + 2000); // one enumDrainIntervalMs()
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> third = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, third.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(third[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(kBucket, decodeSfpp(third[0]).chain_count);
}

// A lost provide is remembered and retried on a later advert that names a different bucket, after
// the backoff and before a full rotation; a burst of adverts inside the backoff sends nothing.
void test_lostEnumProvideRetriesWithinBackoff()
{
    enablePullRetry();
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);
    failPendingPull(hole);

    for (uint32_t i = 0; i < 6; i++) { // no retry storm inside the 30 s backoff
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
        follower->runOnce();
        TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
    }
    Time::setTestMillis(Time::getMillis() + 29000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    Time::setTestMillis(Time::getMillis() + 1000); // 30 s after the failure, under a 60 s rotation
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retry = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retry.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(retry[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(kHighBucket, decodeSfpp(retry[0]).chain_count); // the hole's bucket, not the advert's

    TEST_ASSERT_EQUAL_UINT(2, finishPull(follower, FOLLOWER_NODE, retry[0]));
    assertConverged(kHighBucket);
}

// W is D19's proposed 24 h service window; a hole older than it is accepted as lost instead of
// being chased by every advert from the peer.
void test_failedPullStopsPastServiceWindow()
{
    enablePullRetry();
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);
    failPendingPull(hole);

    Time::setTestMillis(Time::getMillis() + 24ULL * 60 * 60 * 1000 + 1000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// The attempt count must survive the exchange: the second failure waits 60 s, not another 30 s.
// On 28900150 the entry is erased at launch, so the parent retries at +30 s and this fails.
void test_failedPullBackoffDoublesAcrossFailures()
{
    enablePullRetry();
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);
    failPendingPull(hole); // first failure: 30 s backoff

    Time::setTestMillis(Time::getMillis() + 30000);
    failPendingPull(other); // the remembered entry retries and fails: 60 s backoff

    Time::setTestMillis(Time::getMillis() + 30000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    Time::setTestMillis(Time::getMillis() + 31000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retry = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retry.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(retry[0]).sfpp_message_type);
}

// The content model has no tail pull, so this pins the remembered entry instead: it retries on its
// own timer with no fresh advert and keeps its summary, so a hole the follower holds in scratch
// completes the retried list by promotion, with no item request and no collision probe.
void test_failedTailPullRetriesAsTail()
{
    enablePullRetry();
    ingestAuthority(32);
    ingestIntoBucket(kHighBucket, 8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kHighBucket, 1));
    const IngestedText &newest = ingested.back();
    ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, newest.id, newest.text, 100); // too young to push

    meshtastic_MeshPacket hole;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, hole));
    failPendingPull(hole);

    // No new advert: the remembered entry itself must carry the bucket and its summary.
    Time::setTestMillis(Time::getMillis() + 30000);
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retry = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retry.size());
    TEST_ASSERT_EQUAL_UINT32(kHighBucket, decodeSfpp(retry[0]).chain_count);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, retry[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // the promoted scratch row completed the list
    assertConverged(kHighBucket);
}

// Eight retries (nine failures) park the hole; neither its timer nor an advert for another bucket
// wakes it, while a changed summary for its own bucket pulls again.
// On 3cd7de9e7 the parked entry's attempt state survives the new summary, so the revival fails.
void test_failedPullCapParksUntilSummaryChanges()
{
    enablePullRetry();
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);
    for (uint8_t i = 0; i < 9; i++) {
        if (i > 0)
            Time::setTestMillis(Time::getMillis() + 6 * 60 * 1000); // past the 5 min backoff cap
        failPendingPull(i == 0 ? hole : other);
    }
    Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // parked after the eighth retry

    ingestIntoBucket(kHighBucket, 1);
    meshtastic_MeshPacket changed;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, changed));
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
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);
    failPendingPull(hole);

    ingestIntoBucket(kHighBucket, 1);
    meshtastic_MeshPacket changed;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, changed));
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
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);
    failPendingPull(hole);

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

// A failed pull of kHighBucket's hole, opened by the advert that names it.
void failNamedBucketPull(meshtastic_MeshPacket &advert)
{
    meshtastic_MeshPacket other;
    setupHighBucketHole(advert, other);
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
    TEST_ASSERT_EQUAL_UINT32(kHighBucket, decodeSfpp(again[0]).chain_count);
}

// Item 4: with PullRetry on, a named-mismatch advert also bypasses the backoff; the backoff only
// paces the remembered entry's own timer.
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

// Item 2, first half: tail entries are gone with the chain, so this pins the named equivalent. A
// parked hole stays parked until a write to its own bucket changes that bucket's summary; each write
// re-opens it, so a hole that keeps failing is re-parked and re-opened once per write.
void test_parkedTailHoleRevivesOnEveryWrite()
{
    enablePullRetry();
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);

    for (uint8_t round = 0; round < 2; round++) {
        TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, hole));
        for (uint8_t i = 0; i < 9; i++) {
            if (i > 0)
                Time::setTestMillis(Time::getMillis() + 6 * 60 * 1000); // past the 5 min backoff cap
            failPendingPull(i == 0 ? hole : other);
        }
        Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000);
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, other));
        follower->runOnce();
        TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // parked

        ingestIntoBucket(kHighBucket, 1); // one new write changes the hole bucket's summary
    }
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, hole));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, hole));
    follower->runOnce();
    TEST_ASSERT_EQUAL_UINT(1, takeSent().size()); // revived a second time
}

// Item 2, second half: a revival resets firstFailedMs, so W counts from the last revival, not from
// the hole's first failure. A hole in a bucket that keeps changing is never given up.
void test_revivalRestartsServiceWindow()
{
    enablePullRetry();
    meshtastic_MeshPacket hole;
    meshtastic_MeshPacket other;
    setupHighBucketHole(hole, other);
    failPendingPull(hole); // first failure: W starts here
    const uint32_t firstFailure = Time::getMillis();

    Time::setTestMillis(firstFailure + 23ULL * 60 * 60 * 1000); // inside W
    ingestIntoBucket(kHighBucket, 1);
    meshtastic_MeshPacket changed;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kHighBucket, false, changed));
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
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advert));
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

    // Rolling boundary: still inside 10 min of the first send, so no 31st frame.
    Time::setTestMillis(Time::getMillis() + 9 * 60 * 1000);
    uint32_t blocked = 0;
    for (uint32_t i = 0; i < 3; i++)
        for (const auto &out : drainAuthorityOne(100))
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                blocked++;
    TEST_ASSERT_EQUAL_UINT32(0, blocked);

    // Past 10 min from the first send the window rolls, so a fresh request is served again.
    Time::setTestMillis(Time::getMillis() + 2 * 60 * 1000);
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, wanted[0]));
    uint32_t resumed = 0;
    for (uint32_t i = 0; i < 3; i++)
        for (const auto &out : drainAuthorityOne(100))
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                resumed++;
    TEST_ASSERT_GREATER_THAN_UINT32(0, resumed);
}

// A second listing naming the same missing IDs does not request them again while in flight.
void test_item_in_flight_is_not_requested_twice()
{
    ingestIntoBucket(kBucket, 4);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advert, false, 4));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, request[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provide[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    uint32_t asked = 0;
    for (const auto &out : takeSent())
        if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            asked++;
    TEST_ASSERT_EQUAL_UINT32(1, asked);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    asked = 0;
    for (const auto &out : takeSent())
        if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            asked++;
    TEST_ASSERT_EQUAL_UINT32(0, asked);
}

// G11/F3.8: a single requester gets a per-peer window budget, not the whole global pacer.
void test_fourRequestersShareTheServingWindow()
{
    portduino_config.sfpp_enum_drain_ms = 100;
    ingestAuthority(32);
    std::vector<pinsketch::Element> ids;
    TEST_ASSERT_TRUE(readShortIds(dirA, kBucket, ids));
    TEST_ASSERT_EQUAL_UINT(32, ids.size());
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));

    constexpr NodeNum peers[4] = {0xA0000001, 0xA0000002, 0xA0000003, 0xA0000004};
    for (uint32_t p = 0; p < 4; p++) {
        meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
        msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
        msg.sr_version = sfppsr::kDerivationVersion;
        msg.root_hash.size = 4;
        memcpy(msg.root_hash.bytes, root, 4);
        msg.chain_count = kBucket;
        for (pb_size_t i = 0; i < 8; i++)
            msg.sr_short_ids[i] = ids[p * 8 + i];
        msg.sr_short_ids_count = 8;
        meshtastic_MeshPacket request = meshtastic_MeshPacket_init_zero;
        request.from = peers[p];
        request.to = AUTHORITY_NODE;
        request.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        request.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
        myNodeInfo.my_node_num = AUTHORITY_NODE;
        portduino_config.sfpp_stratum0 = true;
        TEST_ASSERT_TRUE(authority->handleReceivedProtobuf(request, &msg));
    }
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint32_t frames[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < 30; i++) {
        for (const auto &out : drainAuthorityOne(100)) {
            if (decodeSfpp(out).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                continue;
            for (uint32_t p = 0; p < 4; p++)
                if (out.to == peers[p])
                    frames[p]++;
        }
    }
    for (uint32_t p = 0; p < 4; p++)
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(7, frames[p]); // 30 / 4 active requesters
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
    TEST_ASSERT_EQUAL_UINT32(10, readStore(dirA).count); // pushed objects file under their own buckets
}

// G4: a half-received provide ends at the idle gap, so a later advert can start a fresh pull.
void test_lostProvideFrameEndsAtIdle()
{
    ingestAuthority(32);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 2));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    portduino_config.sfpp_stratum0 = false;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());

    // Only frame 0 of 2 arrives for the bucket; the idle gap must end the exchange.
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, buildProvideFromDir(dirA, kBucket, 0, 2)));
    Time::setTestMillis(Time::getMillis() + 31000);
    follower->runOnce();

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> fresh = takeSent();
    uint32_t requests = 0;
    for (const auto &out : fresh)
        if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST)
            requests++;
    TEST_ASSERT_EQUAL_UINT32(1, requests);
}

// F3.4: a requester with frames queued is not evicted by a flood of new request senders.
void test_servedRequesterSurvivesPeerTableFlood()
{
    ingestAuthority(1);
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));
    std::vector<pinsketch::Element> ids;
    TEST_ASSERT_TRUE(readShortIds(dirA, kBucket, ids));
    TEST_ASSERT_TRUE(!ids.empty());

    constexpr NodeNum served = 0xB0000001;
    auto requestFrom = [&](NodeNum node, bool withId) {
        meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
        msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
        msg.sr_version = sfppsr::kDerivationVersion;
        msg.root_hash.size = 4;
        memcpy(msg.root_hash.bytes, root, 4);
        msg.chain_count = kBucket;
        if (withId) {
            msg.sr_short_ids[0] = ids[0];
            msg.sr_short_ids_count = 1;
        } else {
            msg.sr_short_ids[0] = 0xDEADBEEF;
            msg.sr_short_ids_count = 1;
        }
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        mp.from = node;
        mp.to = AUTHORITY_NODE;
        mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
        myNodeInfo.my_node_num = AUTHORITY_NODE;
        portduino_config.sfpp_stratum0 = true;
        TEST_ASSERT_TRUE(authority->handleReceivedProtobuf(mp, &msg));
    };

    requestFrom(served, true); // leaves a frame queued for this peer
    for (uint32_t i = 0; i < 15; i++)
        requestFrom(0xB0000100 + i, false);
    requestFrom(0xB0000200, false); // eviction pressure; the served peer must stay

    for (uint32_t i = 0; i < 9; i++) {
        uint8_t cipher[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        cipher[0] = (uint8_t)i;
        std::vector<meshtastic_StoreForwardPlusPlus> frames;
        TEST_ASSERT_TRUE(sfppframe::split(cipher, sizeof(cipher), NODENUM_BROADCAST, served, 0x9000 + i, 0, false, frames));
        for (const auto &frame : frames) {
            meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
            mp.from = served;
            mp.to = AUTHORITY_NODE;
            mp.channel = 0;
            mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
            mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                         &meshtastic_StoreForwardPlusPlus_msg, &frame);
            TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, true, mp));
        }
    }
    TEST_ASSERT_EQUAL_UINT32(10, readStore(dirA).count); // pushed objects file under their own buckets
}

// --- Stage 8: sketch mode ---

void runSketchCase(uint32_t total, uint32_t missing)
{
    portduino_config.sfpp_repair = SFPP_REPAIR_SKETCH;
    ingestAuthority(total);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, missing));

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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 16));

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
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, true, advert));
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
    TEST_ASSERT_TRUE(deleteIngested(dirB, 1));
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

    // The follower hears five texts the authority did not, in the authority's bucket so the second
    // follower's one exchange covers them.
    PacketId missedId = 0x60000;
    for (uint32_t i = 0; i < 5; i++) {
        char text[24];
        snprintf(text, sizeof(text), "writer missed %u", i);
        missedId = idForBucket(SENDER_NODE, text, kBucket, missedId);
        ingestScratchText(follower, FOLLOWER_NODE, SENDER_NODE, missedId++, text, 301);
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
    TEST_ASSERT_EQUAL_UINT32(6, readBucket(dirA, kBucket).count); // 1 live + 5 pushed

    // The second follower now converges through the ordinary exchange.
    Exchange second = runExchangeFor(follower2, FOLLOWER2_NODE);
    TEST_ASSERT_EQUAL_UINT(1, second.itemRequests);
    TEST_ASSERT_EQUAL_UINT(5, second.itemFrames);

    assertConverged(kBucket, dirC);
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

// --- Q6 stage 5: broadcast listing ---

// Counts the packets of one SF++ type, optionally only those naming `bucket`.
uint32_t countType(const std::vector<meshtastic_MeshPacket> &sent, meshtastic_StoreForwardPlusPlus_SFPP_message_type type,
                   int64_t bucket = -1)
{
    uint32_t n = 0;
    for (const auto &p : sent) {
        const meshtastic_StoreForwardPlusPlus msg = decodeSfpp(p);
        if (msg.sfpp_message_type == type && (bucket < 0 || msg.chain_count == (uint32_t)bucket))
            n++;
    }
    return n;
}

// Serves queued item frames from the authority and hands each to every listed archive that it is
// addressed to (a broadcast frame reaches them all). Returns the frames sent.
uint32_t serveItemsTo(std::vector<std::pair<TestSfppModule *, NodeNum>> hearers)
{
    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 64; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            for (auto &hearer : hearers)
                if (frame.to == NODENUM_BROADCAST || frame.to == hearer.second)
                    TEST_ASSERT_TRUE(deliver(hearer.first, hearer.second, false, frame));
            frames++;
        }
    }
    return frames;
}

// One broadcast listing serves every archive that hears it. B pulls kBucket from A; C, behind on
// the same bucket, never heard A's advert but hears B's request and A's listing. The request is
// broadcast and names A, so C does not answer it; the listing is broadcast, so C compares it with
// its own bucket and asks A for the missing items only, never sending an ENUM_REQUEST of its own.
// Both converge.
void test_broadcast_listing_serves_all_hearers()
{
    ingestIntoBucket(kLowBucket, 1);
    ingestAuthority(20);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 3));
    TEST_ASSERT_TRUE(deleteNewest(dirC, kBucket, 3));

    // C has heard A before, on a bucket where they agree, so it knows A but has nothing to pull.
    meshtastic_MeshPacket lowAdvert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kLowBucket, false, lowAdvert));
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, lowAdvert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, request[0].to);
    TEST_ASSERT_EQUAL_UINT32(AUTHORITY_NODE, decodeSfpp(request[0]).encapsulated_to);

    // C overhears the request; it is not the named archive, so it serves nothing.
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, request[0]));
    Time::setTestMillis(Time::getMillis() + 2000);
    myNodeInfo.my_node_num = FOLLOWER2_NODE;
    follower2->runOnce();
    TEST_ASSERT_EQUAL_UINT32(0, countType(takeSent(), meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE));

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, false, request[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provide[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, provide[0].to);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, provide[0]));
    std::vector<meshtastic_MeshPacket> bItems = takeSent();
    TEST_ASSERT_EQUAL_UINT32(1, countType(bItems, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST));

    // C hears the same listing outside any exchange of its own and pulls on its next slot.
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, false, provide[0]));
    std::vector<meshtastic_MeshPacket> cSent = takeSent();
    Time::setTestMillis(Time::getMillis() + 2000);
    myNodeInfo.my_node_num = FOLLOWER2_NODE;
    follower2->runOnce();
    for (const auto &out : takeSent())
        cSent.push_back(out);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, countType(cSent, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST),
                                     "an archive that heard the listing must not ask for it again");
    TEST_ASSERT_EQUAL_UINT32(1, countType(cSent, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, kBucket));
    meshtastic_MeshPacket cItem;
    for (const auto &out : cSent)
        if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            cItem = out;
    TEST_ASSERT_EQUAL_UINT32(3, decodeSfpp(cItem).sr_short_ids_count);
    TEST_ASSERT_EQUAL_UINT32(AUTHORITY_NODE, cItem.to);

    for (const auto &out : bItems)
        TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, false, out));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, false, cItem));
    TEST_ASSERT_EQUAL_UINT32(6, serveItemsTo({{follower, FOLLOWER_NODE}, {follower2, FOLLOWER2_NODE}}));

    assertConverged(kBucket, dirB);
    assertConverged(kBucket, dirC);
}

// A listing overheard while this archive had its own pull of that bucket queued replaces the
// request. C is busy pulling kLowBucket when A's kBucket advert arrives, so kBucket waits as a
// pending pull. C then overhears A's kBucket listing sent for another archive. When C's kLowBucket
// exchange ends, the pending pull asks A for the one missing item and sends no ENUM_REQUEST.
void test_overheard_listing_suppresses_own_request()
{
    ingestIntoBucket(kLowBucket, 2);
    ingestIntoBucket(kBucket, 5);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kLowBucket, 1));
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));

    meshtastic_MeshPacket lowAdvert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kLowBucket, false, lowAdvert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, lowAdvert));
    std::vector<meshtastic_MeshPacket> lowRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, lowRequest.size());
    TEST_ASSERT_EQUAL_UINT32(kLowBucket, decodeSfpp(lowRequest[0]).chain_count);

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildAdvertFromDir(dirA, kBucket, false, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // busy: kBucket waits as a pending pull

    meshtastic_MeshPacket overheard = buildProvideFromDir(dirA, kBucket);
    overheard.to = NODENUM_BROADCAST; // A's listing for some other archive
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, overheard));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    // Finish the kLowBucket exchange.
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, false, lowRequest[0]));
    std::vector<meshtastic_MeshPacket> lowProvide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, lowProvide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, false, lowProvide[0]));
    std::vector<meshtastic_MeshPacket> lowItems = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, lowItems.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, false, lowItems[0]));
    TEST_ASSERT_EQUAL_UINT32(1, serveItemsTo({{follower, FOLLOWER_NODE}}));
    assertConverged(kLowBucket);

    // The pending kBucket pull runs on the next slot from the overheard listing.
    std::vector<meshtastic_MeshPacket> sent;
    for (uint32_t i = 0; i < 3; i++) {
        Time::setTestMillis(Time::getMillis() + 2000);
        myNodeInfo.my_node_num = FOLLOWER_NODE;
        follower->runOnce();
        for (const auto &out : takeSent())
            sent.push_back(out);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, countType(sent, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, kBucket),
                                     "the overheard listing must replace our own request");
    TEST_ASSERT_EQUAL_UINT32(1, countType(sent, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, kBucket));
    for (const auto &out : sent)
        if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, false, out));
    TEST_ASSERT_EQUAL_UINT32(1, serveItemsTo({{follower, FOLLOWER_NODE}}));
    assertConverged(kBucket);
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
    RUN_TEST(test_item_in_flight_is_not_requested_twice);
    RUN_TEST(test_windowBudgetCapsServedFrames);
    RUN_TEST(test_lostProvideFrameEndsAtIdle);
    RUN_TEST(test_fourRequestersShareTheServingWindow);
    RUN_TEST(test_knownPeerTableIsLeastRecentlyUsed);
    RUN_TEST(test_servedRequesterSurvivesPeerTableFlood);
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
    RUN_TEST(test_broadcast_listing_serves_all_hearers);
    RUN_TEST(test_overheard_listing_suppresses_own_request);
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
