/*
 * Native coverage for the StoreForwardPlusPlusModule archive path in
 * src/modules/Native/StoreForwardPlusPlus.cpp: the first automated test that runs SF++ module code.
 *
 * The module is normally only constructed by meshtasticd's module setup, which a test binary never
 * runs, and the native runner passes -s, which turns SF++ off. These cases therefore construct the
 * module directly against a throwaway sqlite store and drive its handlers.
 *
 * What is pinned:
 *   - a text message is ingested into channel_messages with its plaintext payload and wire hashes;
 *   - runOnce() announces the tip of that stored chain, and a LINK_REQUEST for the first link reads
 *     the stored ciphertext back out;
 *   - a legacy CLIENT_HISTORY (portnum 65) is answered from the store in the legacy response format,
 *     with the 48 h retention;
 *   - portnum 65 is not claimed while the legacy StoreForwardModule is a server, so the two modules
 *     cannot answer the same request twice.
 *
 * The regression guarded: without this suite nothing executes the module at all, so a change that
 * made ingest, announce, read-back or the legacy shim silently stop working would pass every other
 * native test.
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
#include "modules/Native/StoreForwardPlusPlus.h"
#include "modules/RoutingModule.h"
#include "support/MockMeshService.h"

#if !MESHTASTIC_EXCLUDE_STOREFORWARD
#include "modules/StoreForwardModule.h"
#endif

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
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
constexpr NodeNum LOCAL_NODE = 0x11112222;
constexpr NodeNum REMOTE_NODE = 0x33334444;
constexpr PacketId TEXT_ID = 0x01020304;
constexpr uint32_t LEGACY_RETENTION_S = 48 * 60 * 60;

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

// Router::send is the seam every non-local SF++ packet crosses, so it is where replies are observed.
// Like a real interface it takes ownership rather than releasing the packet: MeshService::sendToMesh
// still reads it after sendLocal() to make the phone copy.
class CapturingRouter : public Router
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sent.push_back(*p);
        owned.push_back(p);
        return ERRNO_OK;
    }

    std::vector<meshtastic_MeshPacket> sent;
    std::vector<meshtastic_MeshPacket *> owned;
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

// Exposes the protected handlers and supplies the encrypted copy MeshModule::callModules() would.
class TestSfppModule : public StoreForwardPlusPlusModule
{
  public:
    using StoreForwardPlusPlusModule::buildBucketSummary;
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

    ProcessMessage dispatch(const meshtastic_MeshPacket &mp)
    {
        currentEncrypted = nullptr;
        return handleReceived(mp);
    }
};

TestNodeDB *testNodeDB;
CapturingRouter *testRouter;
MockMeshService *testService;
TestRoutingModule *testRoutingModule;
TestSfppModule *testSfpp;
std::string dbDir;

std::string dbFile()
{
    return dbDir + "/storeforwardpp.db";
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

meshtastic_MeshPacket makeEncryptedCopyCCM(const meshtastic_MeshPacket &mp)
{
    uint8_t plain[256];
    const size_t len = pb_encode_to_bytes(plain, sizeof(plain), &meshtastic_Data_msg, &mp.decoded);

    meshtastic_MeshPacket enc = meshtastic_MeshPacket_init_zero;
    enc.from = mp.from;
    enc.to = mp.to;
    enc.id = mp.id;
    enc.channel = channels.getHash(0);
    enc.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    TEST_ASSERT_TRUE(crypto->encryptPacketCCM(channels.getKey(0), mp.from, mp.to, mp.id, len, plain, enc.encrypted.bytes));
    enc.encrypted.size = len + CryptoEngine::AEAD_TAG_SIZE;
    return enc;
}

meshtastic_MeshPacket makeHistoryRequest(NodeNum from, NodeNum to, PacketId id, uint32_t windowMinutes, uint32_t cursor = 0)
{
    meshtastic_StoreAndForward request = meshtastic_StoreAndForward_init_zero;
    request.rr = meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY;
    if (windowMinutes > 0 || cursor > 0) {
        request.which_variant = meshtastic_StoreAndForward_history_tag;
        request.variant.history.window = windowMinutes;
        request.variant.history.last_request = cursor;
    }

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.to = to;
    mp.id = id;
    mp.channel = 0;
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_API;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;
    mp.decoded.payload.size =
        pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &request);
    return mp;
}

// Pulls every MeshPacket the archive queued for its own attached phone.
std::vector<meshtastic_MeshPacket> drainPhoneQueue()
{
    std::vector<meshtastic_MeshPacket> out;
    while (meshtastic_MeshPacket *p = testService->getForPhone()) {
        out.push_back(*p);
        testService->releaseToPool(p);
    }
    return out;
}

const meshtastic_MeshPacket *findPort(const std::vector<meshtastic_MeshPacket> &packets, meshtastic_PortNum port)
{
    for (const auto &p : packets) {
        if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag && p.decoded.portnum == port)
            return &p;
    }
    return nullptr;
}

bool decodeSfpp(const meshtastic_MeshPacket &p, meshtastic_StoreForwardPlusPlus &out)
{
    return p.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
           pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_StoreForwardPlusPlus_msg, &out);
}

bool decodeStoreForward(const meshtastic_MeshPacket &p, meshtastic_StoreAndForward &out)
{
    return p.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
           pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_StoreAndForward_msg, &out);
}

struct StoredRow {
    std::vector<uint8_t> root;
    std::vector<uint8_t> encrypted;
    std::vector<uint8_t> messageHash;
    std::string payload;
    uint32_t rxTime = 0;
    uint64_t shortId = 0;
    uint64_t checksum = 0;
    uint32_t bucket = 0;
    uint8_t timeBasis = 0;
    uint32_t bootId = 0;
    uint32_t ageMargin = 0;
};

bool readStoredRow(PacketId id, StoredRow &row)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db,
                           "select root_hash, encrypted_bytes, payload, rx_time, message_hash, short_id, checksum, bucket, "
                           "time_basis, boot_id, age_margin from channel_messages where packet_id=? limit 1;",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const uint8_t *root = (const uint8_t *)sqlite3_column_blob(stmt, 0);
            int rootLen = sqlite3_column_bytes(stmt, 0);
            row.root.assign(root, root + rootLen);
            const uint8_t *enc = (const uint8_t *)sqlite3_column_blob(stmt, 1);
            int encLen = sqlite3_column_bytes(stmt, 1);
            row.encrypted.assign(enc, enc + encLen);
            const unsigned char *text = sqlite3_column_text(stmt, 2);
            row.payload.assign((const char *)text, sqlite3_column_bytes(stmt, 2));
            row.rxTime = (uint32_t)sqlite3_column_int64(stmt, 3);
            const uint8_t *hash = (const uint8_t *)sqlite3_column_blob(stmt, 4);
            int hashLen = sqlite3_column_bytes(stmt, 4);
            row.messageHash.assign(hash, hash + hashLen);
            row.shortId = (uint64_t)sqlite3_column_int64(stmt, 5);
            row.checksum = (uint64_t)sqlite3_column_int64(stmt, 6);
            row.bucket = (uint32_t)sqlite3_column_int64(stmt, 7);
            row.timeBasis = (uint8_t)sqlite3_column_int(stmt, 8);
            row.bootId = (uint32_t)sqlite3_column_int64(stmt, 9);
            row.ageMargin = (uint32_t)sqlite3_column_int64(stmt, 10);
            found = true;
        }
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

uint32_t storedBucket(PacketId id)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return 0;
    }
    sqlite3_stmt *stmt = nullptr;
    uint32_t bucket = 0;
    if (sqlite3_prepare_v2(db, "select bucket from channel_messages where packet_id=? limit 1;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            bucket = (uint32_t)sqlite3_column_int64(stmt, 0);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return bucket;
}

std::vector<uint8_t> mappingRoot();

uint32_t storedRowCount(PacketId id)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return 0;
    }
    sqlite3_stmt *stmt = nullptr;
    uint32_t count = 0;
    if (sqlite3_prepare_v2(db, "select count(*) from channel_messages where packet_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = (uint32_t)sqlite3_column_int64(stmt, 0);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

uint32_t storedRowCountInBucket(PacketId id, uint32_t bucket)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return 0;
    }
    sqlite3_stmt *stmt = nullptr;
    uint32_t count = 0;
    if (sqlite3_prepare_v2(db, "select count(*) from channel_messages where packet_id=? and bucket=?;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        sqlite3_bind_int64(stmt, 2, bucket);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = (uint32_t)sqlite3_column_int64(stmt, 0);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

uint32_t storedScratchRowCount(PacketId id)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return 0;
    }
    sqlite3_stmt *stmt = nullptr;
    uint32_t count = 0;
    if (sqlite3_prepare_v2(db, "select count(*) from local_messages where packet_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = (uint32_t)sqlite3_column_int64(stmt, 0);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

bool readScratchEncrypted(PacketId id, std::vector<uint8_t> &out)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return false;
    }
    sqlite3_stmt *stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db, "select encrypted_bytes from local_messages where packet_id=? limit 1;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const uint8_t *enc = (const uint8_t *)sqlite3_column_blob(stmt, 0);
            const int len = sqlite3_column_bytes(stmt, 0);
            out.assign(enc, enc + len);
            found = true;
        }
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

void insertScratchRow(uint32_t to, uint32_t from, PacketId id, const uint8_t *cipher, size_t len, const uint8_t *hash)
{
    std::vector<uint8_t> root = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, root.size());
    sqlite3 *db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    sqlite3_stmt *stmt = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK,
                          sqlite3_prepare_v2(db,
                                             "insert into local_messages (destination, sender, packet_id, root_hash, "
                                             "encrypted_bytes, message_hash, payload, rx_time) values (?, ?, ?, ?, ?, ?, '', ?);",
                                             -1, &stmt, nullptr));
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)to);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)from);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)id);
    sqlite3_bind_blob(stmt, 4, root.data(), SFPP_HASH_SIZE, NULL);
    sqlite3_bind_blob(stmt, 5, cipher, (int)len, NULL);
    sqlite3_bind_blob(stmt, 6, hash, SFPP_HASH_SIZE, NULL);
    sqlite3_bind_int64(stmt, 7, getValidTime(RTCQualityNTP, true));
    TEST_ASSERT_EQUAL_INT(SQLITE_DONE, sqlite3_step(stmt));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void insertRawCanonRow(PacketId id, const uint8_t *hash)
{
    std::vector<uint8_t> root = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, root.size());
    uint8_t cipher[2] = {0x5A, 0x5A};
    sqlite3 *db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    sqlite3_stmt *stmt = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK,
                          sqlite3_prepare_v2(db,
                                             "insert into channel_messages (destination, sender, packet_id, rx_time, root_hash, "
                                             "encrypted_bytes, message_hash, payload, short_id, checksum, bucket) "
                                             "values (?, ?, ?, ?, ?, ?, ?, '', ?, ?, ?);",
                                             -1, &stmt, nullptr));
    sqlite3_bind_int64(stmt, 1, NODENUM_BROADCAST);
    sqlite3_bind_int64(stmt, 2, REMOTE_NODE);
    sqlite3_bind_int64(stmt, 3, id);
    sqlite3_bind_int64(stmt, 4, getValidTime(RTCQualityNTP, true));
    sqlite3_bind_blob(stmt, 5, root.data(), SFPP_HASH_SIZE, NULL);
    sqlite3_bind_blob(stmt, 6, cipher, sizeof(cipher), NULL);
    sqlite3_bind_blob(stmt, 7, hash, SFPP_HASH_SIZE, NULL);
    sqlite3_bind_int64(stmt, 8, (sqlite3_int64)sfppsr::shortId(hash, SFPP_HASH_SIZE));
    sqlite3_bind_int64(stmt, 9, (sqlite3_int64)sfppsr::checksumContribution(hash, SFPP_HASH_SIZE));
    sqlite3_bind_int(stmt, 10, (int)sfppsr::contentBucket(hash, SFPP_HASH_SIZE));
    TEST_ASSERT_EQUAL_INT(SQLITE_DONE, sqlite3_step(stmt));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void setStoredRxTime(PacketId id, uint32_t rxTime)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "update channel_messages set rx_time=? where packet_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, rxTime);
        sqlite3_bind_int64(stmt, 2, id);
        sqlite3_step(stmt);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void updateStoredPayload(PacketId id, const char *payload)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "update channel_messages set payload=? where packet_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, payload, -1, NULL);
        sqlite3_bind_int64(stmt, 2, id);
        sqlite3_step(stmt);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void updateStoredEncrypted(PacketId id, const uint8_t *data, size_t len)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "update channel_messages set encrypted_bytes=? where packet_id=?;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, data, (int)len, NULL);
        sqlite3_bind_int64(stmt, 2, id);
        sqlite3_step(stmt);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
}

bool ingestOneText(const char *text)
{
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, text, getValidTime(RTCQualityNTP, true));
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    return testSfpp->ingest(mp, &enc) == ProcessMessage::CONTINUE;
}

bool ingestNumberedTexts(uint32_t count)
{
    const uint32_t now = getValidTime(RTCQualityNTP, true);
    for (uint32_t i = 0; i < count; i++) {
        meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID + i, "paged text", now - (count - i));
        meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
        if (testSfpp->ingest(mp, &enc) != ProcessMessage::CONTINUE)
            return false;
    }
    return true;
}

// The root this store maps the primary channel to, or empty when it has no mapping yet.
std::vector<uint8_t> mappingRoot()
{
    std::vector<uint8_t> out;
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return out;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "select root_hash from mappings limit 1;", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t *root = (const uint8_t *)sqlite3_column_blob(stmt, 0);
        const int len = sqlite3_column_bytes(stmt, 0);
        if (root && len == (int)SFPP_HASH_SIZE)
            out.assign(root, root + SFPP_HASH_SIZE);
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return out;
}

std::vector<std::vector<uint8_t>> mappingRoots()
{
    std::vector<std::vector<uint8_t>> out;
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return out;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "select root_hash from mappings order by rowid;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const uint8_t *root = (const uint8_t *)sqlite3_column_blob(stmt, 0);
            const int len = sqlite3_column_bytes(stmt, 0);
            if (root && len == (int)SFPP_HASH_SIZE)
                out.emplace_back(root, root + SFPP_HASH_SIZE);
        }
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return out;
}

bool writeMappingRoot(const uint8_t *root)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbFile().c_str(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return false;
    }
    // A store built by the old rule has only the stale channel mapping, so drop the new one first.
    sqlite3_exec(db, "delete from mappings;", nullptr, nullptr, nullptr);
    sqlite3_stmt *stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db, "insert into mappings (chain_type, identifier, root_hash) values(0, ?, ?);", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, channels.getHash(0));
        sqlite3_bind_blob(stmt, 2, root, SFPP_HASH_SIZE, NULL);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt)
        sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

// A fresh empty store for the channel, run once with the given node identity and clock, returning the
// root it mapped. Used to show the root depends on neither.
std::vector<uint8_t> freshDerivedRoot(NodeNum node, uint32_t nowMs)
{
    delete testSfpp;
    testSfpp = nullptr;
    std::remove(dbFile().c_str());
    std::remove((dbFile() + "-journal").c_str());

    myNodeInfo.my_node_num = node;
    Time::setTestMillis(nowMs);
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    testSfpp = new TestSfppModule();
    testSfpp->runOnce();
    return mappingRoot();
}

void setTestClock(RTCQuality quality, uint32_t epoch, uint32_t uptimeSecs)
{
    Time::setTestMillis(uptimeSecs * 1000);
    resetRTCStateForTests();
    if (quality == RTCQualityNone) {
        setBootRelativeTimeForUnitTest(uptimeSecs);
    } else {
        struct timeval tv;
        tv.tv_sec = epoch;
        tv.tv_usec = 0;
        perhapsSetRTC(quality, &tv);
    }
}

void recreateModule()
{
    delete testSfpp;
    testSfpp = new TestSfppModule();
}

void setPrimaryChannel(const char *name, const uint8_t *psk, size_t pskLen)
{
    meshtastic_Channel &channel = channelFile.channels[0];
    memset(channel.settings.name, 0, sizeof(channel.settings.name));
    strncpy(channel.settings.name, name, sizeof(channel.settings.name) - 1);
    channel.settings.psk.size = pskLen;
    if (pskLen > 0)
        memcpy(channel.settings.psk.bytes, psk, pskLen);
    channels.onConfigChanged();
}

} // namespace

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    channelFile = meshtastic_ChannelFile_init_zero;
    owner = meshtastic_User_init_zero;

    testNodeDB = new TestNodeDB();
    nodeDB = testNodeDB;
    myNodeInfo.my_node_num = LOCAL_NODE;

    channels.initDefaults();
    channels.onConfigChanged();

    testService = new MockMeshService();
    service = testService;

    testRouter = new CapturingRouter();
    testRouter->addInterface(std::unique_ptr<RadioInterface>(new TestRadio()));
    router = testRouter;

    testRoutingModule = new TestRoutingModule();
    routingModule = testRoutingModule;

    char tmpl[] = "/tmp/meshtastic-sfpp-XXXXXX";
    char *dir = mkdtemp(tmpl);
    dbDir = dir ? dir : "/tmp";
    portduino_config.sfpp_db_path = dbDir + "/";
    portduino_config.sfpp_enabled = true;
    portduino_config.sfpp_hops = 3;
    portduino_config.sfpp_steal_port = false;

    testSfpp = new TestSfppModule();
}

void tearDown(void)
{
    delete testSfpp;
    testSfpp = nullptr;

    if (!dbDir.empty() && dbDir != "/tmp") {
        std::remove(dbFile().c_str());
        std::remove((dbFile() + "-journal").c_str());
        rmdir(dbDir.c_str());
    }

    delete testRoutingModule;
    testRoutingModule = nullptr;
    routingModule = nullptr;

    // Broadcast sends are copied to the phone queue; both pools must be drained before teardown.
    drainPhoneQueue();
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
}

// --- Archive store, announce and read-back ---

// Proves handleReceived() ingests a broadcast text into channel_messages with the plaintext payload
// and the hashes LINK_PROVIDE/ADVERT depend on. Without it a regression could leave the store empty
// while every other suite still passed.
void test_ingestStoresTextWithPayloadAndHashes()
{
    TEST_ASSERT_TRUE(ingestOneText("hello archive"));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_STRING("hello archive", row.payload.c_str());
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, row.root.size());
    TEST_ASSERT_TRUE(row.shortId != 0);
    TEST_ASSERT_TRUE(row.encrypted.size() > 0);
}

// Every archive stores the channel texts it hears directly, with no writer role to opt into.
void test_text_is_canonical_without_stratum0()
{
    TEST_ASSERT_TRUE(ingestOneText("canonical without a flag"));

    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(TEXT_ID));
    TEST_ASSERT_EQUAL_UINT32(0, storedScratchRowCount(TEXT_ID));
    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_STRING("canonical without a flag", row.payload.c_str());
}

// The emitted config and the shipped config name no writer role.
void test_default_config_has_no_stratum0()
{
    portduino_config.sfpp_enabled = true;
    TEST_ASSERT_EQUAL(std::string::npos, portduino_config.emit_yaml().find("Stratum0"));

    std::ifstream config("bin/config-dist.yaml");
    TEST_ASSERT_TRUE(config.good());
    const std::string text((std::istreambuf_iterator<char>(config)), std::istreambuf_iterator<char>());
    TEST_ASSERT_EQUAL(std::string::npos, text.find("Stratum0"));
}

// One message heard as two ciphertexts is one object; the sender's copy replaces a relay's copy.
void test_archive_keeps_one_object_for_two_ciphertexts()
{
    const PacketId id = TEXT_ID + 0x200;
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, id, "one message, two copies", getValidTime(RTCQualityNTP, true));

    mp.relay_node = 0x5b;
    meshtastic_MeshPacket relay = makeEncryptedCopy(mp);
    relay.encrypted.bytes[0] ^= 0xFF;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &relay));
    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));

    mp.relay_node = nodeDB->getLastByteOfNodeNum(REMOTE_NODE);
    meshtastic_MeshPacket sender = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &sender));

    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));
    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(id, row));
    TEST_ASSERT_EQUAL_UINT(sender.encrypted.size, row.encrypted.size());
    TEST_ASSERT_EQUAL_MEMORY(sender.encrypted.bytes, row.encrypted.data(), row.encrypted.size());
}

// Replacing the hash moves the row out of the relay's content bucket into the sender's.
void test_sender_copy_moves_the_content_bucket()
{
    const PacketId id = TEXT_ID + 0x201;
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, id, "bucket move", getValidTime(RTCQualityNTP, true));

    meshtastic_MeshPacket sender = makeEncryptedCopy(mp);
    uint8_t senderHash[SFPP_HASH_SIZE];
    sfppframe::messageHash(sender.encrypted.bytes, sender.encrypted.size, mp.to, mp.from, id, senderHash);
    const uint32_t senderBucket = sfppsr::contentBucket(senderHash, SFPP_HASH_SIZE);

    meshtastic_MeshPacket relay = makeEncryptedCopy(mp);
    uint32_t relayBucket = senderBucket;
    uint8_t relayHash[SFPP_HASH_SIZE];
    for (uint16_t i = 0; i < 256 && relayBucket == senderBucket; i++) {
        relay = makeEncryptedCopy(mp);
        relay.encrypted.bytes[1] ^= (uint8_t)i;
        sfppframe::messageHash(relay.encrypted.bytes, relay.encrypted.size, mp.to, mp.from, id, relayHash);
        relayBucket = sfppsr::contentBucket(relayHash, SFPP_HASH_SIZE);
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(senderBucket, relayBucket);

    mp.relay_node = 0x5b;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &relay));
    TEST_ASSERT_EQUAL_UINT32(relayBucket, storedBucket(id));

    mp.relay_node = nodeDB->getLastByteOfNodeNum(REMOTE_NODE);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &sender));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(id, row));
    TEST_ASSERT_EQUAL_UINT32(senderBucket, row.bucket);
    TEST_ASSERT_EQUAL_MEMORY(sender.encrypted.bytes, row.encrypted.data(), row.encrypted.size());
    TEST_ASSERT_EQUAL_UINT32(0, storedRowCountInBucket(id, relayBucket));
}

// A relay's later copy never displaces the sender's copy.
void test_relay_copy_does_not_replace_the_sender_copy()
{
    const PacketId id = TEXT_ID + 0x202;
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, id, "sender first", getValidTime(RTCQualityNTP, true));

    mp.relay_node = nodeDB->getLastByteOfNodeNum(REMOTE_NODE);
    meshtastic_MeshPacket sender = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &sender));

    mp.relay_node = 0x5b;
    meshtastic_MeshPacket relay = makeEncryptedCopy(mp);
    relay.encrypted.bytes[0] ^= 0xAA;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &relay));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(id, row));
    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));
    TEST_ASSERT_EQUAL_MEMORY(sender.encrypted.bytes, row.encrypted.data(), row.encrypted.size());
}

// Guards the base's isInDB path; not a RED parent test for the identity change.
void test_identity_collision_never_adds_a_second_row()
{
    const PacketId id = TEXT_ID + 0x203;
    const PacketId other = TEXT_ID + 0x204;
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, id, "collision", getValidTime(RTCQualityNTP, true));

    mp.relay_node = 0x5b;
    meshtastic_MeshPacket relay = makeEncryptedCopy(mp);
    relay.encrypted.bytes[0] ^= 0x11;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &relay));
    StoredRow relayRow;
    TEST_ASSERT_TRUE(readStoredRow(id, relayRow));
    const uint32_t relayTime = relayRow.rxTime;

    mp.relay_node = nodeDB->getLastByteOfNodeNum(REMOTE_NODE);
    meshtastic_MeshPacket sender = makeEncryptedCopy(mp);
    uint8_t senderHash[SFPP_HASH_SIZE];
    sfppframe::messageHash(sender.encrypted.bytes, sender.encrypted.size, mp.to, mp.from, id, senderHash);
    insertRawCanonRow(other, senderHash);

    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &sender));
    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));
    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(id, row));
    TEST_ASSERT_EQUAL_MEMORY(relay.encrypted.bytes, row.encrypted.data(), row.encrypted.size());
    TEST_ASSERT_EQUAL_UINT32(relayTime, row.rxTime);
    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(other));
}

// A recovered item frame for the same identity does not add a second row.
void test_recovered_item_with_the_same_identity_is_one_object()
{
    const PacketId id = TEXT_ID + 0x205;
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, id, "recovered copy", getValidTime(RTCQualityNTP, true));

    mp.relay_node = 0x5b;
    meshtastic_MeshPacket relay = makeEncryptedCopy(mp);
    relay.encrypted.bytes[0] ^= 0x22;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &relay));
    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));

    meshtastic_MeshPacket sender = makeEncryptedCopy(mp);
    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    TEST_ASSERT_TRUE(sfppframe::split(sender.encrypted.bytes, sender.encrypted.size, mp.to, mp.from, id, 0, false, frames));
    for (const auto &frame : frames) {
        meshtastic_MeshPacket item = meshtastic_MeshPacket_init_zero;
        item.from = REMOTE_NODE;
        item.to = LOCAL_NODE;
        item.id = 0x70000000 + id;
        item.channel = 0;
        item.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
        item.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        item.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP;
        item.decoded.payload.size = pb_encode_to_bytes(item.decoded.payload.bytes, sizeof(item.decoded.payload.bytes),
                                                       &meshtastic_StoreForwardPlusPlus_msg, &frame);
        meshtastic_MeshPacket enc = meshtastic_MeshPacket_init_zero;
        enc.id = item.id;
        TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(item, &enc));
    }

    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));
    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(id, row));
    TEST_ASSERT_EQUAL_MEMORY(relay.encrypted.bytes, row.encrypted.data(), row.encrypted.size());
}

// Every heard text is canonical: one row, and the sender's copy replaces a relay's.
void test_live_text_keeps_one_object_for_two_ciphertexts()
{
    const PacketId id = TEXT_ID + 0x206;
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, id, "canonical identity", getValidTime(RTCQualityNTP, true));

    mp.relay_node = 0x5b;
    meshtastic_MeshPacket relay = makeEncryptedCopy(mp);
    relay.encrypted.bytes[0] ^= 0x33;
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &relay));
    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));
    TEST_ASSERT_EQUAL_UINT32(0, storedScratchRowCount(id));

    mp.relay_node = nodeDB->getLastByteOfNodeNum(REMOTE_NODE);
    meshtastic_MeshPacket sender = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &sender));

    TEST_ASSERT_EQUAL_UINT32(1, storedRowCount(id));
    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(id, row));
    TEST_ASSERT_EQUAL_UINT(sender.encrypted.size, row.encrypted.size());
    TEST_ASSERT_EQUAL_MEMORY(sender.encrypted.bytes, row.encrypted.data(), row.encrypted.size());
}

// A scratch row is promoted only under its own content bucket; a short-ID request for another
// bucket must not move it there.
void test_scratch_collision_across_buckets_is_not_promoted()
{
    const PacketId id = TEXT_ID + 0x207;
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, id, "other bucket", getValidTime(RTCQualityNTP, true));
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    uint8_t hash[SFPP_HASH_SIZE];
    sfppframe::messageHash(enc.encrypted.bytes, enc.encrypted.size, mp.to, mp.from, id, hash);
    testSfpp->runOnce(); // derive the channel root before seeding the row
    insertScratchRow(mp.to, mp.from, id, enc.encrypted.bytes, enc.encrypted.size, hash);
    TEST_ASSERT_EQUAL_UINT32(1, storedScratchRowCount(id));
    const uint32_t ownBucket = sfppsr::contentBucket(hash, SFPP_HASH_SIZE);
    const uint32_t otherBucket = (ownBucket + 1) % 256;
    std::vector<pinsketch::Element> ids{sfppsr::shortId(hash, SFPP_HASH_SIZE)};
    std::vector<uint8_t> root = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, root.size());

    testSfpp->testPromoteScratchMatches(root.data(), otherBucket, ids);
    TEST_ASSERT_EQUAL_UINT32(1, storedScratchRowCount(id));
    TEST_ASSERT_EQUAL_UINT32(0, storedRowCount(id));
}

// The store and the frame splitter must hash a message identically; if they drift, a frame's key
// stops matching the row it came from and no replay can ever verify.
void test_storedMessageHashMatchesTheFrameHash()
{
    TEST_ASSERT_TRUE(ingestOneText("hash me"));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));

    uint8_t expected[sfppframe::kMessageHashBytes];
    sfppframe::messageHash(row.encrypted.data(), row.encrypted.size(), NODENUM_BROADCAST, REMOTE_NODE, TEXT_ID, expected);
    TEST_ASSERT_EQUAL_UINT32(sfppframe::kMessageHashBytes, (uint32_t)row.messageHash.size());
    TEST_ASSERT_EQUAL_MEMORY(expected, row.messageHash.data(), sfppframe::kMessageHashBytes);
}

// runOnce() in stratum-0 mode must announce the stored tip: CANON_ANNOUNCE with the full root, the
// short message and commit hashes and a non-zero chain count. A quiet module here means a peer can
// never learn the archive has anything.

// A LINK_REQUEST with an empty commit hash asks for the first link. The answer must carry the
// original ciphertext and sender/id back, i.e. the archive can serve what it stored.

// --- Legacy CLIENT_HISTORY shim ---

// A phone connected to the archive sends CLIENT_HISTORY to the node itself. The archive must answer
// ROUTER_HISTORY and then the stored text as ROUTER_TEXT_BROADCAST, which is what an unchanged
// Android/Apple client already understands.
void test_legacyHistoryAnswersInLegacyFormat()
{
    TEST_ASSERT_TRUE(ingestOneText("history text"));

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x0B0B0B0B, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));

    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL(meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY, reply.rr);
    TEST_ASSERT_EQUAL(meshtastic_StoreAndForward_history_tag, reply.which_variant);
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);
    TEST_ASSERT_EQUAL_UINT32(LEGACY_RETENTION_S * 1000, reply.variant.history.window);

    testSfpp->runOnce();
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward text;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], text));
    TEST_ASSERT_EQUAL(meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST, text.rr);
    TEST_ASSERT_EQUAL(meshtastic_StoreAndForward_text_tag, text.which_variant);
    TEST_ASSERT_EQUAL_UINT32(TEXT_ID, text.original_id);
    TEST_ASSERT_EQUAL_UINT(REMOTE_NODE, phone[0].from);
    TEST_ASSERT_EQUAL_UINT(strlen("history text"), text.variant.text.size);
    TEST_ASSERT_EQUAL_MEMORY("history text", text.variant.text.bytes, text.variant.text.size);
}

// The legacy shim's history replies share the 30-frame/10-min serving budget; a burst of requests
// cannot send a 31st header inside the window.
void test_legacyHistoryHeadersShareTheServeBudget()
{
    for (uint32_t i = 0; i < 31; i++) {
        meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x40000000 + i, 0);
        TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));
    }
    uint32_t replies = 0;
    for (const auto &p : drainPhoneQueue()) {
        meshtastic_StoreAndForward reply;
        if (decodeStoreForward(p, reply) && reply.rr == meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY)
            replies++;
    }
    TEST_ASSERT_EQUAL_UINT32(30, replies);
}

// A client-supplied window is honoured (the legacy request carries minutes) and may shorten the
// default retention but never extend past it.
void test_legacyHistoryUsesLegacyRequestWindow()
{
    TEST_ASSERT_TRUE(ingestOneText("window text"));

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x0C0C0C0C, 60);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));

    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(60 * 60 * 1000, reply.variant.history.window);
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);
}

// The 48 h default is the ceiling: a text older than that must not be returned even by an
// unspecified-window request. This keeps the archive from becoming an unbounded history oracle.
void test_legacyHistoryExcludesTextsOlderThanRetention()
{
    TEST_ASSERT_TRUE(ingestOneText("ancient text"));
    setStoredRxTime(TEXT_ID, getValidTime(RTCQualityNTP, true) - LEGACY_RETENTION_S - 60);

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x0D0D0D0D, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));

    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(0, reply.variant.history.history_messages);
}

// A follower stores the chain's ciphertext but not its plaintext. The shim must decrypt the primary
// channel copy and require a text Data, rather than serving nothing for those rows.
void test_legacyHistoryDecryptsEmptyPayloadRow()
{
    TEST_ASSERT_TRUE(ingestOneText("follower text"));
    updateStoredPayload(TEXT_ID, "");

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x0F0F0F0F, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));

    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);

    testSfpp->runOnce();
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward text;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], text));
    TEST_ASSERT_EQUAL(meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST, text.rr);
    TEST_ASSERT_EQUAL_UINT32(TEXT_ID, text.original_id);
    TEST_ASSERT_EQUAL_UINT(strlen("follower text"), text.variant.text.size);
    TEST_ASSERT_EQUAL_MEMORY("follower text", text.variant.text.bytes, text.variant.text.size);
}

// The fallback decrypt is only a history source for text: another portnum must be skipped rather
// than queued as if it were a message.
void test_legacyHistorySkipsNonTextCiphertext()
{
    TEST_ASSERT_TRUE(ingestOneText("not a text"));

    meshtastic_Data position = meshtastic_Data_init_zero;
    position.portnum = meshtastic_PortNum_POSITION_APP;
    position.payload.size = 4;
    memset(position.payload.bytes, 0x5A, position.payload.size);

    uint8_t plain[128];
    const size_t len = pb_encode_to_bytes(plain, sizeof(plain), &meshtastic_Data_msg, &position);
    crypto->setKey(channels.getKey(0));
    crypto->encryptPacket(REMOTE_NODE, TEXT_ID, len, plain);
    updateStoredEncrypted(TEXT_ID, plain, len);
    updateStoredPayload(TEXT_ID, "");

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x1A1A1A1A, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));

    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(0, reply.variant.history.history_messages);
}

// An AEAD primary stores CCM rows; the fallback must use the CCM path, not CTR, or the row is
// silently dropped.
void test_legacyHistoryDecryptsAeadPrimary()
{
    channels.getByIndex(0).settings.use_aead = true;

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "aead text", getValidTime(RTCQualityNTP, true));
    meshtastic_MeshPacket enc = makeEncryptedCopyCCM(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));
    updateStoredPayload(TEXT_ID, "");

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x1B1B1B1B, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));

    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);

    testSfpp->runOnce();
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward text;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], text));
    TEST_ASSERT_EQUAL_UINT32(TEXT_ID, text.original_id);
    TEST_ASSERT_EQUAL_MEMORY("aead text", text.variant.text.bytes, text.variant.text.size);
}

// The legacy shim pages from the last served rx_time, so a second request gets the next page.
void test_legacyHistoryPagesByCursor()
{
    TEST_ASSERT_TRUE(ingestNumberedTexts(60));

    meshtastic_MeshPacket first = makeHistoryRequest(0, LOCAL_NODE, 0x0E0E0E0E, 60);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(first));
    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(50, reply.variant.history.history_messages);
    const uint32_t cursor = reply.variant.history.last_request;
    TEST_ASSERT_GREATER_THAN_UINT32(0, cursor);

    // The serving window caps 30 frames, so let it roll over before draining the rest of the page.
    for (uint32_t i = 0; i < 100; i++) {
        if (i > 0 && i % 35 == 0)
            Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000 + 1);
        testSfpp->runOnce();
    }
    drainPhoneQueue();

    meshtastic_MeshPacket second = makeHistoryRequest(0, LOCAL_NODE, 0x0F0F0F0F, 60, cursor);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(second));
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward next;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], next));
    TEST_ASSERT_EQUAL_UINT32(10, next.variant.history.history_messages);
}

// Canon rows outside the 48 h window go even when the 1,000-row cap is not reached.

// An adopted broadcast text keeps its unsigned node number, so the shim serves it.
void test_shimServesAdoptedBroadcastText()
{
    TEST_ASSERT_TRUE(ingestOneText("adopted broadcast"));

    sqlite3 *db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    sqlite3_stmt *stmt = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK,
                          sqlite3_prepare_v2(db, "select destination from channel_messages limit 1;", -1, &stmt, nullptr));
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));
    TEST_ASSERT_EQUAL_INT64((sqlite3_int64)NODENUM_BROADCAST, sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x1D1D1D1D, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));
    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);

    testSfpp->runOnce();
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward text;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], text));
    TEST_ASSERT_EQUAL_UINT(strlen("adopted broadcast"), text.variant.text.size);
}

// Repair: a negative destination from an old store is converted and served on restart;
// a peers collision is resolved rather than left half-converted.
int storedSchemaVersion();
std::vector<std::string> filesMovedAside();

void test_oldVersionStoreIsSetAsideAndNewRowsAreUnsigned()
{
    // Simulate an old store: the v2 layout with a negative broadcast destination.
    TEST_ASSERT_TRUE(ingestOneText("old version row"));
    sqlite3 *db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db, "update channel_messages set destination=-1; pragma user_version=2;",
                                                  nullptr, nullptr, nullptr));
    sqlite3_close(db);

    delete testSfpp;
    testSfpp = new TestSfppModule(); // the version check moves the old store aside

    StoredRow oldRow;
    TEST_ASSERT_FALSE(readStoredRow(TEXT_ID, oldRow));
    TEST_ASSERT_EQUAL_INT(StoreForwardPlusPlusModule::kSchemaVersion, storedSchemaVersion());
    TEST_ASSERT_EQUAL_UINT(1, filesMovedAside().size());

    // A new broadcast row is stored unsigned and served by the shim.
    TEST_ASSERT_TRUE(ingestOneText("new broadcast row"));
    db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    sqlite3_stmt *stmt = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK,
                          sqlite3_prepare_v2(db, "select destination from channel_messages limit 1;", -1, &stmt, nullptr));
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));
    TEST_ASSERT_EQUAL_INT64((sqlite3_int64)NODENUM_BROADCAST, sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x1E1E1E1E, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));
    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);
}

// --- Coexistence with the legacy module ---

// A portnum-65 packet that is not a CLIENT_HISTORY belongs to the legacy client module (heartbeats,
// another server's ROUTER_TEXT_*). The shim owns the port but must pass these on, not consume them.
void test_shimPassesNonHistoryPort65ToLaterModules()
{
    meshtastic_StoreAndForward heartbeat = meshtastic_StoreAndForward_init_zero;
    heartbeat.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT;
    heartbeat.which_variant = meshtastic_StoreAndForward_heartbeat_tag;
    heartbeat.variant.heartbeat.period = 900;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = REMOTE_NODE;
    mp.to = LOCAL_NODE;
    mp.id = 0x0E0E0E0E;
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_API;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;
    mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                 &meshtastic_StoreAndForward_msg, &heartbeat);

    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->dispatch(mp));
    TEST_ASSERT_EQUAL_UINT(0, testRouter->sent.size());
    TEST_ASSERT_EQUAL_UINT(0, drainPhoneQueue().size());
}

// With no legacy server the shim must claim portnum 65; otherwise the shim can never run.
void test_shimClaimsPort65WithNoLegacyServer()
{
    meshtastic_MeshPacket sf = meshtastic_MeshPacket_init_zero;
    sf.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    sf.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;

    TEST_ASSERT_NULL(storeForwardModule);
    TEST_ASSERT_TRUE(testSfpp->wantPacket(&sf));
}

// When the legacy module is a server it owns portnum 65; SF++ must stand down so a phone receives
// exactly one history answer.
void test_shimYieldsPort65ToLegacyServer()
{
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
    moduleConfig.store_forward.enabled = true;
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;

    storeForwardModule = new StoreForwardModule();
    TEST_ASSERT_TRUE(storeForwardModule->isServer());

    meshtastic_MeshPacket sf = meshtastic_MeshPacket_init_zero;
    sf.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    sf.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;
    TEST_ASSERT_FALSE(testSfpp->wantPacket(&sf));

    delete storeForwardModule;
    storeForwardModule = nullptr;
#else
    TEST_IGNORE_MESSAGE("legacy Store & Forward module is not compiled in");
#endif
}

// A legacy module that is present but not a server never answers portnum 65, so the shim must still
// claim it. This is the common meshtasticd case where store_forward is off but SF++ is on.
void test_shimClaimsPort65WithLegacyClientOnly()
{
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
    moduleConfig.store_forward.enabled = false;

    storeForwardModule = new StoreForwardModule();
    TEST_ASSERT_FALSE(storeForwardModule->isServer());

    meshtastic_MeshPacket sf = meshtastic_MeshPacket_init_zero;
    sf.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    sf.decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;
    TEST_ASSERT_TRUE(testSfpp->wantPacket(&sf));

    delete storeForwardModule;
    storeForwardModule = nullptr;
#else
    TEST_IGNORE_MESSAGE("legacy Store & Forward module is not compiled in");
#endif
}

int storedSchemaVersion()
{
    sqlite3 *db = nullptr;
    int version = -1;
    if (sqlite3_open(dbFile().c_str(), &db) == SQLITE_OK) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
            version = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return version;
}

// Every file in the test's DB directory other than the live store and its journal.
std::vector<std::string> filesMovedAside()
{
    std::vector<std::string> out;
    if (DIR *d = opendir(dbDir.c_str())) {
        while (dirent *e = readdir(d)) {
            const std::string name = e->d_name;
            if (name != "." && name != ".." && name != "storeforwardpp.db" && name != "storeforwardpp.db-journal")
                out.push_back(dbDir + "/" + name);
        }
        closedir(d);
    }
    return out;
}

// A new store is stamped with the current schema, and a restart keeps what it holds.
void test_storeIsStampedAndKeptAcrossRestart()
{
    TEST_ASSERT_EQUAL_INT(StoreForwardPlusPlusModule::kSchemaVersion, storedSchemaVersion());
    TEST_ASSERT_TRUE(ingestOneText("kept across restart"));

    delete testSfpp;
    testSfpp = new TestSfppModule();

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_STRING("kept across restart", row.payload.c_str());
    TEST_ASSERT_EQUAL_UINT(0, filesMovedAside().size());
}

// A store from before the stamp (user_version 0) is moved aside whole and replaced by an empty one,
// so a schema change never meets old rows.
void test_unstampedStoreIsMovedAsideAndStartsEmpty()
{
    TEST_ASSERT_TRUE(ingestOneText("from the old schema"));
    delete testSfpp;
    testSfpp = nullptr;

    sqlite3 *db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db, "PRAGMA user_version = 0;", nullptr, nullptr, nullptr));
    sqlite3_close(db);

    testSfpp = new TestSfppModule();

    StoredRow row;
    TEST_ASSERT_FALSE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_INT(StoreForwardPlusPlusModule::kSchemaVersion, storedSchemaVersion());

    const std::vector<std::string> aside = filesMovedAside();
    TEST_ASSERT_EQUAL_UINT(1, aside.size());
    TEST_ASSERT_NOT_NULL(strstr(aside[0].c_str(), "storeforwardpp.db.v0-"));
    for (const std::string &f : aside)
        std::remove(f.c_str());
}

// The root is derived from the channel, so a follower with no stratum 0 gets one on its first run.
// Two archives with different node numbers and clocks derive the same root, which is what makes
// the stores comparable without an authority.
void test_root_is_derived_on_two_archives_without_stratum0()
{
    const std::vector<uint8_t> rootA = freshDerivedRoot(LOCAL_NODE, 1000);
    const std::vector<uint8_t> rootB = freshDerivedRoot(REMOTE_NODE, 123456789);

    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, rootA.size());
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, rootB.size());
    TEST_ASSERT_EQUAL_MEMORY(rootA.data(), rootB.data(), SFPP_HASH_SIZE);
}

// A different name or key is a different channel and must not share a store.
void test_root_changes_when_the_channel_identity_changes()
{
    const std::vector<uint8_t> base = freshDerivedRoot(LOCAL_NODE, 1000);
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, base.size());

    const std::string name = channels.getName(0);
    CryptoKey key = channels.getKey(0);
    setPrimaryChannel("sfpp-other", key.bytes, key.length);
    const std::vector<uint8_t> renamed = freshDerivedRoot(LOCAL_NODE, 1000);
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, renamed.size());
    TEST_ASSERT_TRUE(memcmp(base.data(), renamed.data(), SFPP_HASH_SIZE) != 0);

    uint8_t otherKey[16];
    memset(otherKey, 0xA5, sizeof(otherKey));
    setPrimaryChannel(name.c_str(), otherKey, sizeof(otherKey));
    const std::vector<uint8_t> rekeyed = freshDerivedRoot(LOCAL_NODE, 1000);
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, rekeyed.size());
    TEST_ASSERT_TRUE(memcmp(base.data(), rekeyed.data(), SFPP_HASH_SIZE) != 0);
}

// A mapping left by the old generated-root rule is not used: a v2 store whose channel mapping does
// not match the derived root is moved aside and a fresh store is created under the derived root.
void test_stale_mapping_moves_the_store_aside()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    testSfpp->runOnce();
    const std::vector<uint8_t> derived = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, derived.size());

    delete testSfpp;
    testSfpp = nullptr;
    uint8_t stale[SFPP_HASH_SIZE];
    memset(stale, 0x5C, sizeof(stale));
    TEST_ASSERT_TRUE(writeMappingRoot(stale));

    testSfpp = new TestSfppModule(); // the ctor sees the stale channel mapping and moves the file
    testSfpp->runOnce();

    const std::vector<uint8_t> after = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, after.size());
    TEST_ASSERT_EQUAL_MEMORY(derived.data(), after.data(), SFPP_HASH_SIZE);
    TEST_ASSERT_TRUE(memcmp(after.data(), stale, SFPP_HASH_SIZE) != 0);

    const std::vector<std::string> aside = filesMovedAside();
    TEST_ASSERT_EQUAL_UINT(1, aside.size());
    TEST_ASSERT_NOT_NULL(strstr(aside[0].c_str(), "storeforwardpp.db.root-"));
    for (const std::string &f : aside)
        std::remove(f.c_str());
}

// A later v2 PSK/name change is not a schema change: the old store is moved aside and the archive
// starts empty under the new channel root. The old rows are not served.
void test_channel_change_moves_the_v2_store_aside()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    TEST_ASSERT_TRUE(ingestOneText("served under the old channel"));
    const std::vector<uint8_t> oldRoot = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, oldRoot.size());
    delete testSfpp;
    testSfpp = nullptr;

    uint8_t otherKey[16];
    memset(otherKey, 0xA5, sizeof(otherKey));
    setPrimaryChannel("sfpp-new-key", otherKey, sizeof(otherKey));

    testSfpp = new TestSfppModule(); // the ctor sees a channel mapping for another root
    testSfpp->runOnce();
    const std::vector<uint8_t> newRoot = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, newRoot.size());
    TEST_ASSERT_TRUE(memcmp(oldRoot.data(), newRoot.data(), SFPP_HASH_SIZE) != 0);

    StoredRow row;
    TEST_ASSERT_FALSE(readStoredRow(TEXT_ID, row));
    const std::vector<std::string> aside = filesMovedAside();
    TEST_ASSERT_EQUAL_UINT(1, aside.size());
    TEST_ASSERT_NOT_NULL(strstr(aside[0].c_str(), "storeforwardpp.db.root-"));
    for (const std::string &f : aside)
        std::remove(f.c_str());
}

// Two channels that share the 1-byte hash must keep separate mapping rows in one store; keying the
// mapping by the hash would replace the first with the second.
void test_two_channels_sharing_a_hash_keep_two_mappings()
{
    portduino_config.sfpp_repair = SFPP_REPAIR_ENUM;
    testSfpp->runOnce();
    const std::vector<uint8_t> rootA = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, rootA.size());

    std::string name = channels.getName(0);
    TEST_ASSERT_GREATER_OR_EQUAL(2, name.size());
    std::swap(name[0], name[1]); // XOR hash is position-independent
    CryptoKey key = channels.getKey(0);
    TEST_ASSERT_EQUAL_INT(channels.getHash(0), channels.hashFor(0, name.c_str()));
    setPrimaryChannel(name.c_str(), key.bytes, key.length);

    testSfpp->runOnce();
    const std::vector<std::vector<uint8_t>> roots = mappingRoots();
    TEST_ASSERT_EQUAL_UINT(2, roots.size());
    bool sawA = false, sawB = false;
    for (const auto &root : roots) {
        sawA = sawA || memcmp(root.data(), rootA.data(), SFPP_HASH_SIZE) == 0;
        sawB = sawB || memcmp(root.data(), rootA.data(), SFPP_HASH_SIZE) != 0;
    }
    TEST_ASSERT_TRUE(sawA);
    TEST_ASSERT_TRUE(sawB);
}

// The root rule changes the store's identity, so the schema stamp moves to 2 and a v1 store is
// moved aside whole rather than served under the new root.
// The row is filed under the first byte of its message hash, the content bucket.
void test_stored_row_carries_the_content_bucket()
{
    TEST_ASSERT_TRUE(ingestOneText("bucket me"));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_UINT32(row.messageHash[0], storedBucket(TEXT_ID));
}

void test_versionOneStoreIsMovedAside()
{
    TEST_ASSERT_TRUE(ingestOneText("from version one"));
    delete testSfpp;
    testSfpp = nullptr;

    sqlite3 *db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr));
    sqlite3_close(db);

    testSfpp = new TestSfppModule();

    StoredRow row;
    TEST_ASSERT_FALSE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_INT(StoreForwardPlusPlusModule::kSchemaVersion, storedSchemaVersion());

    const std::vector<std::string> aside = filesMovedAside();
    TEST_ASSERT_EQUAL_UINT(1, aside.size());
    TEST_ASSERT_NOT_NULL(strstr(aside[0].c_str(), "storeforwardpp.db.v1-"));
    for (const std::string &f : aside)
        std::remove(f.c_str());
}

// --- relative stamps and margins ---

// No proper clock: the row is stamped in uptime seconds and the shim still serves it, with no epoch
// passed off as wall time.
void test_noClockIngestAndServe()
{
    setTestClock(RTCQualityNone, 0, 5000);
    recreateModule();

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "no clock", 0);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_UINT8(1, row.timeBasis);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, row.bootId);
    TEST_ASSERT_EQUAL_UINT32(5000, row.rxTime);

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x2A2A2A2A, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));
    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);

    testSfpp->runOnce(); // the queued text goes before any timedatectl promotion
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward text;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], text));
    TEST_ASSERT_FALSE(phone[0].has_rx_time); // the route's uptime placeholder, never shown as an epoch
    TEST_ASSERT_EQUAL_UINT(strlen("no clock"), text.variant.text.size);
    TEST_ASSERT_EQUAL_MEMORY("no clock", text.variant.text.bytes, text.variant.text.size);
}

// Mesh time is not a proper clock: the row stays boot-relative, but the shim may show the rough
// epoch it implies.
void test_meshTimeOnlyIngestStaysBootRelative()
{
    constexpr uint32_t EPOCH = 1800000000;
    setTestClock(RTCQualityFromNet, EPOCH, 5000);
    recreateModule();

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "mesh time", 0);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_UINT8(1, row.timeBasis);
    TEST_ASSERT_EQUAL_UINT32(5000, row.rxTime);

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x2B2B2B2B, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));
    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(1, reply.variant.history.history_messages);

    testSfpp->runOnce();
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward text;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], text));
    TEST_ASSERT_EQUAL_UINT32(EPOCH, phone[0].rx_time);
}

// A packet that arrived 400 s ago keeps that age; it is not stamped "now" on the way into the store.
void test_receivedAgeIsKept()
{
    constexpr uint32_t EPOCH = 1800000000;
    setTestClock(RTCQualityNTP, EPOCH, 5000);
    recreateModule();

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "aged text", EPOCH - 400);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_UINT8(0, row.timeBasis);
    TEST_ASSERT_EQUAL_UINT32(EPOCH - 400, row.rxTime);

    meshtastic_MeshPacket request = makeHistoryRequest(0, LOCAL_NODE, 0x2C2C2C2C, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(request));
    drainPhoneQueue();
    testSfpp->runOnce();
    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward text;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], text));
    TEST_ASSERT_EQUAL_UINT32(TEXT_ID, text.original_id);
}

// The first proper clock heals a boot-relative row to an epoch with its age intact, so the 48 h trim
// does not mistake the uptime stamp for an ancient epoch.
void test_healOnFirstProperClockDoesNotTrim()
{
    constexpr uint32_t EPOCH = 1800000000;
    setTestClock(RTCQualityNone, 0, 5000);
    recreateModule();

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "heal me", 4000);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));

    setTestClock(RTCQualityNTP, EPOCH, 5000);
    testSfpp->runOnce();

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_UINT8(0, row.timeBasis);
    TEST_ASSERT_EQUAL_UINT32(EPOCH - 1000, row.rxTime);
}

// A restart keeps the age of a boot-relative row and widens its margin because the downtime is
// unknown.
void test_restartKeepsAgeWithWidenedMargin()
{
    setTestClock(RTCQualityNone, 0, 5000);
    recreateModule();

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "restart me", 4000);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));

    setTestClock(RTCQualityNone, 0, 0); // a power cycle: uptime restarts
    recreateModule();

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_UINT8(2, row.timeBasis);
    TEST_ASSERT_EQUAL_UINT32(1000, row.rxTime);
    TEST_ASSERT_EQUAL_UINT32(LEGACY_RETENTION_S, row.ageMargin);
}

// A second restart with unknown downtime keeps the margin at the 48 h retention horizon, not
// another 48 h wider.
void test_unknownDowntimeMarginIsCapped()
{
    setTestClock(RTCQualityNone, 0, 5000);
    recreateModule();

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "margin cap", 4000);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));

    setTestClock(RTCQualityNone, 0, 0); // first power cycle, downtime unknown
    recreateModule();
    StoredRow first;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, first));
    TEST_ASSERT_EQUAL_UINT32(LEGACY_RETENTION_S, first.ageMargin);

    setTestClock(RTCQualityNone, 0, 0); // second power cycle, downtime still unknown
    recreateModule();
    StoredRow second;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, second));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(LEGACY_RETENTION_S, second.ageMargin);
}

// Mixed bases are aged through the one helper: the old epoch row goes, the fresh epoch and the
// boot-relative rows stay.
void test_mixedBasesTrimCorrectly()
{
    constexpr uint32_t EPOCH = 1800000000;
    constexpr PacketId FRESH_ID = TEXT_ID + 1;
    constexpr PacketId BOOT_ID = TEXT_ID + 2;
    setTestClock(RTCQualityNTP, EPOCH, 5000);
    recreateModule();

    meshtastic_MeshPacket old = makeText(REMOTE_NODE, TEXT_ID, "old epoch", EPOCH - LEGACY_RETENTION_S - 100);
    meshtastic_MeshPacket oldEnc = makeEncryptedCopy(old);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(old, &oldEnc));
    meshtastic_MeshPacket fresh = makeText(REMOTE_NODE, FRESH_ID, "fresh epoch", EPOCH);
    meshtastic_MeshPacket freshEnc = makeEncryptedCopy(fresh);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(fresh, &freshEnc));

    setTestClock(RTCQualityFromNet, EPOCH, 5000);
    recreateModule();
    meshtastic_MeshPacket boot = makeText(REMOTE_NODE, BOOT_ID, "boot row", 4940);
    meshtastic_MeshPacket bootEnc = makeEncryptedCopy(boot);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(boot, &bootEnc));

    // Keep the host's timedatectl from promoting in this no-proper-clock run.
    const char *path = getenv("PATH");
    const std::string savedPath = path ? path : "";
    setenv("PATH", "/nonexistent", 1);
    testSfpp->runOnce();
    setenv("PATH", savedPath.c_str(), 1);

    StoredRow oldRow, freshRow, bootRow;
    TEST_ASSERT_FALSE(readStoredRow(TEXT_ID, oldRow));
    TEST_ASSERT_TRUE(readStoredRow(FRESH_ID, freshRow));
    TEST_ASSERT_TRUE(readStoredRow(BOOT_ID, bootRow));
    TEST_ASSERT_EQUAL_UINT8(1, bootRow.timeBasis);
}

// Two pages of rows with no epoch must not overlap: the cursor advances by rowid when it cannot
// advance by a displayed epoch.
void test_noEpochHistoryPagesByRowid()
{
    constexpr uint32_t CURSOR_BASE = 0xF0000000;
    setTestClock(RTCQualityNone, 0, 5000);
    recreateModule();

    constexpr uint32_t ROWS = 60;
    for (uint32_t i = 0; i < ROWS; i++) {
        meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID + i, "paged no-epoch text", 0);
        meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
        TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));
    }

    meshtastic_MeshPacket first = makeHistoryRequest(0, LOCAL_NODE, 0x2D2D2D2D, 0);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(first));
    std::vector<meshtastic_MeshPacket> phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward reply;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], reply));
    TEST_ASSERT_EQUAL_UINT32(50, reply.variant.history.history_messages);
    const uint32_t cursor = reply.variant.history.last_request;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(CURSOR_BASE, cursor);

    for (uint32_t i = 0; i < 50; i++) {
        if (i > 0 && i % 25 == 0)
            Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000 + 1);
        testSfpp->runOnce();
    }
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(50, phone.size());
    std::vector<uint32_t> firstIds;
    for (const auto &p : phone) {
        meshtastic_StoreAndForward text;
        TEST_ASSERT_TRUE(decodeStoreForward(p, text));
        firstIds.push_back(text.original_id);
    }

    meshtastic_MeshPacket second = makeHistoryRequest(0, LOCAL_NODE, 0x2E2E2E2E, 0, cursor);
    TEST_ASSERT_EQUAL(ProcessMessage::STOP, testSfpp->dispatch(second));
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(1, phone.size());
    meshtastic_StoreAndForward next;
    TEST_ASSERT_TRUE(decodeStoreForward(phone[0], next));
    TEST_ASSERT_EQUAL_UINT32(10, next.variant.history.history_messages);
    TEST_ASSERT_NOT_EQUAL_UINT32(cursor, next.variant.history.last_request);

    Time::setTestMillis(Time::getMillis() + 10 * 60 * 1000 + 1);
    for (uint32_t i = 0; i < 10; i++)
        testSfpp->runOnce();
    phone = drainPhoneQueue();
    TEST_ASSERT_EQUAL_UINT(10, phone.size());
    for (const auto &p : phone) {
        meshtastic_StoreAndForward text;
        TEST_ASSERT_TRUE(decodeStoreForward(p, text));
        for (uint32_t id : firstIds)
            TEST_ASSERT_NOT_EQUAL_UINT32(id, text.original_id);
    }
}

// A stamp in the future is not aged as a negative interval; the future age is rejected, not stored.
void test_futureLookingAgeIsRejected()
{
    constexpr uint32_t EPOCH = 1800000000;
    setTestClock(RTCQualityNTP, EPOCH, 5000);
    recreateModule();

    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "future text", EPOCH + 500);
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));

    StoredRow row;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_UINT8(0, row.timeBasis);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(EPOCH, row.rxTime);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(EPOCH - 5, row.rxTime);
}

// The content-bucket schema bumps the stamp again; a v3 store is served history and is moved aside.
void test_v3StoreIsMovedAside()
{
    TEST_ASSERT_TRUE(ingestOneText("from version three"));
    delete testSfpp;
    testSfpp = nullptr;

    sqlite3 *db = nullptr;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(dbFile().c_str(), &db));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db, "PRAGMA user_version = 3;", nullptr, nullptr, nullptr));
    sqlite3_close(db);

    testSfpp = new TestSfppModule();

    StoredRow row;
    TEST_ASSERT_FALSE(readStoredRow(TEXT_ID, row));
    TEST_ASSERT_EQUAL_INT(StoreForwardPlusPlusModule::kSchemaVersion, storedSchemaVersion());

    const std::vector<std::string> aside = filesMovedAside();
    TEST_ASSERT_EQUAL_UINT(1, aside.size());
    TEST_ASSERT_NOT_NULL(strstr(aside[0].c_str(), "storeforwardpp.db.v3-"));
    for (const std::string &f : aside)
        std::remove(f.c_str());
}

// The content summary is a function of the held objects, not of a local counter or arrival order:
// two evaluations over the same holding agree.
void test_content_summary_matches_for_the_same_holding()
{
    // Two archives hold the same text; one also holds a row in another bucket. A counter-keyed
    // summary would select different rows, the content summary must not.
    TEST_ASSERT_TRUE(ingestOneText("same holding"));
    StoredRow target;
    TEST_ASSERT_TRUE(readStoredRow(TEXT_ID, target));
    const uint32_t bucket = target.bucket;

    PacketId paddingId = TEXT_ID + 1;
    uint32_t paddingBucket = bucket;
    for (int i = 0; i < 32 && paddingBucket == bucket; i++, paddingId++) {
        meshtastic_MeshPacket mp = makeText(REMOTE_NODE, paddingId, "padding", getValidTime(RTCQualityNTP, true));
        meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
        uint8_t hash[SFPP_HASH_SIZE];
        sfppframe::messageHash(enc.encrypted.bytes, enc.encrypted.size, mp.to, mp.from, mp.id, hash);
        paddingBucket = sfppsr::contentBucket(hash, SFPP_HASH_SIZE);
        if (paddingBucket != bucket)
            TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, testSfpp->ingest(mp, &enc));
    }
    TEST_ASSERT_TRUE(bucket != paddingBucket);

    char tmpl[] = "/tmp/meshtastic-sfpp-b-XXXXXX";
    char *dir = mkdtemp(tmpl);
    TEST_ASSERT_NOT_NULL(dir);
    const std::string secondDir = dir;
    const std::string savedPath = portduino_config.sfpp_db_path;
    portduino_config.sfpp_db_path = secondDir + "/";
    TestSfppModule *secondModule = new TestSfppModule();
    meshtastic_MeshPacket mp = makeText(REMOTE_NODE, TEXT_ID, "same holding", getValidTime(RTCQualityNTP, true));
    meshtastic_MeshPacket enc = makeEncryptedCopy(mp);
    TEST_ASSERT_EQUAL(ProcessMessage::CONTINUE, secondModule->ingest(mp, &enc));

    const std::vector<uint8_t> root = mappingRoot();
    TEST_ASSERT_EQUAL_UINT(SFPP_HASH_SIZE, root.size());

    sfppsr::BucketSummary a(4);
    sfppsr::BucketSummary b(4);
    TEST_ASSERT_TRUE(testSfpp->buildBucketSummary(root.data(), root.size(), bucket, a));
    TEST_ASSERT_TRUE(secondModule->buildBucketSummary(root.data(), root.size(), bucket, b));
    TEST_ASSERT_EQUAL_UINT32(1, a.count());
    TEST_ASSERT_EQUAL_UINT32(a.count(), b.count());
    TEST_ASSERT_EQUAL_UINT64(a.checksum(), b.checksum());

    sfppsr::Scope scope;
    memcpy(scope.rootPrefix, root.data(), root.size());
    scope.rootPrefixLen = root.size();
    scope.bucket = bucket;
    meshtastic_StoreForwardPlusPlus advertA = meshtastic_StoreForwardPlusPlus_init_zero;
    meshtastic_StoreForwardPlusPlus advertB = meshtastic_StoreForwardPlusPlus_init_zero;
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(scope, a, advertA));
    TEST_ASSERT_TRUE(sfppsr::buildAdvert(scope, b, advertB));
    TEST_ASSERT_EQUAL_UINT32(advertA.message.size, advertB.message.size);
    TEST_ASSERT_EQUAL_MEMORY(advertA.message.bytes, advertB.message.bytes, advertA.message.size);

    delete secondModule;
    portduino_config.sfpp_db_path = savedPath;
    std::remove((secondDir + "/storeforwardpp.db").c_str());
    std::remove((secondDir + "/storeforwardpp.db-journal").c_str());
    rmdir(secondDir.c_str());
}

void loop() {}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_ingestStoresTextWithPayloadAndHashes);
    RUN_TEST(test_text_is_canonical_without_stratum0);
    RUN_TEST(test_default_config_has_no_stratum0);
    RUN_TEST(test_archive_keeps_one_object_for_two_ciphertexts);
    RUN_TEST(test_sender_copy_moves_the_content_bucket);
    RUN_TEST(test_relay_copy_does_not_replace_the_sender_copy);
    RUN_TEST(test_identity_collision_never_adds_a_second_row);
    RUN_TEST(test_recovered_item_with_the_same_identity_is_one_object);
    RUN_TEST(test_live_text_keeps_one_object_for_two_ciphertexts);
    RUN_TEST(test_scratch_collision_across_buckets_is_not_promoted);
    RUN_TEST(test_storedMessageHashMatchesTheFrameHash);
    RUN_TEST(test_legacyHistoryAnswersInLegacyFormat);
    RUN_TEST(test_legacyHistoryUsesLegacyRequestWindow);
    RUN_TEST(test_legacyHistoryHeadersShareTheServeBudget);
    RUN_TEST(test_legacyHistoryExcludesTextsOlderThanRetention);
    RUN_TEST(test_legacyHistoryPagesByCursor);
    RUN_TEST(test_shimServesAdoptedBroadcastText);
    RUN_TEST(test_oldVersionStoreIsSetAsideAndNewRowsAreUnsigned);
    RUN_TEST(test_legacyHistoryDecryptsEmptyPayloadRow);
    RUN_TEST(test_legacyHistorySkipsNonTextCiphertext);
    RUN_TEST(test_legacyHistoryDecryptsAeadPrimary);
    RUN_TEST(test_shimPassesNonHistoryPort65ToLaterModules);
    RUN_TEST(test_shimClaimsPort65WithNoLegacyServer);
    RUN_TEST(test_shimYieldsPort65ToLegacyServer);
    RUN_TEST(test_shimClaimsPort65WithLegacyClientOnly);
    RUN_TEST(test_storeIsStampedAndKeptAcrossRestart);
    RUN_TEST(test_unstampedStoreIsMovedAsideAndStartsEmpty);
    RUN_TEST(test_root_is_derived_on_two_archives_without_stratum0);
    RUN_TEST(test_root_changes_when_the_channel_identity_changes);
    RUN_TEST(test_stale_mapping_moves_the_store_aside);
    RUN_TEST(test_channel_change_moves_the_v2_store_aside);
    RUN_TEST(test_two_channels_sharing_a_hash_keep_two_mappings);
    RUN_TEST(test_stored_row_carries_the_content_bucket);
    RUN_TEST(test_content_summary_matches_for_the_same_holding);
    RUN_TEST(test_versionOneStoreIsMovedAside);
    RUN_TEST(test_noClockIngestAndServe);
    RUN_TEST(test_meshTimeOnlyIngestStaysBootRelative);
    RUN_TEST(test_receivedAgeIsKept);
    RUN_TEST(test_healOnFirstProperClockDoesNotTrim);
    RUN_TEST(test_restartKeepsAgeWithWidenedMargin);
    RUN_TEST(test_unknownDowntimeMarginIsCapped);
    RUN_TEST(test_mixedBasesTrimCorrectly);
    RUN_TEST(test_noEpochHistoryPagesByRowid);
    RUN_TEST(test_futureLookingAgeIsRejected);

    RUN_TEST(test_v3StoreIsMovedAside);
    UNITY_END();
}

#endif // ARCH_PORTDUINO && !ARCH_PORTDUINO_WASM && !MESHTASTIC_EXCLUDE_SFPP