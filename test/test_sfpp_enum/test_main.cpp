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
#include "platform/portduino/PortduinoGlue.h"
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

// Ingests a live text as `node`, committed to the canonical store rather than scratch.
meshtastic_MeshPacket ingestOn(TestSfppModule *module, NodeNum node, PacketId id, const char *text)
{
    meshtastic_MeshPacket mp = makeText(SENDER_NODE, id, text, getValidTime(RTCQualityNTP, true));
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    myNodeInfo.my_node_num = node;
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
    if (!readMappingRoot(dir, root))
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

// Builds a whole-window advert from everything a directory holds, so a test can choose the root
// width without waiting for the full-root cadence.
bool buildWindowAdvertFromDir(const std::string &dir, bool fullRoot, meshtastic_MeshPacket &out)
{
    uint8_t root[SFPP_HASH_SIZE];
    if (!readRoot(dir, root))
        return false;

    sfppsr::BucketSummary summary(sfppsr::kMaxSketchCapacity);
    sqlite3 *db = nullptr;
    if (!openDb(dir, &db))
        return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "select short_id, checksum from channel_messages where short_id is not null;", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW)
            summary.add((pinsketch::Element)sqlite3_column_int64(stmt, 0), (uint64_t)sqlite3_column_int64(stmt, 1));
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);

    sfppsr::AdvertScope storeScope;
    storeScope.version = sfppsr::kAdvertSchemaVersion;
    storeScope.retentionHours = 48;
    storeScope.windowEighths = 0;
    storeScope.storeSize = (uint16_t)summary.count();

    sfppsr::Scope scope;
    scope.rootPrefixLen = fullRoot ? SFPP_HASH_SIZE : 4;
    memcpy(scope.rootPrefix, root, scope.rootPrefixLen);
    scope.bucket = sfppsr::kWholeSetBucket;

    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildWindowAdvert(scope, storeScope, summary, msg))
        return false;

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

// A full-root advert naming an arbitrary root, used to prove a foreign root is ignored and our
// own is accepted.
bool buildWindowAdvertWithRoot(const uint8_t root[SFPP_HASH_SIZE], meshtastic_MeshPacket &out)
{
    sfppsr::BucketSummary summary(4);
    summary.add(0x1234, 0x5678);

    sfppsr::AdvertScope storeScope;
    storeScope.version = sfppsr::kAdvertSchemaVersion;
    storeScope.retentionHours = 48;
    storeScope.windowEighths = 0;
    storeScope.storeSize = (uint16_t)summary.count();

    sfppsr::Scope scope;
    scope.rootPrefixLen = SFPP_HASH_SIZE;
    memcpy(scope.rootPrefix, root, SFPP_HASH_SIZE);
    scope.bucket = sfppsr::kWholeSetBucket;

    meshtastic_StoreForwardPlusPlus msg;
    if (!sfppsr::buildWindowAdvert(scope, storeScope, summary, msg))
        return false;

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
    follower = new TestSfppModule();
}

void buildSecondFollowerFromCopy()
{
    TEST_ASSERT_TRUE(copyDb(dirA, dirC));
    portduino_config.sfpp_db_path = dirC + "/";
    follower2 = new TestSfppModule();
}

bool deliver(TestSfppModule *target, NodeNum targetNode, const meshtastic_MeshPacket &p)
{
    myNodeInfo.my_node_num = targetNode;
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

Exchange runExchangeFor(TestSfppModule *requester, NodeNum node, const meshtastic_MeshPacket *advertOverride = nullptr)
{
    Exchange result;
    meshtastic_MeshPacket advert = advertOverride ? *advertOverride : runAuthorityOnce();

    TEST_ASSERT_TRUE(deliver(requester, node, advert));
    result.followerSends = takeSent();
    if (result.followerSends.empty())
        return result; // in sync, nothing to pull

    TEST_ASSERT_EQUAL_UINT(1, result.followerSends.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST,
                      decodeSfpp(result.followerSends[0]).sfpp_message_type);
    result.enumRequests = (uint32_t)result.followerSends.size();

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, result.followerSends[0]));
    std::vector<meshtastic_MeshPacket> provides = drainAuthorityOne(2000); // the provide is queued too
    TEST_ASSERT_EQUAL_UINT(1, provides.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provides[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(requester, node, provides[0]));
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    for (const auto &request : requests)
        if (decodeSfpp(request).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            result.itemRequests++;
    TEST_ASSERT_EQUAL_UINT(1, result.itemRequests);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, requests[0]));
    // The authority queues the item frames and answers runOnce one frame at a time, so a large
    // request never fills the radio's 16-entry TX queue.
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    for (uint32_t guard = 0; guard < 512; guard++) {
        Time::setTestMillis(Time::getMillis() + 2000); // one drain interval
        myNodeInfo.my_node_num = AUTHORITY_NODE;
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
            TEST_ASSERT_TRUE(deliver(requester, node, frame));
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
    TEST_ASSERT_TRUE(deliver(requester, requesterNode, advert));
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, requests[0]));

    std::vector<meshtastic_MeshPacket> provides = drainAuthorityOne(2000); // the provide is queued
    TEST_ASSERT_EQUAL_UINT(1, provides.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provides[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(requester, requesterNode, provides[0]));

    std::vector<meshtastic_MeshPacket> wanted = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, wanted.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, wanted[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // queued, not sent in one burst
}

std::vector<meshtastic_MeshPacket> drainAuthorityOne(uint32_t advanceMs)
{
    Time::setTestMillis(Time::getMillis() + advanceMs);
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    testRouter->sent.clear();
    authority->runOnce();
    return takeSent();
}

bool loadSfppYaml(const std::string &body)
{
    char path[] = "/tmp/meshtastic-sfpp-enum-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0)
        return false;
    const ssize_t written = write(fd, body.data(), body.size());
    close(fd);
    const bool loaded = written == (ssize_t)body.size() && loadConfig(path);
    unlink(path);
    return loaded;
}

std::string sfppBlock(const std::string &yaml)
{
    const size_t begin = yaml.find("StoreAndForward:");
    if (begin == std::string::npos)
        return "";
    const size_t end = yaml.find("\nGeneral:", begin);
    return yaml.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

} // namespace

void ingestScratchText(const std::string &dir, NodeNum from, PacketId id, const char *text);

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
    portduino_config.sfpp_hops = 3;
    portduino_config.sfpp_steal_port = false;
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    portduino_config.sfpp_enum_interval = 1;
    portduino_config.sfpp_enum_drain_ms = 2000;
    portduino_config.sfpp_scratch_sweep = 5;
    portduino_config.sfpp_item_transport = SFPP_ITEM_DIRECT; // existing cases drive direct exchanges
    portduino_config.sfpp_enum_listen_only = false;
    portduino_config.sfpp_pull_retry = false;
    portduino_config.sfpp_max_ids_per_frame = 40;
    portduino_config.sfpp_item_body_max = 168;
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
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
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
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
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
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
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
    msg.bucket = 0;
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

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, mp));
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
    msg.bucket = 0;
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
    msg.bucket = kBucket; // the populated bucket, so only the ID is unknown
    msg.sr_short_ids[0] = 0xDEADBEEF;
    msg.sr_short_ids_count = 1;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = FOLLOWER_NODE;
    mp.to = AUTHORITY_NODE;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                 &meshtastic_StoreForwardPlusPlus_msg, &msg);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, mp));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
}

// --- advert and drain robustness ---

// The advert carries only the 4-byte root prefix.
void test_advertUsesFourByteRootPrefix()
{
    ingestAuthority(4);
    buildFollowerFromCopy();

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_EQUAL_UINT(4, decodeSfpp(advert).root_hash.size);
}

// A queued frame goes on the next drain tick without waiting out a full interval.
// The first item frame goes on the next paced drain step, sharing the one last-send
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

// A rejected send leaves the frame queued and the next drain sends it.
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
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, retry[0]));
    assertConverged();
}

// A lost ITEM_FRAME is recovered by the bounded item retry.
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
    testRouter->sent.clear();
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> retries = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, retries.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(retries[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, retries[0]));
    std::vector<meshtastic_MeshPacket> frame = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, frame.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, frame[0]));
    assertConverged();
}

// Two requesters each get their frames; the per-requester queues are served round-robin.
void test_twoRequestersBothComplete()
{
    ingestAuthority(4);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteIngested(dirB, 3));
    TEST_ASSERT_TRUE(deleteIngested(dirC, 4));

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> bRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bRequest.size());
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, advert));
    std::vector<meshtastic_MeshPacket> cRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, cRequest.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, bRequest[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, cRequest[0]));
    // Both provides are queued and drained round-robin, one per step.
    std::vector<meshtastic_MeshPacket> bProvide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, bProvide.size());
    TEST_ASSERT_EQUAL(NODENUM_BROADCAST, bProvide[0].to); // the listing is broadcast; requester order is kept
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(bProvide[0]).sfpp_message_type);
    std::vector<meshtastic_MeshPacket> cProvide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, cProvide.size());
    TEST_ASSERT_EQUAL(NODENUM_BROADCAST, cProvide[0].to);
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(cProvide[0]).sfpp_message_type);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, bProvide[0]));
    std::vector<meshtastic_MeshPacket> bItem = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bItem.size());
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, cProvide[0]));
    std::vector<meshtastic_MeshPacket> cItem = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, cItem.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, bItem[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, cItem[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint32_t bFrames = 0;
    uint32_t cFrames = 0;
    for (uint32_t guard = 0; guard < 8 && (bFrames == 0 || cFrames == 0); guard++) {
        std::vector<meshtastic_MeshPacket> sent = drainAuthorityOne(2000);
        for (const auto &frame : sent) {
            if (decodeSfpp(frame).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                continue; // the advert that follows the drained queues
            if (frame.to == FOLLOWER_NODE) {
                TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, frame));
                bFrames++;
            } else if (frame.to == FOLLOWER2_NODE) {
                TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, frame));
                cFrames++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(1, bFrames);
    TEST_ASSERT_EQUAL_UINT(1, cFrames);

    assertConverged(kBucket, dirB);
    assertConverged(kBucket, dirC);
}

// --- wrong sketch decode ---

// --- send and advert robustness ---

// sendToMesh may free the packet during send; the module must not touch it afterwards. Run
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

// A blank follower now derives the channel root itself, so a full-root advert naming that same
// root lets it join and pull normally.
void test_blankFollowerPullsFromFullRootAdvert()
{
    ingestAuthority(3);

    // A fresh store: no rows and no root.
    portduino_config.sfpp_db_path = dirB + "/";
    follower = new TestSfppModule();

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildWindowAdvertFromDir(dirA, true, advert));
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, decodeSfpp(advert).root_hash.size);
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));

    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(requests[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, requests[0]));

    std::vector<meshtastic_MeshPacket> provides = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provides.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, provides[0]));

    std::vector<meshtastic_MeshPacket> items = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, items.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, items[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 32; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, frame));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(3, frames);
    assertConverged();
}

// At production pacing (20 s) with two requesters, a follower missing 20 converges,
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
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> bRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bRequest.size());
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, advert));
    std::vector<meshtastic_MeshPacket> cRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, cRequest.size());

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, bRequest[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, cRequest[0]));

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
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, provide[0]));
        for (const auto &out : takeSent())
            bItem.push_back(out);
        TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, provide[0]));
        for (const auto &out : takeSent())
            cItem.push_back(out);
    }
    TEST_ASSERT_EQUAL_UINT(1, bItem.size());
    TEST_ASSERT_EQUAL_UINT(1, cItem.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(bItem[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(cItem[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT(20, (uint32_t)decodeSfpp(bItem[0]).sr_short_ids_count);
    TEST_ASSERT_EQUAL_UINT(4, (uint32_t)decodeSfpp(cItem[0]).sr_short_ids_count);
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, bItem[0]));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, cItem[0]));

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
                TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, frame));
                bFrames++;
            } else if (frame.to == FOLLOWER2_NODE) {
                TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, frame));
                cFrames++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(20, bFrames); // no retry-induced duplicates
    TEST_ASSERT_EQUAL_UINT(4, cFrames);

    assertConverged(kBucket, dirB);
    assertConverged(kBucket, dirC);
}

// Broadcast item frames serve a listener that never requests; a
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
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> bRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, bRequest.size());

    portduino_config.sfpp_enum_listen_only = true;
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // listen-only: learns the authority, sends nothing
    portduino_config.sfpp_enum_listen_only = false;

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, bRequest[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, provide[0]));
    std::vector<meshtastic_MeshPacket> itemRequest = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, itemRequest.size());
    portduino_config.sfpp_enum_listen_only = true;
    TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, provide[0]));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
    portduino_config.sfpp_enum_listen_only = false;
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, itemRequest[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 64; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, frame.to);
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, frame));
            portduino_config.sfpp_enum_listen_only = true;
            TEST_ASSERT_TRUE(deliver(follower2, FOLLOWER2_NODE, frame));
            TEST_ASSERT_EQUAL_UINT(0, takeSent().size()); // still sends nothing
            portduino_config.sfpp_enum_listen_only = false;
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(20, frames);

    assertConverged(kBucket, dirB);
    assertConverged(kBucket, dirC);
}

// Texts heard live sit in scratch; promote them instead of re-fetching.
void test_heardLiveObjectsArePromoted()
{
    ingestAuthority(20);
    TEST_ASSERT_TRUE(copyDb(dirA, dirB));
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket)); // canon empty, root retained
    TEST_ASSERT_EQUAL_UINT32(0, readStore(dirB).count);
    portduino_config.sfpp_db_path = dirB + "/";
    follower = new TestSfppModule();

    for (uint32_t i = 0; i < 15; i++) // the first 15 of the authority's 20 texts, heard live
        ingestScratchText(dirB, SENDER_NODE, ingested[i].id, ingested[i].text);

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);
    TEST_ASSERT_EQUAL_UINT(5, exchange.itemFrames); // the 15 scratch rows are not re-fetched
    assertConverged();
}

// --- rising tips pull once ---

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
    follower = new TestSfppModule();
    TEST_ASSERT_TRUE(
        insertScratchRow(dirB, NODENUM_BROADCAST, SENDER_NODE, held.id, held.cipher, sizeof(held.cipher), held.hash));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemFrames);
    TEST_ASSERT_EQUAL_UINT(0, countRows(dirB, held.id));          // unconfirmed promotion removed
    TEST_ASSERT_EQUAL_UINT(1, countRows(dirB, missingObject.id)); // peer's object fetched
    assertConverged();
}

// --- listen-only silence ---

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
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, request));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE, decodeSfpp(provide[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(requester, node, provide[0]));
    std::vector<meshtastic_MeshPacket> item = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, item.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, item[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 16; guard++) {
        std::vector<meshtastic_MeshPacket> out = drainAuthorityOne(2000);
        if (out.empty())
            continue;
        if (decodeSfpp(out[0]).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
            break;
        for (const auto &frame : out) {
            TEST_ASSERT_TRUE(deliver(requester, node, frame));
            frames++;
        }
    }
    return frames;
}

// --- advert rotation ---

// One whole-window advert covers a changed bucket along with the rest, so a follower behind in it
// catches up on the next advert rather than after a rotation turn.
void test_whole_window_advert_covers_a_changed_bucket()
{
    ingestIntoBucket(kHighBucket, 30);
    ingestAuthority(8);
    buildFollowerFromCopy(); // B has both buckets
    ingestAuthority(2);      // A's kBucket gains two objects (now 10)

    meshtastic_MeshPacket advert = runAuthorityOnce();
    const meshtastic_StoreForwardPlusPlus advertMsg = decodeSfpp(advert);
    TEST_ASSERT_EQUAL_UINT(0, advertMsg.sr_version);
    TEST_ASSERT_EQUAL_UINT32(40, advertMsg.sr_count); // both buckets in one advert

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));

    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, request.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(request[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(sfppsr::kWholeSetBucket, decodeSfpp(request[0]).bucket);

    TEST_ASSERT_EQUAL_UINT(2, finishPull(follower, FOLLOWER_NODE, request[0]));
    TEST_ASSERT_EQUAL_UINT32(10, readBucket(dirA, kBucket).count);
    assertConverged(kBucket);
}

void enablePullRetry()
{
    portduino_config.sfpp_pull_retry = true;
}

// --- failed-pull retry and the shared pacer ---

// --- Retry semantics still open ---
// These pin today's behaviour, not a decision. Whichever way Tom settles each item, the matching
// assertion is meant to change, so the change is deliberate and visible.

// --- pacer and per-peer budget ---

// The one pacer has a 30-frame/10-min window across all sends, not just drain spacing.
void test_windowBudgetCapsServedFrames()
{
    portduino_config.sfpp_enum_drain_ms = 100;
    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteBucket(dirB, kBucket));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildWindowAdvertFromDir(dirA, false, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    follower->runOnce();
    std::vector<meshtastic_MeshPacket> initial = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, initial.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, initial[0]));
    std::vector<meshtastic_MeshPacket> provide = drainAuthorityOne(100);
    TEST_ASSERT_EQUAL_UINT(1, provide.size());
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, provide[0]));
    std::vector<meshtastic_MeshPacket> wanted = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, wanted.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, wanted[0]));

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
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, wanted[0]));
    uint32_t resumed = 0;
    for (uint32_t i = 0; i < 3; i++)
        for (const auto &out : drainAuthorityOne(100))
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                resumed++;
    TEST_ASSERT_GREATER_THAN_UINT32(0, resumed);
}

// A single requester gets a per-peer window budget, not the whole global pacer.
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
        msg.bucket = kBucket;
        for (pb_size_t i = 0; i < 8; i++)
            msg.sr_short_ids[i] = ids[p * 8 + i];
        msg.sr_short_ids_count = 8;
        meshtastic_MeshPacket request = meshtastic_MeshPacket_init_zero;
        request.from = peers[p];
        request.to = AUTHORITY_NODE;
        request.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        request.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
        myNodeInfo.my_node_num = AUTHORITY_NODE;
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

void requestOneItemFrom(NodeNum peer, const uint8_t *root, pinsketch::Element id)
{
    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = 4;
    memcpy(msg.root_hash.bytes, root, 4);
    msg.bucket = kBucket;
    msg.sr_short_ids[0] = id;
    msg.sr_short_ids_count = 1;
    meshtastic_MeshPacket request = meshtastic_MeshPacket_init_zero;
    request.from = peer;
    request.to = AUTHORITY_NODE;
    request.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    request.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    myNodeInfo.my_node_num = AUTHORITY_NODE;
    TEST_ASSERT_TRUE(authority->handleReceivedProtobuf(request, &msg));
}

bool drainReaches(NodeNum peer, uint32_t rounds)
{
    for (uint32_t i = 0; i < rounds; i++) {
        for (const auto &out : drainAuthorityOne(100)) {
            if (out.to == peer &&
                decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                return true;
        }
    }
    return false;
}

// A full table of idle (empty-queue) requesters yields one slot to a newcomer.
void test_fullIdleRequesterTableServesNewcomer()
{
    portduino_config.sfpp_enum_drain_ms = 100;
    ingestAuthority(32);
    std::vector<pinsketch::Element> ids;
    TEST_ASSERT_TRUE(readShortIds(dirA, kBucket, ids));
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));

    constexpr NodeNum first = 0xB0000001;
    for (uint32_t i = 0; i < 16; i++)
        requestOneItemFrom(first + i, root, ids[0]);
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    // Drain every queued frame so the requesters really are idle, not just quiet.
    for (uint32_t i = 0; i < 20; i++)
        drainAuthorityOne(100);

    Time::setTestMillis(Time::getMillis() + 2 * 100); // two drain intervals of idleness
    const NodeNum newcomer = first + 16;
    requestOneItemFrom(newcomer, root, ids[0]);
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    TEST_ASSERT_TRUE(drainReaches(newcomer, 20));
}

// A requester with frames still queued is busy even after a quiet drain interval: a newcomer
// must not take its slot, and every queued frame is still delivered.
void test_quietRequesterWithQueuedFramesKeepsItsSlot()
{
    portduino_config.sfpp_enum_drain_ms = 100;
    ingestAuthority(32);
    std::vector<pinsketch::Element> ids;
    TEST_ASSERT_TRUE(readShortIds(dirA, kBucket, ids));
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));

    constexpr NodeNum first = 0xD0000001;
    requestOneItemFrom(first, root, ids[0]);
    requestOneItemFrom(first, root, ids[1]); // a second frame still waiting its turn
    for (uint32_t i = 1; i < 16; i++)
        requestOneItemFrom(first + i, root, ids[0]);
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    Time::setTestMillis(Time::getMillis() + 2 * 100); // the whole table is quiet for a drain interval
    const NodeNum newcomer = first + 16;
    requestOneItemFrom(newcomer, root, ids[0]);
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint32_t victimFrames = 0;
    bool newcomerServed = false;
    for (uint32_t window = 0; window < 2; window++) {
        for (uint32_t i = 0; i < 32; i++) {
            for (const auto &out : drainAuthorityOne(100)) {
                if (decodeSfpp(out).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                    continue;
                if (out.to == newcomer)
                    newcomerServed = true;
                if (out.to == first)
                    victimFrames++;
            }
        }
        if (window == 0)
            Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000); // the next serving window
    }

    TEST_ASSERT_FALSE(newcomerServed);
    TEST_ASSERT_EQUAL_UINT32(2, victimFrames);
}

// While every slot is active a newcomer is refused and no requester loses its slot.
void test_fullActiveRequesterTableKeepsSlots()
{
    portduino_config.sfpp_enum_drain_ms = 100;
    ingestAuthority(32);
    std::vector<pinsketch::Element> ids;
    TEST_ASSERT_TRUE(readShortIds(dirA, kBucket, ids));
    uint8_t root[SFPP_HASH_SIZE];
    TEST_ASSERT_TRUE(readRoot(dirA, root));

    constexpr NodeNum first = 0xC0000001;
    for (uint32_t i = 0; i < 16; i++)
        requestOneItemFrom(first + i, root, ids[0]);

    const NodeNum newcomer = first + 16;
    requestOneItemFrom(newcomer, root, ids[0]); // all 16 were heard from just now
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    uint32_t served = 0;
    bool newcomerServed = false;
    for (uint32_t i = 0; i < 30; i++) {
        for (const auto &out : drainAuthorityOne(100)) {
            if (decodeSfpp(out).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                continue;
            if (out.to == newcomer)
                newcomerServed = true;
            for (uint32_t p = 0; p < 16; p++)
                if (out.to == first + p)
                    served++;
        }
    }
    TEST_ASSERT_FALSE(newcomerServed);
    TEST_ASSERT_EQUAL_UINT32(16, served);
}

// --- sketch mode ---

// --- writer-missed recovery ---

// Seeds a scratch row directly; live text now goes to the canonical store.
void ingestScratchText(const std::string &dir, NodeNum from, PacketId id, const char *text)
{
    meshtastic_MeshPacket mp = makeText(from, id, text, getValidTime(RTCQualityNTP, true));
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    uint8_t hash[SFPP_HASH_SIZE];
    sfppframe::messageHash(enc.encrypted.bytes, enc.encrypted.size, mp.to, mp.from, id, hash);
    TEST_ASSERT_TRUE(insertScratchRow(dir, mp.to, from, id, enc.encrypted.bytes, enc.encrypted.size, hash));
}

void test_two_archives_derive_the_same_root_without_stratum0()
{
    portduino_config.sfpp_db_path = dirB + "/";
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
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;

    uint8_t foreign[SFPP_HASH_SIZE];
    memset(foreign, 0xAB, sizeof(foreign));
    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildWindowAdvertWithRoot(foreign, advert));

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    follower->runOnce();

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, takeSent().size(), "a foreign full-root advert must not start a pull");

    uint8_t mapped[SFPP_HASH_SIZE] = {0};
    if (readMappingRoot(dirB, mapped))
        TEST_ASSERT_TRUE_MESSAGE(memcmp(mapped, foreign, SFPP_HASH_SIZE) != 0, "the foreign root must not be adopted");
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
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    follower->runOnce(); // derive the root
    takeSent();

    uint8_t ours[SFPP_HASH_SIZE] = {0};
    TEST_ASSERT_TRUE(readMappingRoot(dirB, ours));
    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildWindowAdvertWithRoot(ours, advert));

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);
}

// A full-root advert naming our own derived root is the same scope: accept it and pull.
void test_full_root_advert_with_our_root_is_pulled()
{
    portduino_config.sfpp_db_path = dirB + "/";
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    follower = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER_NODE;
    follower->runOnce();

    uint8_t ours[SFPP_HASH_SIZE] = {0};
    TEST_ASSERT_TRUE_MESSAGE(readMappingRoot(dirB, ours), "the follower must have derived a root");

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildWindowAdvertWithRoot(ours, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    follower->runOnce();

    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(sfppsr::kWholeSetBucket, decodeSfpp(sent[0]).bucket);
}

// --- broadcast listing ---

// Counts the packets of one SF++ type, optionally only those naming `bucket`.
uint32_t countType(const std::vector<meshtastic_MeshPacket> &sent, meshtastic_StoreForwardPlusPlus_SFPP_message_type type,
                   int64_t bucket = -1)
{
    uint32_t n = 0;
    for (const auto &p : sent) {
        const meshtastic_StoreForwardPlusPlus msg = decodeSfpp(p);
        if (msg.sfpp_message_type == type && (bucket < 0 || msg.bucket == (uint32_t)bucket))
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
                    TEST_ASSERT_TRUE(deliver(hearer.first, hearer.second, frame));
            frames++;
        }
    }
    return frames;
}

// --- no authority ---

// Runs one archive's tick as that node and returns what it sent.
std::vector<meshtastic_MeshPacket> tick(TestSfppModule *module, NodeNum node, uint32_t advanceMs = 2000)
{
    Time::setTestMillis(Time::getMillis() + advanceMs);
    myNodeInfo.my_node_num = node;
    testRouter->sent.clear();
    module->runOnce();
    return takeSent();
}

struct Archive {
    TestSfppModule *module;
    NodeNum node;
};

// Ingests one text into `module`'s store, in `bucket`, with a fresh packet ID.
void ingestUnique(TestSfppModule *module, NodeNum node, uint32_t bucket, const char *text)
{
    IngestedText t;
    snprintf(t.text, sizeof(t.text), "%s", text);
    t.id = idForBucket(SENDER_NODE, t.text, bucket, nextTextId);
    t.bucket = bucket;
    nextTextId = t.id + 1;
    ingestOn(module, node, t.id, t.text);
    ingested.push_back(t);
}

// Ticks every archive in turn and hands each on-air packet to the others. Replies a receiver sends
// immediately are queued and delivered too, as a shared radio would carry them.
void runArchiveNetwork(const std::vector<Archive> &archives, uint32_t rounds)
{
    for (uint32_t round = 0; round < rounds; round++) {
        for (const auto &sender : archives) {
            std::vector<meshtastic_MeshPacket> pending = tick(sender.module, sender.node);
            for (size_t i = 0; i < pending.size() && i < 4096; i++) {
                const meshtastic_MeshPacket p = pending[i];
                for (const auto &hearer : archives) {
                    if (hearer.node == p.from)
                        continue;
                    deliver(hearer.module, hearer.node, p);
                    for (const auto &reply : takeSent())
                        pending.push_back(reply);
                }
            }
        }
    }
}

void assertSameStore(const std::string &one, const std::string &two)
{
    const BucketView a = readStore(one);
    const BucketView b = readStore(two);
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    TEST_ASSERT_EQUAL_UINT64(a.checksum, b.checksum);
}

// A hash-verified broadcast item frame carrying (from, id, cipher), sent by `sender`.
meshtastic_MeshPacket itemFrameFrom(NodeNum sender, PacketId id, const uint8_t *cipher, size_t len, uint8_t channel = 0)
{
    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    TEST_ASSERT_TRUE(sfppframe::split(cipher, len, NODENUM_BROADCAST, SENDER_NODE, id, 0, false, frames));
    TEST_ASSERT_EQUAL_UINT(1, frames.size());
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = sender;
    mp.to = NODENUM_BROADCAST;
    mp.channel = channel;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
    mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                 &meshtastic_StoreForwardPlusPlus_msg, &frames[0]);
    return mp;
}

// The follower is pulling the newest kBucket object from A and has asked for it. The item frame
// that answers arrives from another archive (UNKNOWN_NODE, which holds the same object and is
// serving someone else), and is stored: an object is accepted from any peer when it is one this
// archive asked for. An object nobody asked for, from the same peer, is still refused.
void test_objectFromAnyPeerIsAccepted()
{
    ingestAuthority(3);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));
    driveToItemRequest(follower, FOLLOWER_NODE);

    std::vector<meshtastic_MeshPacket> frames = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, frames.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME, decodeSfpp(frames[0]).sfpp_message_type);
    meshtastic_MeshPacket fromOther = frames[0];
    fromOther.from = UNKNOWN_NODE;
    fromOther.to = NODENUM_BROADCAST;
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, fromOther));
    assertConverged();

    const uint8_t cipher[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, itemFrameFrom(UNKNOWN_NODE, 0x7100, cipher, sizeof(cipher))));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, readStore(dirB).count, "an object nobody asked for must not be stored");
}

// The derived root belongs to the primary channel, so an item frame that arrived on any other
// channel is not filed under it, even from the exchange peer with a wanted ID. The same frame on
// the primary channel is stored.
void test_itemFrameOnAnotherChannelIsRefused()
{
    ingestAuthority(3);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));
    driveToItemRequest(follower, FOLLOWER_NODE);

    std::vector<meshtastic_MeshPacket> frames = drainAuthorityOne(2000);
    TEST_ASSERT_EQUAL_UINT(1, frames.size());
    meshtastic_MeshPacket onSecondary = frames[0];
    onSecondary.channel = 1;
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, onSecondary));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, readBucket(dirB, kBucket).count, "a frame from another channel must be refused");

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, frames[0]));
    assertConverged();
}

// A text that one archive heard and A missed reaches A through the ordinary advert and pull, with
// no push. The follower writes the text canonically and advertises kBucket; A pulls it from the
// follower. Mid-pull, A hears a third archive advertise a bucket they agree on; that advert names
// no leader, so the follower's item frames are still accepted.
void test_writer_missed_object_is_found_by_advert()
{
    ingestIntoBucket(kLowBucket, 1);
    ingestAuthority(2);
    buildFollowerFromCopy();

    char text[24];
    snprintf(text, sizeof(text), "writer missed");
    const PacketId missedId = idForBucket(SENDER_NODE, text, kBucket, 0x60000);
    ingestOn(follower, FOLLOWER_NODE, missedId, text);
    TEST_ASSERT_EQUAL_UINT32(3, readBucket(dirB, kBucket).count);
    TEST_ASSERT_EQUAL_UINT32(2, readBucket(dirA, kBucket).count);

    std::vector<meshtastic_MeshPacket> sent = tick(follower, FOLLOWER_NODE);
    TEST_ASSERT_EQUAL_UINT32(0, countType(sent, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)); // no push
    TEST_ASSERT_EQUAL_UINT32(1, countType(sent, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT));
    meshtastic_MeshPacket advert;
    for (const auto &out : sent)
        if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT)
            advert = out;

    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, advert));
    std::vector<meshtastic_MeshPacket> request = takeSent();
    TEST_ASSERT_EQUAL_UINT32(
        1, countType(request, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, sfppsr::kWholeSetBucket));
    TEST_ASSERT_EQUAL_UINT32(FOLLOWER_NODE, decodeSfpp(request[0]).encapsulated_to);

    // A third archive's in-sync advert, heard mid-pull.
    meshtastic_MeshPacket thirdAdvert;
    TEST_ASSERT_TRUE(buildWindowAdvertFromDir(dirA, false, thirdAdvert));
    thirdAdvert.from = FOLLOWER2_NODE;
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, thirdAdvert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, request[0]));
    std::vector<meshtastic_MeshPacket> provide = tick(follower, FOLLOWER_NODE);
    TEST_ASSERT_EQUAL_UINT32(1, countType(provide, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE));
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, provide[0]));
    std::vector<meshtastic_MeshPacket> items = takeSent();
    TEST_ASSERT_EQUAL_UINT32(1, countType(items, meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, items[0]));

    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 8; guard++) {
        for (const auto &out : tick(follower, FOLLOWER_NODE)) {
            if (decodeSfpp(out).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                continue;
            TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, out));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1, frames);
    TEST_ASSERT_EQUAL_UINT32(3, readBucket(dirA, kBucket).count);
    assertConverged(kBucket);
}

// Three archives each hold texts the others never heard. Every archive advertises, and each pulls
// a bucket it is behind on from whoever advertises it, until all three stores hold the same set.
void test_threeArchivesWithDifferentTextsConverge()
{
    ingestAuthority(2);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();

    ingestUnique(authority, AUTHORITY_NODE, kLowBucket, "alpha");
    ingestUnique(follower, FOLLOWER_NODE, kBucket, "bravo");
    ingestUnique(follower2, FOLLOWER2_NODE, kHighBucket, "charlie");

    runArchiveNetwork({{authority, AUTHORITY_NODE}, {follower, FOLLOWER_NODE}, {follower2, FOLLOWER2_NODE}}, 240);

    assertSameStore(dirA, dirB);
    assertSameStore(dirA, dirC);
    TEST_ASSERT_EQUAL_UINT32(5, readStore(dirA).count);
    for (uint32_t bucket : {kLowBucket, kBucket, kHighBucket}) {
        const BucketView a = readBucket(dirA, bucket);
        TEST_ASSERT_EQUAL_UINT32(a.count, readBucket(dirB, bucket).count);
        TEST_ASSERT_EQUAL_UINT64(a.checksum, readBucket(dirB, bucket).checksum);
        TEST_ASSERT_EQUAL_UINT32(a.count, readBucket(dirC, bucket).count);
        TEST_ASSERT_EQUAL_UINT64(a.checksum, readBucket(dirC, bucket).checksum);
    }
}

// One archive is away while the other two exchange and converge. It returns and catches up from a
// present archive that is not the sender of the texts it missed; the other then catches up too.
void test_absentArchiveCatchesUpOnReturn()
{
    ingestAuthority(2);
    buildFollowerFromCopy();
    buildSecondFollowerFromCopy();

    ingestUnique(authority, AUTHORITY_NODE, kLowBucket, "alpha");
    ingestUnique(follower, FOLLOWER_NODE, kBucket, "bravo");
    ingestUnique(follower2, FOLLOWER2_NODE, kHighBucket, "charlie");

    // C is away: A and B converge on their two stores.
    runArchiveNetwork({{authority, AUTHORITY_NODE}, {follower, FOLLOWER_NODE}}, 240);
    assertSameStore(dirA, dirB);
    TEST_ASSERT_EQUAL_UINT32(4, readStore(dirA).count);
    TEST_ASSERT_EQUAL_UINT32(3, readStore(dirC).count);

    // C returns to B only. B holds alpha, which reached it from A, so C catches up from a holder
    // that did not send the text; B also picks up C's own text.
    runArchiveNetwork({{follower, FOLLOWER_NODE}, {follower2, FOLLOWER2_NODE}}, 240);
    assertSameStore(dirB, dirC);
    TEST_ASSERT_EQUAL_UINT32(5, readStore(dirC).count);
    TEST_ASSERT_EQUAL_UINT32(4, readStore(dirA).count);

    // A returns and converges with the other two.
    runArchiveNetwork({{authority, AUTHORITY_NODE}, {follower, FOLLOWER_NODE}, {follower2, FOLLOWER2_NODE}}, 240);
    assertSameStore(dirA, dirB);
    assertSameStore(dirA, dirC);
    TEST_ASSERT_EQUAL_UINT32(5, readStore(dirA).count);
}

// An archive that never heard a text gets it while the only present holder is another archive,
// not the text's original sender.
void test_neverHeardTextArrivesFromAnotherArchive()
{
    ingestAuthority(1);
    buildFollowerFromCopy();
    const IngestedText text = ingested[0];

    // C is empty but derives the same root, so it has heard no text at all.
    portduino_config.sfpp_db_path = dirC + "/";
    follower2 = new TestSfppModule();
    myNodeInfo.my_node_num = FOLLOWER2_NODE;
    follower2->runOnce();
    takeSent();
    TEST_ASSERT_EQUAL_UINT32(0, readStore(dirC).count);
    TEST_ASSERT_EQUAL_UINT32(1, readStore(dirB).count);

    // A is away. B is the only present holder and is not the original sender SENDER_NODE.
    runArchiveNetwork({{follower, FOLLOWER_NODE}, {follower2, FOLLOWER2_NODE}}, 120);

    TEST_ASSERT_EQUAL_UINT32(1, countRows(dirC, text.id));
    assertSameStore(dirB, dirC);
}

// --- configurable frame limits ---

// Ten short IDs per frame turns a 25-ID listing into three frames; the follower reassembles the
// whole listing and asks for all 25 missing objects, three item-request frames.
void test_maxIdsPerFrameSplitsListingIntoThreeFrames()
{
    const std::string yaml = "StoreAndForward:\n"
                             "  Enabled: true\n"
                             "  DBPath: " +
                             dirA +
                             "/\n"
                             "  Repair: enum\n"
                             "  EnumDrainMs: 100\n"
                             "  MaxIdsPerFrame: 10\n";
    TEST_ASSERT_TRUE(loadSfppYaml(yaml));

    ingestAuthority(25);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 25));

    meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, requests[0]));

    std::vector<meshtastic_MeshPacket> provides;
    for (uint32_t i = 0; i < 20 && provides.size() < 3; i++) {
        for (const auto &out : drainAuthorityOne(100)) {
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE)
                provides.push_back(out);
        }
    }
    TEST_ASSERT_EQUAL_UINT(3, provides.size());

    uint32_t listed = 0;
    for (uint32_t i = 0; i < provides.size(); i++) {
        const meshtastic_StoreForwardPlusPlus msg = decodeSfpp(provides[i]);
        TEST_ASSERT_EQUAL_UINT32(3, msg.sr_frame >> 8);
        TEST_ASSERT_EQUAL_UINT32(i, msg.sr_frame & 0xFF);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(10, msg.sr_short_ids_count);
        listed += msg.sr_short_ids_count;
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, provides[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(25, listed);

    std::vector<meshtastic_MeshPacket> wanted = takeSent();
    uint32_t requested = 0;
    uint32_t requestFrames = 0;
    for (const auto &out : wanted) {
        const meshtastic_StoreForwardPlusPlus msg = decodeSfpp(out);
        if (msg.sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            continue;
        requestFrames++;
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(10, msg.sr_short_ids_count);
        requested += msg.sr_short_ids_count;
    }
    TEST_ASSERT_EQUAL_UINT32(3, requestFrames);
    TEST_ASSERT_EQUAL_UINT32(25, requested);
}

// The two frame-limit keys survive a parse -> emit -> parse -> emit round trip.
void test_frameLimitConfigRoundTrips()
{
    TEST_ASSERT_TRUE(loadSfppYaml("StoreAndForward:\n"
                                  "  Enabled: true\n"
                                  "  MaxIdsPerFrame: 12\n"
                                  "  ItemBodyMax: 100\n"));
    const std::string first = sfppBlock(portduino_config.emit_yaml());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, first.find("MaxIdsPerFrame: 12"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, first.find("ItemBodyMax: 100"));

    TEST_ASSERT_TRUE(loadSfppYaml(first));
    const std::string second = sfppBlock(portduino_config.emit_yaml());
    TEST_ASSERT_EQUAL_STRING(first.c_str(), second.c_str());
}

// --- whole-window advert (Wdd) ---

// One advert names the whole window, not a rotating bucket: its count spans every content bucket.
void test_whole_window_advert_replaces_the_bucket_advert()
{
    ingestIntoBucket(kLowBucket, 3);
    ingestAuthority(5);
    ingestIntoBucket(kHighBucket, 2);

    const meshtastic_StoreForwardPlusPlus advert = decodeSfpp(runAuthorityOnce());
    TEST_ASSERT_EQUAL_UINT(0, advert.sr_version); // the legacy per-bucket advert sets sr_version=1
    TEST_ASSERT_EQUAL_UINT32(10, advert.sr_count);
    TEST_ASSERT_TRUE(advert.scope != 0); // field 9 carries the packed scope now
}

// One advert per tick, and the wire names no bucket: the whole window is the unit.
void test_one_advert_per_tick_and_no_bucket_index()
{
    ingestIntoBucket(kLowBucket, 2);
    ingestAuthority(3);
    ingestIntoBucket(kHighBucket, 1);

    const meshtastic_StoreForwardPlusPlus advert = decodeSfpp(runAuthorityOnce());
    TEST_ASSERT_EQUAL_UINT(0, advert.sr_version);
    TEST_ASSERT_EQUAL_UINT32(0, advert.bucket);
    TEST_ASSERT_EQUAL_UINT32(6, advert.sr_count);

    const meshtastic_StoreForwardPlusPlus second = decodeSfpp(runAuthorityOnce());
    TEST_ASSERT_EQUAL_UINT(0, second.sr_version);
    TEST_ASSERT_EQUAL_UINT32(0, second.bucket);
}

// Consecutive adverts each cover the whole window, including objects written since the last one.
void test_consecutive_adverts_cover_the_whole_window()
{
    ingestIntoBucket(kLowBucket, 2);
    ingestAuthority(3);
    const meshtastic_StoreForwardPlusPlus first = decodeSfpp(runAuthorityOnce());
    TEST_ASSERT_EQUAL_UINT32(5, first.sr_count);
    TEST_ASSERT_EQUAL_UINT32(0, first.bucket);

    ingestIntoBucket(kHighBucket, 2);
    const meshtastic_StoreForwardPlusPlus second = decodeSfpp(runAuthorityOnce());
    TEST_ASSERT_EQUAL_UINT32(7, second.sr_count);
    TEST_ASSERT_EQUAL_UINT32(0, second.bucket);
    TEST_ASSERT_EQUAL_UINT(0, second.sr_version);

    sfppsr::Advert parsed;
    TEST_ASSERT_TRUE(sfppsr::parseWindowAdvert(second, parsed));
    TEST_ASSERT_EQUAL_UINT32(sfppsr::kWholeSetBucket, parsed.scope.bucket);
}

// The sketch capacity follows the writes since the previous advert: c = clamp(ceil(1.5 x writes), 4, 48).
void test_advert_capacity_follows_writes_and_caps_at_48()
{
    ingestAuthority(8); // ceil(1.5 x 8) = 12
    TEST_ASSERT_EQUAL_UINT(12 * 4, decodeSfpp(runAuthorityOnce()).message.size);

    // Nothing written since: the floor.
    TEST_ASSERT_EQUAL_UINT(4 * 4, decodeSfpp(runAuthorityOnce()).message.size);

    ingestAuthority(40); // 1.5 x 40 = 60, capped to 48
    TEST_ASSERT_EQUAL_UINT(48 * 4, decodeSfpp(runAuthorityOnce()).message.size);
}

// The new advert carries the packed scope in field 9, declares the store, and omits sr_version.
void test_new_advert_carries_scope_and_no_sr_version()
{
    portduino_config.sfpp_retention_hours = 48;
    portduino_config.sfpp_window_hours = 24;
    ingestAuthority(4);

    const meshtastic_StoreForwardPlusPlus msg = decodeSfpp(runAuthorityOnce());
    TEST_ASSERT_EQUAL_UINT(0, msg.sr_version);
    TEST_ASSERT_EQUAL_UINT(4, msg.root_hash.size);

    sfppsr::Advert advert;
    TEST_ASSERT_TRUE(sfppsr::parseWindowAdvert(msg, advert));
    TEST_ASSERT_EQUAL_UINT8(sfppsr::kAdvertSchemaVersion, advert.storeScope.version);
    TEST_ASSERT_EQUAL_UINT8(48, advert.storeScope.retentionHours);
    TEST_ASSERT_EQUAL_UINT8(4, advert.storeScope.windowEighths);
    TEST_ASSERT_EQUAL_UINT16(4, advert.storeScope.storeSize);
}

// A count gap larger than the advert's capacity cannot decode; the receiver goes straight to the
// whole-window listing instead of asking for a sketch it cannot resolve.
void test_count_gap_skips_the_sketch_decode()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_SKETCH;
    ingestAuthority(60); // ceil(1.5 x 60) caps at 48
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(execSql(dirB, "delete from channel_messages;"));

    const meshtastic_MeshPacket advert = runAuthorityOnce();
    sfppsr::Advert parsed;
    TEST_ASSERT_TRUE(sfppsr::parseWindowAdvert(decodeSfpp(advert), parsed));
    TEST_ASSERT_EQUAL_UINT(48, parsed.sketch.capacity());
    TEST_ASSERT_GREATER_THAN_UINT32(parsed.sketch.capacity(), parsed.count);

    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> sent = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(sent[0]).sfpp_message_type);
    TEST_ASSERT_EQUAL_UINT32(sfppsr::kWholeSetBucket, decodeSfpp(sent[0]).bucket);
}

// Objects missing from several content buckets converge from one whole-window advert and listing.
void test_whole_window_listing_converges()
{
    ingestIntoBucket(kLowBucket, 2);
    ingestIntoBucket(kBucket, 2);
    ingestIntoBucket(kHighBucket, 2);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteIngested(dirB, 1));
    TEST_ASSERT_TRUE(deleteIngested(dirB, 4));
    TEST_ASSERT_TRUE(deleteIngested(dirB, 6));

    Exchange exchange = runExchange();
    TEST_ASSERT_EQUAL_UINT(1, exchange.enumRequests);
    TEST_ASSERT_EQUAL_UINT(1, exchange.itemRequests);
    TEST_ASSERT_EQUAL_UINT(3, exchange.itemFrames);
    assertSameStore(dirA, dirB);
}

// The whole-window listing is not truncated at eight provide frames; a 40-ID window at four IDs
// per frame arrives as ten frames and is reassembled into one request.
void test_whole_window_listing_splits_past_eight_frames()
{
    const std::string yaml = "StoreAndForward:\n"
                             "  Enabled: true\n"
                             "  DBPath: " +
                             dirA +
                             "/\n"
                             "  Repair: enum\n"
                             "  EnumDrainMs: 100\n"
                             "  MaxIdsPerFrame: 4\n";
    TEST_ASSERT_TRUE(loadSfppYaml(yaml));

    ingestAuthority(40);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(execSql(dirB, "delete from channel_messages;"));

    const meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(requests[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, requests[0]));

    std::vector<meshtastic_MeshPacket> provides;
    for (uint32_t i = 0; i < 60 && provides.size() < 10; i++) {
        for (const auto &out : drainAuthorityOne(100)) {
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE)
                provides.push_back(out);
        }
    }
    TEST_ASSERT_EQUAL_UINT(10, provides.size());

    uint32_t listed = 0;
    for (const auto &provide : provides) {
        const meshtastic_StoreForwardPlusPlus msg = decodeSfpp(provide);
        TEST_ASSERT_EQUAL_UINT32(10, msg.sr_frame >> 8);
        TEST_ASSERT_EQUAL_UINT32(sfppsr::kWholeSetBucket, msg.bucket);
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, provide));
        listed += msg.sr_short_ids_count;
    }
    TEST_ASSERT_EQUAL_UINT(40, listed);

    uint32_t itemFrames = 0;
    uint32_t requested = 0;
    for (const auto &out : takeSent()) {
        const meshtastic_StoreForwardPlusPlus msg = decodeSfpp(out);
        if (msg.sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST)
            continue;
        itemFrames++;
        requested += msg.sr_short_ids_count;
    }
    TEST_ASSERT_EQUAL_UINT(10, itemFrames);
    TEST_ASSERT_EQUAL_UINT(40, requested);
}

// A whole-window pull of 60 missing objects needs two item-request frames; the request must not
// be dropped once it is larger than one frame.
void test_whole_window_pull_past_forty_ids_converges()
{
    ingestAuthority(60);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(execSql(dirB, "delete from channel_messages;"));

    const meshtastic_MeshPacket advert = runAuthorityOnce();
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    std::vector<meshtastic_MeshPacket> requests = takeSent();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_REQUEST, decodeSfpp(requests[0]).sfpp_message_type);
    TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, requests[0]));

    std::vector<meshtastic_MeshPacket> provides;
    for (uint32_t i = 0; i < 60 && provides.size() < 2; i++) {
        for (const auto &out : drainAuthorityOne(100)) {
            if (decodeSfpp(out).sfpp_message_type == meshtastic_StoreForwardPlusPlus_SFPP_message_type_ENUM_PROVIDE)
                provides.push_back(out);
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, provides.size());

    uint32_t listed = 0;
    for (const auto &provide : provides) {
        TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, provide));
        listed += decodeSfpp(provide).sr_short_ids_count;
    }
    TEST_ASSERT_EQUAL_UINT(60, listed);

    std::vector<meshtastic_MeshPacket> itemRequests = takeSent();
    TEST_ASSERT_EQUAL_UINT(2, itemRequests.size());
    uint32_t requested = 0;
    for (const auto &request : itemRequests) {
        TEST_ASSERT_EQUAL(meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_REQUEST, decodeSfpp(request).sfpp_message_type);
        requested += decodeSfpp(request).sr_short_ids_count;
        TEST_ASSERT_TRUE(deliver(authority, AUTHORITY_NODE, request));
    }
    TEST_ASSERT_EQUAL_UINT(60, requested);

    // The serving pacer releases 30 frames per rolling 10 minutes, so the remainder needs a
    // second window; keep draining across both until every object has arrived.
    uint32_t frames = 0;
    for (uint32_t guard = 0; guard < 1000 && frames < 60; guard++) {
        Time::setTestMillis(Time::getMillis() + 2000);
        myNodeInfo.my_node_num = AUTHORITY_NODE;
        testRouter->sent.clear();
        authority->runOnce();
        for (const auto &out : takeSent()) {
            if (decodeSfpp(out).sfpp_message_type != meshtastic_StoreForwardPlusPlus_SFPP_message_type_ITEM_FRAME)
                continue;
            TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, out));
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(60, frames);
    assertConverged();
}

// A per-bucket advert in the old shape, built by hand so the test outlives the old builder.
bool buildLegacyBucketAdvertFromDir(const std::string &dir, uint32_t bucket, meshtastic_MeshPacket &out)
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

    meshtastic_StoreForwardPlusPlus msg = meshtastic_StoreForwardPlusPlus_init_default;
    msg.sfpp_message_type = meshtastic_StoreForwardPlusPlus_SFPP_message_type_ADVERT;
    msg.sr_version = sfppsr::kDerivationVersion;
    msg.root_hash.size = 4;
    memcpy(msg.root_hash.bytes, root, msg.root_hash.size);
    msg.bucket = bucket;
    msg.sr_count = summary.count();
    msg.sr_checksum = summary.checksum();
    summary.sketch().serialize(msg.message.bytes);
    msg.message.size = summary.sketch().serializedSize();

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

// New builds accept only the packed-scope advert; the old per-bucket shape starts no pull.
void test_old_bucket_advert_is_refused()
{
    ingestAuthority(8);
    buildFollowerFromCopy();
    TEST_ASSERT_TRUE(deleteNewest(dirB, kBucket, 1));

    meshtastic_MeshPacket advert;
    TEST_ASSERT_TRUE(buildLegacyBucketAdvertFromDir(dirA, kBucket, advert));
    TEST_ASSERT_TRUE(deliver(follower, FOLLOWER_NODE, advert));
    TEST_ASSERT_EQUAL_UINT(0, takeSent().size());
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
    RUN_TEST(test_pacedDeliveryConverges);
    RUN_TEST(test_broadcastServesListenOnlyFollower);
    RUN_TEST(test_heardLiveObjectsArePromoted);
    RUN_TEST(test_genuineShortIdCollisionIsFetched);
    RUN_TEST(test_listenOnlyBlocksLegacyShim);
    RUN_TEST(test_corruptScratchRowIsNotPromoted);
    RUN_TEST(test_whole_window_advert_covers_a_changed_bucket);
    RUN_TEST(test_windowBudgetCapsServedFrames);
    RUN_TEST(test_fourRequestersShareTheServingWindow);
    RUN_TEST(test_fullIdleRequesterTableServesNewcomer);
    RUN_TEST(test_fullActiveRequesterTableKeepsSlots);
    RUN_TEST(test_quietRequesterWithQueuedFramesKeepsItsSlot);
    RUN_TEST(test_two_archives_derive_the_same_root_without_stratum0);
    RUN_TEST(test_full_root_advert_with_a_foreign_root_is_ignored);
    RUN_TEST(test_full_root_advert_with_our_root_is_pulled);
    RUN_TEST(test_follower_advertises_without_stratum0);
    RUN_TEST(test_stratum0_node_pulls_from_an_advert);
    RUN_TEST(test_objectFromAnyPeerIsAccepted);
    RUN_TEST(test_itemFrameOnAnotherChannelIsRefused);
    RUN_TEST(test_writer_missed_object_is_found_by_advert);
    RUN_TEST(test_threeArchivesWithDifferentTextsConverge);
    RUN_TEST(test_absentArchiveCatchesUpOnReturn);
    RUN_TEST(test_neverHeardTextArrivesFromAnotherArchive);
    RUN_TEST(test_maxIdsPerFrameSplitsListingIntoThreeFrames);
    RUN_TEST(test_frameLimitConfigRoundTrips);
    RUN_TEST(test_whole_window_advert_replaces_the_bucket_advert);
    RUN_TEST(test_one_advert_per_tick_and_no_bucket_index);
    RUN_TEST(test_consecutive_adverts_cover_the_whole_window);
    RUN_TEST(test_advert_capacity_follows_writes_and_caps_at_48);
    RUN_TEST(test_new_advert_carries_scope_and_no_sr_version);
    RUN_TEST(test_count_gap_skips_the_sketch_decode);
    RUN_TEST(test_whole_window_listing_converges);
    RUN_TEST(test_whole_window_listing_splits_past_eight_frames);
    RUN_TEST(test_whole_window_pull_past_forty_ids_converges);
    RUN_TEST(test_old_bucket_advert_is_refused);
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
