#pragma once

#include "SqliteDb.h"

#if ARCH_PORTDUINO && HAS_SQLITE3

#include <stdint.h>
#include <string>

// Codes stored in meshmap.db columns. Append only: rows written by older builds keep their meaning.

/// rx_frame.our_action
enum class MeshMapAction : uint8_t { NONE = 0, RELAYED = 1, RELAY_CANCELLED = 2, FILTERED = 3 };

/// link.evidence - how a directed link was learned
enum class MeshMapEvidence : uint8_t { DIRECT_RX = 0, NEIGHBOR_INFO = 1, TRACEROUTE = 2, FLOOD_INFERENCE = 3 };

/// node.mobility
enum class MeshMapMobility : uint8_t { UNKNOWN = 0, STATIONARY = 1, MOBILE = 2 };

/**
 * Native-only passive map of the mesh: who hears whom, how well, and which nodes move, kept in its
 * own SQLite database rather than NodeDB. Node numbers are stored as unsigned 32-bit values in
 * INTEGER columns - bind them with sqlite3_bind_int64, never sqlite3_bind_int.
 */
class MeshMapModule
{
  public:
    static constexpr const char *DB_FILE_NAME = "meshmap.db";

    /// Opens dbPath and migrates it; on failure the module stays inert and isReady() is false.
    explicit MeshMapModule(const std::string &dbPath);

    bool isReady() const { return db.isOpen(); }
    SqliteDb &database() { return db; }

    static int schemaVersion();

  private:
    SqliteDb db;
};

extern MeshMapModule *meshMapModule;

#endif
