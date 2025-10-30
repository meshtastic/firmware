#pragma once

#include "tinylsm_config.h"
#include "tinylsm_manifest.h"
#include "tinylsm_table.h"
#include "tinylsm_types.h"
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Compaction Strategy
// ============================================================================

enum class CompactionStrategy {
    SIZE_TIERED, // Merge similar-sized tables
    LEVELED      // Strict level-based (not implemented yet)
};

// ============================================================================
// Compaction Task
// ============================================================================

struct CompactionTask {
    std::vector<uint64_t> input_file_ids; // Tables to compact
    uint8_t output_level;                 // Level for output table
    uint8_t shard;                        // Shard ID
    bool is_ephemeral;                    // True if ephemeral LSM, false if durable

    CompactionTask() : output_level(0), shard(0), is_ephemeral(false) {}
};

// ============================================================================
// Compactor
// ============================================================================

class Compactor
{
  private:
    const StoreConfig *config;
    const char *base_path;

  public:
    Compactor(const StoreConfig *cfg, const char *base);

    // Select tables for compaction
    bool select_compaction(Manifest &manifest, CompactionTask &task);

    // Execute compaction task
    bool compact(const CompactionTask &task, Manifest &manifest, uint32_t ttl_sec);

  private:
    // Size-tiered selection
    bool select_size_tiered(Manifest &manifest, CompactionTask &task);

    // Merge iterator (merges multiple SortedTable iterators)
    class MergeIterator
    {
      private:
        struct StreamState {
            SortedTableReader::Iterator it;
            bool valid;

            StreamState(SortedTableReader::Iterator &&iter) : it(std::move(iter)), valid(iter.valid()) {}
        };

        std::vector<StreamState> streams;
        size_t current_stream;

      public:
        MergeIterator() : current_stream(0) {}

        void add_stream(SortedTableReader::Iterator &&iter);
        bool valid() const;
        void next();
        CompositeKey key() const;
        const uint8_t *value() const;
        size_t value_size() const;
        bool is_tombstone() const;

      private:
        void find_next_smallest();
    };
};

} // namespace tinylsm
} // namespace meshtastic
