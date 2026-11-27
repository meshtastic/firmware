#include "TestUtil.h"
// FSCommon.h is what defines FSCom, so it has to be included before anything tests for it.
#include "FSCommon.h"
#include "SPILock.h"
#include "modules/Telemetry/AirQualityTelemetry.h"
#include "modules/Telemetry/TelemetryStore.h"
#include <unity.h>

#ifdef FSCom
#include "modules/Telemetry/FileTelemetryStore.h"
#endif

// AirQualityTelemetryModule's mesh policy, and the TelemetryStore contract run against every
// backend since the module must not be able to tell them apart. The read cadence lives in
// TelemetryScheduler, tested in test_telemetry_scheduler.

// ------------------------------------------------------------------ offload

static void test_mesh_publishesUnsentReadingWhenDueAndAllowed()
{
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldSendToMesh(true, true, true));
}

// Between offloads the local loop keeps reading, but nothing goes on air.
static void test_mesh_holdsUntilDue()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldSendToMesh(true, false, true));
}

static void test_mesh_holdsWhileAirtimeVetoes()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldSendToMesh(true, true, false));
}

// The same reading is never broadcast twice; once offloaded there is nothing left to publish.
static void test_mesh_doesNotResendTheSameReading()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldSendToMesh(false, true, true));
}

// ----------------------------------------------------------- store contract

// Every backend has to behave identically here: the module holds a TelemetryStore<T> and must not be
// able to tell RAM from a file. Each check names the backend so a failure says which one broke.
// The store is expected to be empty on entry and is left full.
static void checkStoreContract(TelemetryStore<uint32_t> &s, const char *backend)
{
    TelemetryReading<uint32_t> r;

    TEST_ASSERT_TRUE_MESSAGE(s.isEmpty(), backend);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, s.size(), backend);
    TEST_ASSERT_FALSE_MESSAGE(s.newest(r), backend);
    // Nothing read yet is not the same as a reading nobody has taken.
    TEST_ASSERT_FALSE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH), backend);
    TEST_ASSERT_FALSE_MESSAGE(s.at(0, r), backend);

    // Capture time travels with the reading it belongs to.
    // So does the gap since the previous stored reading, 0 when unknown.
    TEST_ASSERT_TRUE_MESSAGE(s.push(11U, 1000U), backend);
    TEST_ASSERT_TRUE_MESSAGE(s.push(22U, 1060U, 60U), backend);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, s.size(), backend);

    TEST_ASSERT_TRUE_MESSAGE(s.newest(r), backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(22U, r.metrics, backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1060U, r.time, backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(60U, r.deltaSecs, backend);

    // Oldest first, so a batched offload can walk them in the order they were taken.
    TEST_ASSERT_TRUE_MESSAGE(s.at(0, r), backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(11U, r.metrics, backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000U, r.time, backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, r.deltaSecs, backend);

    // Each consumer tracks what it has taken independently.
    TEST_ASSERT_TRUE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH), backend);
    TEST_ASSERT_TRUE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_PHONE), backend);
    s.markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
    TEST_ASSERT_FALSE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_PHONE), backend);
    TEST_ASSERT_TRUE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH), backend);

    // Marking by index addresses oldest-first, not raw slots.
    s.markPublished(0, TELEMETRY_PUBLISHED_MESH);
    TEST_ASSERT_TRUE_MESSAGE(s.at(0, r), backend);
    TEST_ASSERT_TRUE_MESSAGE(r.publishedMask & TELEMETRY_PUBLISHED_MESH, backend);
    TEST_ASSERT_TRUE_MESSAGE(s.at(1, r), backend);
    TEST_ASSERT_FALSE_MESSAGE(r.publishedMask & TELEMETRY_PUBLISHED_MESH, backend);

    // The direct uplink is a third, independent consumer.
    s.markPublished(1, TELEMETRY_PUBLISHED_MQTT);
    TEST_ASSERT_TRUE_MESSAGE(s.at(1, r), backend);
    TEST_ASSERT_TRUE_MESSAGE(r.publishedMask & TELEMETRY_PUBLISHED_MQTT, backend);
    TEST_ASSERT_FALSE_MESSAGE(r.publishedMask & TELEMETRY_PUBLISHED_MESH, backend);
    TEST_ASSERT_TRUE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH), backend);

    // Filling past capacity drops the oldest and keeps the order. That is the steady state: the
    // offload samples the loop rather than draining it.
    const uint16_t cap = s.capacity();
    for (uint32_t i = 0; i < (uint32_t)cap + 3U; i++)
        TEST_ASSERT_TRUE_MESSAGE(s.push(100U + i, 2000U + i), backend);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(cap, s.size(), backend);
    TEST_ASSERT_TRUE_MESSAGE(s.at(0, r), backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100U + 3U, r.metrics, backend); // oldest survivor
    TEST_ASSERT_TRUE_MESSAGE(s.newest(r), backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100U + cap + 2U, r.metrics, backend);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2000U + cap + 2U, r.time, backend);

    // A reading landing in a slot whose previous occupant had been published starts unpublished.
    TEST_ASSERT_TRUE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH), backend);
    TEST_ASSERT_TRUE_MESSAGE(s.hasUnpublishedNewest(TELEMETRY_PUBLISHED_PHONE), backend);
    TEST_ASSERT_FALSE_MESSAGE(s.at(cap, r), backend); // one past the end
}

static void test_store_ramHonoursContract()
{
    RamTelemetryStore<uint32_t> s(6);
    TEST_ASSERT_EQUAL_UINT16(6, s.capacity());
    checkStoreContract(s, "ram");
}

// A store that could not get its memory stores nothing rather than faulting on every push.
static void test_store_ramWithoutCapacityStoresNothing()
{
    RamTelemetryStore<uint32_t> s(0);
    TelemetryReading<uint32_t> r;
    TEST_ASSERT_EQUAL_UINT16(0, s.capacity());
    TEST_ASSERT_FALSE(s.push(11U, 1000U));
    TEST_ASSERT_TRUE(s.isEmpty());
    TEST_ASSERT_FALSE(s.newest(r));
}

#ifdef FSCom
static const char *kStorePath = "/test_aq_store.bin";

static void freshStoreFile()
{
    if (FSCom.exists(kStorePath))
        FSCom.remove(kStorePath);
}

static void test_store_fileHonoursContract()
{
    freshStoreFile();
    // Named filesystem rather than the default, which is what a variant pointing this at SD or at a
    // PSRAM filesystem does.
    auto *store = makeFileTelemetryStore<uint32_t>(kStorePath, 6, FSCom);
    FileTelemetryStore<uint32_t> &s = *store;
    TEST_ASSERT_TRUE(s.isUsable());
    TEST_ASSERT_EQUAL_UINT16(6, s.capacity());
    checkStoreContract(s, "file");
    delete store;
}

// The reason the file backend exists: readings outlive the process that took them, so an offline
// node can keep measuring across the deep sleep it takes between offloads.
static void test_store_fileSurvivesReopen()
{
    freshStoreFile();
    {
        FileTelemetryStore<uint32_t> s(kStorePath, 4);
        TEST_ASSERT_TRUE(s.push(11U, 1000U));
        TEST_ASSERT_TRUE(s.push(22U, 1060U));
        s.markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
    }

    FileTelemetryStore<uint32_t> s(kStorePath, 4);
    TelemetryReading<uint32_t> r;
    TEST_ASSERT_TRUE(s.isUsable());
    TEST_ASSERT_EQUAL_UINT16(2, s.size());
    TEST_ASSERT_TRUE(s.at(0, r));
    TEST_ASSERT_EQUAL_UINT32(11U, r.metrics);
    TEST_ASSERT_EQUAL_UINT32(1000U, r.time);
    // Who has already taken a reading survives too, so a reboot does not resend the backlog.
    TEST_ASSERT_TRUE(s.newest(r));
    TEST_ASSERT_EQUAL_UINT32(22U, r.metrics);
    TEST_ASSERT_TRUE(r.publishedMask & TELEMETRY_PUBLISHED_PHONE);
    TEST_ASSERT_FALSE(r.publishedMask & TELEMETRY_PUBLISHED_MESH);
}

// Reopening at a different capacity cannot reuse the slots, so the file is rebuilt rather than
// decoded as nonsense. Same path as a firmware build whose payload struct changed size.
static void test_store_fileRebuiltWhenGeometryChanges()
{
    freshStoreFile();
    {
        FileTelemetryStore<uint32_t> s(kStorePath, 4);
        TEST_ASSERT_TRUE(s.push(11U, 1000U));
        TEST_ASSERT_EQUAL_UINT16(1, s.size());
    }

    FileTelemetryStore<uint32_t> s(kStorePath, 8);
    TEST_ASSERT_TRUE(s.isUsable());
    TEST_ASSERT_EQUAL_UINT16(8, s.capacity());
    TEST_ASSERT_TRUE(s.isEmpty());
}

// Same contract as the RAM store at zero capacity: nothing usable, and a push that declines
// rather than dividing by a zero slot count.
static void test_store_fileWithoutCapacityStoresNothing()
{
    freshStoreFile();
    FileTelemetryStore<uint32_t> s(kStorePath, 0);
    TelemetryReading<uint32_t> r;
    TEST_ASSERT_FALSE(s.isUsable());
    TEST_ASSERT_EQUAL_UINT16(0, s.capacity());
    TEST_ASSERT_FALSE(s.push(11U, 1000U));
    TEST_ASSERT_TRUE(s.isEmpty());
    TEST_ASSERT_FALSE(s.newest(r));
}

// A file cut short of the geometry its header claims, as an interrupted preallocation would leave
// it, is rebuilt rather than read past the end.
static void test_store_fileRebuiltWhenTruncated()
{
    freshStoreFile();
    {
        FileTelemetryStore<uint32_t> s(kStorePath, 6);
        TEST_ASSERT_TRUE(s.push(11U, 1000U));
    }

    // Keep the header, drop every slot behind it
    uint8_t header[FileTelemetryStore<uint32_t>::headerBytes()];
    File r = FSCom.open(kStorePath, FILE_O_READ);
    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL_INT(sizeof(header), r.read(header, sizeof(header)));
    r.close();

    File w = FSCom.open(kStorePath, FILE_O_WRITE); // truncates
    TEST_ASSERT_TRUE(w);
    // Assert it, or a failed write leaves an empty file that rebuilds for the wrong reason
    TEST_ASSERT_EQUAL_INT(sizeof(header), w.write(header, sizeof(header)));
    w.close();

    FileTelemetryStore<uint32_t> s(kStorePath, 6);
    TEST_ASSERT_TRUE(s.isUsable());
    TEST_ASSERT_TRUE(s.isEmpty());
}

// Ring order is derived from the records, so a wrapped ring reopens in the same order it had, with
// no separately committed header to disagree with them.
static void test_store_fileKeepsOrderAcrossReopenWhenFull()
{
    freshStoreFile();
    {
        FileTelemetryStore<uint32_t> s(kStorePath, 4);
        for (uint32_t i = 1; i <= 6U; i++) // two past capacity, so head is off zero
            TEST_ASSERT_TRUE(s.push(100U + i, 2000U + i));
    }

    FileTelemetryStore<uint32_t> s(kStorePath, 4);
    TelemetryReading<uint32_t> r;
    TEST_ASSERT_TRUE(s.isUsable());
    TEST_ASSERT_EQUAL_UINT16(4, s.size());
    for (uint16_t i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(s.at(i, r));
        TEST_ASSERT_EQUAL_UINT32(103U + i, r.metrics);
        TEST_ASSERT_EQUAL_UINT32(2003U + i, r.time);
    }

    // And a push after reopening continues the ring rather than restarting it
    TEST_ASSERT_TRUE(s.push(999U, 3000U));
    TEST_ASSERT_EQUAL_UINT16(4, s.size());
    TEST_ASSERT_TRUE(s.at(0, r));
    TEST_ASSERT_EQUAL_UINT32(104U, r.metrics);
    TEST_ASSERT_TRUE(s.newest(r));
    TEST_ASSERT_EQUAL_UINT32(999U, r.metrics);
}

// A record torn mid-write keeps a plausible leading seq, so the trailing copy is what catches it.
// Reopening must skip that slot rather than serve half a reading.
static void test_store_fileRejectsTornRecord()
{
    freshStoreFile();
    constexpr uint32_t kHeaderBytes = FileTelemetryStore<uint32_t>::headerBytes();
    constexpr uint16_t kSlots = 6;
    {
        FileTelemetryStore<uint32_t> s(kStorePath, kSlots);
        TEST_ASSERT_TRUE(s.push(11U, 1000U));
        TEST_ASSERT_TRUE(s.push(22U, 1060U));
    }

    File f = FSCom.open(kStorePath, FILE_O_READ);
    TEST_ASSERT_TRUE(f);
    const uint32_t recordBytes = (f.size() - kHeaderBytes) / kSlots;
    f.close();

    // seqEnd is the last field, so the record's trailing word is the copy to disagree with
    File w = FSCom.open(kStorePath, "r+");
    TEST_ASSERT_TRUE(w);
    w.seek(kHeaderBytes + recordBytes - 4);
    const uint8_t garbage[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_INT(sizeof(garbage), w.write(garbage, sizeof(garbage)));
    w.close();

    FileTelemetryStore<uint32_t> s(kStorePath, kSlots);
    TelemetryReading<uint32_t> r;
    TEST_ASSERT_TRUE(s.isUsable());
    TEST_ASSERT_EQUAL_UINT16(1, s.size()); // the torn one is gone, the intact one survives
    TEST_ASSERT_TRUE(s.at(0, r));
    TEST_ASSERT_EQUAL_UINT32(22U, r.metrics);
    TEST_ASSERT_EQUAL_UINT32(1060U, r.time);
}

// Preallocated at creation, so a full store costs the same as an empty one and cannot fill the
// filesystem later.
static void test_store_fileDoesNotGrowWithUse()
{
    freshStoreFile();
    FileTelemetryStore<uint32_t> s(kStorePath, 4);

    File f = FSCom.open(kStorePath, FILE_O_READ);
    TEST_ASSERT_TRUE(f);
    const size_t emptySize = f.size();
    f.close();

    for (uint32_t i = 0; i < 20U; i++)
        TEST_ASSERT_TRUE(s.push(i, 1000U + i));

    f = FSCom.open(kStorePath, FILE_O_READ);
    TEST_ASSERT_TRUE(f);
    TEST_ASSERT_EQUAL_UINT32(emptySize, f.size());
    f.close();
}
#endif // FSCom

void setUp(void) {}

void tearDown(void)
{
#ifdef FSCom
    // Leave no residue: run-tests.sh flags a suite that writes a path it has not declared
    freshStoreFile();
#endif
}

extern "C" {
void setup()
{
    initializeTestEnvironment();
#ifdef FSCom
    // FileTelemetryStore brackets every FSCom touch with spiLock; nothing in the test environment
    // creates it, so do it here (initSPI asserts it only runs once).
    if (!spiLock)
        initSPI();
#endif
    UNITY_BEGIN();
    RUN_TEST(test_mesh_publishesUnsentReadingWhenDueAndAllowed);
    RUN_TEST(test_mesh_holdsUntilDue);
    RUN_TEST(test_mesh_holdsWhileAirtimeVetoes);
    RUN_TEST(test_mesh_doesNotResendTheSameReading);
    RUN_TEST(test_store_ramHonoursContract);
    RUN_TEST(test_store_ramWithoutCapacityStoresNothing);
#ifdef FSCom
    RUN_TEST(test_store_fileHonoursContract);
    RUN_TEST(test_store_fileSurvivesReopen);
    RUN_TEST(test_store_fileRebuiltWhenGeometryChanges);
    RUN_TEST(test_store_fileRebuiltWhenTruncated);
    RUN_TEST(test_store_fileKeepsOrderAcrossReopenWhenFull);
    RUN_TEST(test_store_fileRejectsTornRecord);
    RUN_TEST(test_store_fileWithoutCapacityStoresNothing);
    RUN_TEST(test_store_fileDoesNotGrowWithUse);
#endif
    exit(UNITY_END());
}

void loop() {}
}
