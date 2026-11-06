#pragma once

#include "configuration.h"

#if ARCH_PORTDUINO && HAS_SQLITE3

#include <sqlite3.h>
#include <string>

/**
 * One owned SQLite handle for a native-only module: opens read-write in WAL mode and brings the
 * schema up to date through numbered migrations tracked in PRAGMA user_version.
 */
class SqliteDb
{
  public:
    SqliteDb() = default;
    ~SqliteDb() { close(); }
    SqliteDb(const SqliteDb &) = delete;
    SqliteDb &operator=(const SqliteDb &) = delete;

    /// Joins a configured directory and a file name, tolerating a missing trailing '/'.
    static std::string pathIn(const std::string &dir, const char *fileName);

    /// Opens (creating if needed) the database at path. False, with the handle closed, on any failure.
    bool open(const std::string &path);
    void close();
    bool isOpen() const { return db != nullptr; }
    sqlite3 *handle() const { return db; }

    /// Runs one or more statements with no result rows; logs and returns false on error.
    bool exec(const char *sql);

    /// Current PRAGMA user_version, or -1 on error.
    int userVersion();

    /**
     * Applies migrations[v] for each v from the stored user_version up to count - 1, each in its own
     * transaction that also bumps user_version. A database newer than count is refused, not touched.
     */
    bool migrate(const char *const *migrations, int count);

  private:
    sqlite3 *db = nullptr;
    std::string path;
};

#endif
