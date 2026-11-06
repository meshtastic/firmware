// Covers SqliteDb::open()/migrate() in src/modules/Native/SqliteDb.cpp and the schema bring-up in
// MeshMapModule's constructor (src/modules/Native/MeshMapModule.cpp).
//
// Why: meshmap.db outlives the binary that created it. An upgraded daemon must carry an existing
// database forward through PRAGMA user_version without re-running earlier migrations; an older
// build must refuse a database newer than it understands rather than write rows of the wrong shape;
// a migration that fails part way must leave the previous version whole; and any database failure
// must leave the module inert instead of stopping meshtasticd (the closed #9041 called exit()).
//
// Regressions guarded: v1's CREATE TABLEs re-run on every boot, which fails with "table already
// exists" and disables the map from the first restart on; a half-applied migration that bumps or
// keeps tables without its version; an older build quietly opening a newer schema; WAL not engaged,
// which puts a rollback journal and an fsync per transaction on a Pi's SD card.
#include "TestUtil.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unity.h>

#include "modules/Native/MeshMapModule.h"

#if ARCH_PORTDUINO && HAS_SQLITE3

static std::string dbPath;

static bool tableExists(sqlite3 *db, const char *name)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

static void removeDbFiles()
{
    for (const char *suffix : {"", "-wal", "-shm", "-journal"})
        std::remove((dbPath + suffix).c_str());
}

void setUp(void)
{
    const char *home = getenv("HOME");
    dbPath = SqliteDb::pathIn(home ? home : ".", "test_meshmap_schema.db");
    removeDbFiles();
}

void tearDown(void)
{
    removeDbFiles();
}

void test_fresh_database_reaches_current_schema(void)
{
    MeshMapModule map(dbPath);

    TEST_ASSERT_TRUE(map.isReady());
    TEST_ASSERT_EQUAL_INT(MeshMapModule::schemaVersion(), map.database().userVersion());
    for (const char *table : {"rx_frame", "node", "link", "remote_report", "position"})
        TEST_ASSERT_TRUE_MESSAGE(tableExists(map.database().handle(), table), table);
}

// The second construction is the boot after an upgrade with nothing new to apply: it must not
// re-run v1, and the rows from the previous run must still be there.
void test_reopen_keeps_schema_and_rows(void)
{
    {
        MeshMapModule first(dbPath);
        TEST_ASSERT_TRUE(first.isReady());
        TEST_ASSERT_TRUE(first.database().exec("INSERT INTO node(nodenum, first_heard, last_heard) VALUES(4294967295, 1, 2);"));
    }

    MeshMapModule second(dbPath);
    TEST_ASSERT_TRUE(second.isReady());
    TEST_ASSERT_EQUAL_INT(MeshMapModule::schemaVersion(), second.database().userVersion());

    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(second.database().handle(), "SELECT nodenum FROM node;", -1, &stmt, nullptr);
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));
    // 0xffffffff must come back unsigned: INTEGER holds it as int64, not a wrapped int32.
    TEST_ASSERT_EQUAL_INT64(4294967295LL, sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
}

void test_newer_schema_is_refused_and_left_untouched(void)
{
    const int future = MeshMapModule::schemaVersion() + 1;
    {
        SqliteDb raw;
        TEST_ASSERT_TRUE(raw.open(dbPath));
        TEST_ASSERT_TRUE(raw.exec(("PRAGMA user_version=" + std::to_string(future) + ";").c_str()));
    }

    MeshMapModule map(dbPath);
    TEST_ASSERT_FALSE(map.isReady());

    SqliteDb raw;
    TEST_ASSERT_TRUE(raw.open(dbPath));
    TEST_ASSERT_EQUAL_INT(future, raw.userVersion());
    TEST_ASSERT_FALSE(tableExists(raw.handle(), "rx_frame"));
}

// Step two fails after creating a table, so both its DDL and its version bump must roll back.
void test_failed_migration_rolls_back_to_previous_version(void)
{
    static const char *const steps[] = {
        "CREATE TABLE a(x);",
        "CREATE TABLE b(x); THIS IS NOT SQL;",
    };

    SqliteDb db;
    TEST_ASSERT_TRUE(db.open(dbPath));
    TEST_ASSERT_FALSE(db.migrate(steps, 2));

    TEST_ASSERT_EQUAL_INT(1, db.userVersion());
    TEST_ASSERT_TRUE(tableExists(db.handle(), "a"));
    TEST_ASSERT_FALSE(tableExists(db.handle(), "b"));
}

void test_unopenable_path_leaves_module_inert(void)
{
    MeshMapModule map(SqliteDb::pathIn(dbPath + ".missing-dir", "meshmap.db"));

    TEST_ASSERT_FALSE(map.isReady());
    TEST_ASSERT_FALSE(map.database().exec("SELECT 1;"));
    TEST_ASSERT_EQUAL_INT(-1, map.database().userVersion());
}

void test_database_is_in_wal_mode(void)
{
    MeshMapModule map(dbPath);
    TEST_ASSERT_TRUE(map.isReady());

    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(map.database().handle(), "PRAGMA journal_mode;", -1, &stmt, nullptr);
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));
    TEST_ASSERT_EQUAL_STRING("wal", reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
}

// DBPath in config.yaml is a directory and may be written with or without its trailing slash.
void test_pathIn_joins_with_or_without_trailing_slash(void)
{
    TEST_ASSERT_EQUAL_STRING("/var/lib/meshtasticd/meshmap.db", SqliteDb::pathIn("/var/lib/meshtasticd/", "meshmap.db").c_str());
    TEST_ASSERT_EQUAL_STRING("/var/lib/meshtasticd/meshmap.db", SqliteDb::pathIn("/var/lib/meshtasticd", "meshmap.db").c_str());
    TEST_ASSERT_EQUAL_STRING("meshmap.db", SqliteDb::pathIn("", "meshmap.db").c_str());
}

#else

void setUp(void) {}
void tearDown(void) {}

#endif

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
#if ARCH_PORTDUINO && HAS_SQLITE3
    RUN_TEST(test_fresh_database_reaches_current_schema);
    RUN_TEST(test_reopen_keeps_schema_and_rows);
    RUN_TEST(test_newer_schema_is_refused_and_left_untouched);
    RUN_TEST(test_failed_migration_rolls_back_to_previous_version);
    RUN_TEST(test_unopenable_path_leaves_module_inert);
    RUN_TEST(test_database_is_in_wal_mode);
    RUN_TEST(test_pathIn_joins_with_or_without_trailing_slash);
#endif
    exit(UNITY_END());
}

void loop() {}
