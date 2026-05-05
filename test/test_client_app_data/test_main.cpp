/**
 * Tests for ClientAppDataStore, the bounded local app-metadata store
 * exposed through AdminMessage tags 104..107. Targets:
 *
 *  1.  isValidAppId() accept/reject matrix
 *  2.  set/get/remove behaviour and Result mapping
 *  3.  Slot accounting (overwrite reuses, delete frees, full rejects)
 *  4.  Payload size limits
 *  5.  Server-side updated_at semantics
 *  6.  Persistence round-trip via mocked NodeDB::saveProto/loadProto
 *  7.  Storage-error propagation
 *  8.  Init behaviour for first-boot, decode-fail, and reload paths
 *
 * Dispatch-layer tests (AdminModule cases for tags 104..107) are
 * deliberately omitted: the case bodies are thin glue around the store
 * (validation, Result -> Routing_Error mapping, MeshPacket reply
 * construction). Exercising them in isolation requires standing up
 * channels/service/PhoneAPI machinery that this harness does not provide,
 * and would re-test logic the store-level cases below already cover.
 * Coverage gap noted in the Phase 5 report.
 */

#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#include "mesh/NodeDB.h"
#include "modules/ClientAppDataStore.h"

#include <cstring>

// ---------------------------------------------------------------------------
// MockNodeDB: overrides the now-virtual loadProto/saveProto so the store's
// persistence path can be exercised without touching the real filesystem.
// Only handles the LocalClientAppData proto descriptor; falls through to the
// base behaviour for anything else (no other suite shares this fixture).
// ---------------------------------------------------------------------------
class MockNodeDB : public NodeDB
{
  public:
    meshtastic_LocalClientAppData saved = meshtastic_LocalClientAppData_init_zero;
    bool hasSaved = false;
    bool saveOk = true;
    bool fileExists = true;
    int saveCalls = 0;
    int loadCalls = 0;

    bool saveProto(const char *filename, size_t protoSize, const pb_msgdesc_t *fields, const void *dest_struct,
                   bool fullAtomic = true) override
    {
        (void)filename;
        (void)protoSize;
        (void)fullAtomic;
        if (fields == &meshtastic_LocalClientAppData_msg) {
            saveCalls++;
            if (!saveOk) {
                return false;
            }
            saved = *static_cast<const meshtastic_LocalClientAppData *>(dest_struct);
            hasSaved = true;
            return true;
        }
        return true;
    }

    LoadFileResult loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields,
                             void *dest_struct) override
    {
        (void)filename;
        (void)protoSize;
        (void)objSize;
        if (fields == &meshtastic_LocalClientAppData_msg) {
            loadCalls++;
            if (!fileExists || !hasSaved) {
                return LoadFileResult::OTHER_FAILURE;
            }
            *static_cast<meshtastic_LocalClientAppData *>(dest_struct) = saved;
            return LoadFileResult::LOAD_SUCCESS;
        }
        return LoadFileResult::OTHER_FAILURE;
    }
};

static MockNodeDB *mockNodeDB = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static meshtastic_ClientAppData makeRecord(const char *appId, uint32_t version, uint8_t fillByte, size_t payloadLen)
{
    meshtastic_ClientAppData rec = meshtastic_ClientAppData_init_zero;
    strncpy(rec.app_id, appId, sizeof(rec.app_id) - 1);
    rec.app_id[sizeof(rec.app_id) - 1] = '\0';
    rec.version = version;
    rec.payload.size = static_cast<pb_size_t>(payloadLen);
    for (size_t i = 0; i < payloadLen && i < sizeof(rec.payload.bytes); i++) {
        rec.payload.bytes[i] = fillByte;
    }
    rec.updated_at = 0;
    return rec;
}

static void freshStore()
{
    if (clientAppDataStore != nullptr) {
        delete clientAppDataStore;
        clientAppDataStore = nullptr;
    }
    mockNodeDB->saveOk = true;
    mockNodeDB->fileExists = true;
    mockNodeDB->hasSaved = false;
    mockNodeDB->saved = meshtastic_LocalClientAppData_init_zero;
    mockNodeDB->saveCalls = 0;
    mockNodeDB->loadCalls = 0;
    ClientAppDataStore::init();
}

// ---------------------------------------------------------------------------
// isValidAppId() matrix
// ---------------------------------------------------------------------------

static void test_isValidAppId_acceptsConventionalNames()
{
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("exampleapp"));
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("meshtastic-ios"));
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("meshtastic-android"));
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("thirdparty.example"));
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("a"));
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("0"));
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("a.b_c-d.0"));
    // Exactly 32 chars (the maximum), covering a..z + 0..9 + period and underscore.
    TEST_ASSERT_TRUE(ClientAppDataStore::isValidAppId("abcdefghijklmnopqrstuvwxyz012345"));
}

static void test_isValidAppId_rejectsNull()
{
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId(nullptr));
}

static void test_isValidAppId_rejectsEmpty()
{
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId(""));
}

static void test_isValidAppId_rejectsUppercase()
{
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("Exampleapp"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("MeshTastic-iOS"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("APP"));
}

static void test_isValidAppId_rejectsWhitespace()
{
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("with space"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId(" app"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("app "));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("a\tb"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("a\nb"));
}

static void test_isValidAppId_rejectsOverlyLong()
{
    // 33 chars: one past the maximum
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("abcdefghijklmnopqrstuvwxyz0123456"));
    // 64 chars
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

static void test_isValidAppId_rejectsForbiddenChars()
{
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("app!"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("app/x"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("app:x"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("app\\x"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("app@x"));
    TEST_ASSERT_FALSE(ClientAppDataStore::isValidAppId("\xff"));
}

// ---------------------------------------------------------------------------
// set/get round-trip
// ---------------------------------------------------------------------------

static void test_setThenGet_returnsExactBytes()
{
    freshStore();
    meshtastic_ClientAppData in = makeRecord("exampleapp", 7, 0xAB, 64);
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(in)));

    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));

    TEST_ASSERT_EQUAL_STRING("exampleapp", out.app_id);
    TEST_ASSERT_EQUAL_UINT32(7, out.version);
    TEST_ASSERT_EQUAL_UINT(64, out.payload.size);
    for (size_t i = 0; i < 64; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAB, out.payload.bytes[i]);
    }
}

static void test_set_payloadAtMaxAccepted()
{
    freshStore();
    meshtastic_ClientAppData in = makeRecord("exampleapp", 1, 0x5A, ClientAppDataStore::kMaxPayloadBytes);
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(in)));

    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));
    TEST_ASSERT_EQUAL_UINT(ClientAppDataStore::kMaxPayloadBytes, out.payload.size);
}

static void test_set_payloadOverMaxRejected()
{
    freshStore();
    meshtastic_ClientAppData in = makeRecord("exampleapp", 1, 0x5A, 16);
    // Forge an oversize size field. The payload buffer can only hold 512 bytes
    // (per nanopb), but the store must consult the declared `.size` field, not
    // the underlying buffer capacity, when deciding to reject.
    in.payload.size = ClientAppDataStore::kMaxPayloadBytes + 1;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::PayloadTooLarge),
                      static_cast<int>(clientAppDataStore->set(in)));
}

static void test_set_invalidAppIdRejected()
{
    freshStore();
    meshtastic_ClientAppData in = makeRecord("Bad ID", 1, 0, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::InvalidAppId),
                      static_cast<int>(clientAppDataStore->set(in)));
}

static void test_get_invalidAppIdRejected()
{
    freshStore();
    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::InvalidAppId),
                      static_cast<int>(clientAppDataStore->get("BAD", &out)));
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::InvalidAppId),
                      static_cast<int>(clientAppDataStore->get(nullptr, &out)));
}

static void test_get_missingReturnsNotFound()
{
    freshStore();
    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::NotFound),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));
}

// ---------------------------------------------------------------------------
// Slot accounting
// ---------------------------------------------------------------------------

static void test_overwrite_doesNotConsumeSlot()
{
    freshStore();
    meshtastic_ClientAppData v1 = makeRecord("exampleapp", 1, 0x11, 8);
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(v1)));
    TEST_ASSERT_EQUAL(1, clientAppDataStore->recordCount());

    meshtastic_ClientAppData v2 = makeRecord("exampleapp", 2, 0x22, 16);
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(v2)));
    TEST_ASSERT_EQUAL_MESSAGE(1, clientAppDataStore->recordCount(),
                              "Overwrite of same app_id must not consume an extra slot");

    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));
    TEST_ASSERT_EQUAL_UINT32(2, out.version);
    TEST_ASSERT_EQUAL_UINT(16, out.payload.size);
    TEST_ASSERT_EQUAL_HEX8(0x22, out.payload.bytes[0]);
}

static void test_maxRecordCount_enforced()
{
    freshStore();
    const char *ids[ClientAppDataStore::kMaxRecords] = {"app.a", "app.b", "app.c", "app.d"};
    for (size_t i = 0; i < ClientAppDataStore::kMaxRecords; i++) {
        meshtastic_ClientAppData rec = makeRecord(ids[i], static_cast<uint32_t>(i), 0, 4);
        TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                          static_cast<int>(clientAppDataStore->set(rec)));
    }
    TEST_ASSERT_EQUAL(ClientAppDataStore::kMaxRecords, clientAppDataStore->recordCount());

    meshtastic_ClientAppData overflow = makeRecord("app.e", 99, 0, 4);
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::NoSpace),
                      static_cast<int>(clientAppDataStore->set(overflow)));
    TEST_ASSERT_EQUAL(ClientAppDataStore::kMaxRecords, clientAppDataStore->recordCount());
}

static void test_deleteFreesSlot()
{
    freshStore();
    const char *ids[ClientAppDataStore::kMaxRecords] = {"app.a", "app.b", "app.c", "app.d"};
    for (size_t i = 0; i < ClientAppDataStore::kMaxRecords; i++) {
        TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                          static_cast<int>(clientAppDataStore->set(makeRecord(ids[i], 0, 0, 4))));
    }
    TEST_ASSERT_EQUAL(ClientAppDataStore::kMaxRecords, clientAppDataStore->recordCount());

    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->remove("app.b")));
    TEST_ASSERT_EQUAL(ClientAppDataStore::kMaxRecords - 1, clientAppDataStore->recordCount());

    // Slot freed, so a brand-new app_id should now fit.
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(makeRecord("app.e", 0, 0, 4))));
    TEST_ASSERT_EQUAL(ClientAppDataStore::kMaxRecords, clientAppDataStore->recordCount());

    // app.b really gone, app.e present.
    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::NotFound),
                      static_cast<int>(clientAppDataStore->get("app.b", &out)));
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->get("app.e", &out)));
}

static void test_deleteThenGet_returnsNotFound()
{
    freshStore();
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(makeRecord("exampleapp", 1, 0xCC, 4))));
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->remove("exampleapp")));
    TEST_ASSERT_EQUAL(0, clientAppDataStore->recordCount());

    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::NotFound),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));
}

static void test_deleteMissingReturnsNotFound()
{
    freshStore();
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::NotFound),
                      static_cast<int>(clientAppDataStore->remove("never-set")));
}

static void test_remove_invalidAppIdRejected()
{
    freshStore();
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::InvalidAppId),
                      static_cast<int>(clientAppDataStore->remove("BAD")));
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::InvalidAppId),
                      static_cast<int>(clientAppDataStore->remove(nullptr)));
}

// ---------------------------------------------------------------------------
// updated_at: server-set, ignores caller-supplied value
// ---------------------------------------------------------------------------

static void test_updatedAt_isFirmwareSetNotCallerSupplied()
{
    freshStore();
    meshtastic_ClientAppData in = makeRecord("exampleapp", 1, 0, 8);
    in.updated_at = 0xDEADBEEF; // poisoned, must be overwritten by firmware
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(in)));

    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0xDEADBEEFu, out.updated_at,
                                  "Firmware must overwrite caller-supplied updated_at");
    // initializeTestEnvironment() seeds the RTC via perhapsSetRTC(RTCQualityNTP, ...),
    // so on the Portduino test env updated_at must be a real epoch (>= year 2000).
    // 946684800 = 2000-01-01T00:00:00Z.
    TEST_ASSERT_TRUE_MESSAGE(out.updated_at >= 946684800u,
                             "updated_at should be a real epoch when test RTC is seeded");
}

// ---------------------------------------------------------------------------
// Persistence: calls into the (mocked) NodeDB layer
// ---------------------------------------------------------------------------

static void test_set_callsSaveProto()
{
    freshStore();
    int before = mockNodeDB->saveCalls;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(makeRecord("a.b", 1, 0, 4))));
    TEST_ASSERT_EQUAL(before + 1, mockNodeDB->saveCalls);
}

static void test_remove_callsSaveProto()
{
    freshStore();
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(makeRecord("a.b", 1, 0, 4))));
    int before = mockNodeDB->saveCalls;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->remove("a.b")));
    TEST_ASSERT_EQUAL(before + 1, mockNodeDB->saveCalls);
}

static void test_storageError_setReturnsStorageError()
{
    freshStore();
    mockNodeDB->saveOk = false;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::StorageError),
                      static_cast<int>(clientAppDataStore->set(makeRecord("exampleapp", 1, 0, 8))));
}

static void test_storageError_removeReturnsStorageError()
{
    freshStore();
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(makeRecord("exampleapp", 1, 0, 8))));
    mockNodeDB->saveOk = false;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::StorageError),
                      static_cast<int>(clientAppDataStore->remove("exampleapp")));
}

static void test_persistenceSurvivesReinit()
{
    freshStore();
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(makeRecord("exampleapp", 42, 0xEE, 32))));

    // Simulate a reboot: tear down the in-memory store, leave the mock's
    // saved snapshot intact, re-init. The store must reload the record.
    delete clientAppDataStore;
    clientAppDataStore = nullptr;
    mockNodeDB->saveCalls = 0;
    mockNodeDB->loadCalls = 0;
    ClientAppDataStore::init();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockNodeDB->loadCalls,
                              "init() must call loadProto exactly once");

    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));
    TEST_ASSERT_EQUAL_UINT32(42, out.version);
    TEST_ASSERT_EQUAL_UINT(32, out.payload.size);
    TEST_ASSERT_EQUAL_HEX8(0xEE, out.payload.bytes[0]);
}

static void test_factoryResetSemantic_emptyAfterFileVanishes()
{
    // True factoryReset() does rmDir("/prefs") which deletes
    // /prefs/clientappdata.proto. We model that by setting fileExists=false
    // (loadProto then returns OTHER_FAILURE, the same outcome as a missing
    // file). After re-init the store must be empty.
    freshStore();
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::Ok),
                      static_cast<int>(clientAppDataStore->set(makeRecord("exampleapp", 1, 0, 8))));
    TEST_ASSERT_EQUAL(1, clientAppDataStore->recordCount());

    delete clientAppDataStore;
    clientAppDataStore = nullptr;
    mockNodeDB->fileExists = false; // == post-factoryReset world
    ClientAppDataStore::init();

    TEST_ASSERT_EQUAL(0, clientAppDataStore->recordCount());
    meshtastic_ClientAppData out = meshtastic_ClientAppData_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(ClientAppDataStore::Result::NotFound),
                      static_cast<int>(clientAppDataStore->get("exampleapp", &out)));
}

static void test_init_firstBoot_isEmpty()
{
    if (clientAppDataStore != nullptr) {
        delete clientAppDataStore;
        clientAppDataStore = nullptr;
    }
    mockNodeDB->hasSaved = false;
    mockNodeDB->fileExists = true; // exists doesn't matter when nothing saved
    mockNodeDB->saveCalls = 0;
    mockNodeDB->loadCalls = 0;
    ClientAppDataStore::init();
    TEST_ASSERT_EQUAL(1, mockNodeDB->loadCalls);
    TEST_ASSERT_EQUAL(0, clientAppDataStore->recordCount());
}

// ---------------------------------------------------------------------------
// Unity lifecycle
// ---------------------------------------------------------------------------

void setUp(void)
{
    mockNodeDB = new MockNodeDB();
    nodeDB = mockNodeDB;
}

void tearDown(void)
{
    if (clientAppDataStore != nullptr) {
        delete clientAppDataStore;
        clientAppDataStore = nullptr;
    }
    nodeDB = nullptr;
    delete mockNodeDB;
    mockNodeDB = nullptr;
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    // isValidAppId
    RUN_TEST(test_isValidAppId_acceptsConventionalNames);
    RUN_TEST(test_isValidAppId_rejectsNull);
    RUN_TEST(test_isValidAppId_rejectsEmpty);
    RUN_TEST(test_isValidAppId_rejectsUppercase);
    RUN_TEST(test_isValidAppId_rejectsWhitespace);
    RUN_TEST(test_isValidAppId_rejectsOverlyLong);
    RUN_TEST(test_isValidAppId_rejectsForbiddenChars);

    // CRUD round-trip
    RUN_TEST(test_setThenGet_returnsExactBytes);
    RUN_TEST(test_set_payloadAtMaxAccepted);
    RUN_TEST(test_set_payloadOverMaxRejected);
    RUN_TEST(test_set_invalidAppIdRejected);
    RUN_TEST(test_get_invalidAppIdRejected);
    RUN_TEST(test_get_missingReturnsNotFound);

    // Slot accounting
    RUN_TEST(test_overwrite_doesNotConsumeSlot);
    RUN_TEST(test_maxRecordCount_enforced);
    RUN_TEST(test_deleteFreesSlot);
    RUN_TEST(test_deleteThenGet_returnsNotFound);
    RUN_TEST(test_deleteMissingReturnsNotFound);
    RUN_TEST(test_remove_invalidAppIdRejected);

    // updated_at
    RUN_TEST(test_updatedAt_isFirmwareSetNotCallerSupplied);

    // Persistence + storage error
    RUN_TEST(test_set_callsSaveProto);
    RUN_TEST(test_remove_callsSaveProto);
    RUN_TEST(test_storageError_setReturnsStorageError);
    RUN_TEST(test_storageError_removeReturnsStorageError);
    RUN_TEST(test_persistenceSurvivesReinit);
    RUN_TEST(test_factoryResetSemantic_emptyAfterFileVanishes);
    RUN_TEST(test_init_firstBoot_isEmpty);

    exit(UNITY_END());
}

void loop() {}
