#include "SqliteDb.h"

#if ARCH_PORTDUINO && HAS_SQLITE3
#include <cstring>

std::string SqliteDb::pathIn(const std::string &dir, const char *fileName)
{
    if (dir.empty() || dir.back() == '/')
        return dir + fileName;
    return dir + "/" + fileName;
}

bool SqliteDb::open(const std::string &dbPath)
{
    close();
    path = dbPath;

    // FULLMUTEX: a module may hand writes to its own thread while the main loop still reads.
    int res = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (res != SQLITE_OK) {
        LOG_ERROR("SQLite: cannot open %s: %s", path.c_str(), db ? sqlite3_errmsg(db) : sqlite3_errstr(res));
        close();
        return false;
    }
    if (sqlite3_db_readonly(db, "main") == 1) {
        LOG_ERROR("SQLite: %s opened read-only", path.c_str());
        close();
        return false;
    }

    sqlite3_busy_timeout(db, 2000);

    // WAL keeps writes sequential (kinder to SD cards) and lets the sqlite3 CLI read a live database.
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA journal_mode=WAL;", -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *mode = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (!mode || strcmp(mode, "wal") != 0)
            LOG_WARN("SQLite: %s is not in WAL mode (%s)", path.c_str(), mode ? mode : "?");
    }
    sqlite3_finalize(stmt);

    if (!exec("PRAGMA synchronous=NORMAL;")) {
        close();
        return false;
    }

    LOG_INFO("SQLite: opened %s", path.c_str());
    return true;
}

void SqliteDb::close()
{
    if (db) {
        // _v2 defers the close until any statement a caller still holds is finalized.
        sqlite3_close_v2(db);
        db = nullptr;
    }
}

bool SqliteDb::exec(const char *sql)
{
    if (!db)
        return false;
    char *err = nullptr;
    int res = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (res != SQLITE_OK) {
        LOG_ERROR("SQLite: %s: %s", path.c_str(), err ? err : sqlite3_errmsg(db));
        sqlite3_free(err);
        return false;
    }
    return true;
}

int SqliteDb::userVersion()
{
    if (!db)
        return -1;
    sqlite3_stmt *stmt = nullptr;
    int version = -1;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return version;
}

bool SqliteDb::migrate(const char *const *migrations, int count)
{
    int version = userVersion();
    if (version < 0)
        return false;
    if (version > count) {
        LOG_ERROR("SQLite: %s has schema v%d, newer than this build's v%d", path.c_str(), version, count);
        return false;
    }

    for (; version < count; version++) {
        // PRAGMA takes no bound parameters, so the version goes in as text.
        std::string bump = "PRAGMA user_version=" + std::to_string(version + 1) + ";";
        if (!exec("BEGIN;"))
            return false;
        if (!exec(migrations[version]) || !exec(bump.c_str()) || !exec("COMMIT;")) {
            exec("ROLLBACK;");
            LOG_ERROR("SQLite: %s migration to v%d failed", path.c_str(), version + 1);
            return false;
        }
        LOG_INFO("SQLite: %s migrated to schema v%d", path.c_str(), version + 1);
    }
    return true;
}

#endif
