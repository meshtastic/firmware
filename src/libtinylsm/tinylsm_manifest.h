#pragma once

#include "tinylsm_config.h"
#include "tinylsm_table.h"
#include "tinylsm_types.h"
#include <cstdint>
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Manifest Entry (per SortedTable)
// ============================================================================

struct ManifestEntry {
    SortedTableMeta table_meta;
    uint64_t sequence; // Global sequence number for this entry

    ManifestEntry() : table_meta(), sequence(0) {}
    ManifestEntry(const SortedTableMeta &meta, uint64_t seq) : table_meta(meta), sequence(seq) {}
};

// ============================================================================
// Manifest (tracks all active SortedTables)
// ============================================================================

class Manifest
{
  private:
    std::vector<ManifestEntry> entries;
    uint64_t generation;    // Incremented on each write
    uint64_t next_sequence; // Next sequence number for new tables
    bool use_a;             // Current A/B toggle

    const char *base_path;
    const char *name_prefix; // "manifest-d" or "manifest-e"

  public:
    Manifest(const char *base, const char *prefix);

    // Load manifest from disk (try A, then B)
    bool load();

    // Save manifest to disk (atomically, using A/B)
    bool save();

    // Add table
    bool add_table(const SortedTableMeta &meta);

    // Remove table
    bool remove_table(uint64_t file_id);

    // Get all tables
    const std::vector<ManifestEntry> &get_entries() const { return entries; }

    // Get tables at specific level
    std::vector<ManifestEntry> get_tables_at_level(uint8_t level) const;

    // Get tables in key range
    std::vector<ManifestEntry> get_tables_in_range(const KeyRange &range) const;

    // Allocate new file ID
    uint64_t allocate_file_id() { return next_sequence++; }

    // Get generation
    uint64_t get_generation() const { return generation; }

    // Clear all entries (for testing/reset)
    void clear();

  private:
    bool serialize(std::vector<uint8_t> &output) const;
    bool deserialize(const uint8_t *data, size_t size);
    bool build_filepath(char *dest, size_t dest_size, bool use_a_side) const;
};

} // namespace tinylsm
} // namespace meshtastic
