#pragma once

#include "tinylsm_config.h"
#include "tinylsm_fs.h"
#include "tinylsm_types.h"
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// WAL Entry
// ============================================================================

struct WALEntry {
    CompositeKey key;
    uint32_t value_size;
    uint8_t is_tombstone;
    // Followed by value bytes

    WALEntry() : key(), value_size(0), is_tombstone(0) {}
    WALEntry(CompositeKey k, uint32_t vs, bool tomb) : key(k), value_size(vs), is_tombstone(tomb ? 1 : 0) {}
};

// ============================================================================
// Write-Ahead Log (Ring buffer for durable LSM)
// ============================================================================

class WAL
{
  private:
    FileHandle file;
    size_t capacity_bytes;
    size_t current_bytes;
    bool use_a; // A/B toggle
    const char *base_path;
    bool is_open;

    std::vector<uint8_t> buffer; // In-memory buffer for batch writes

  public:
    WAL(const char *base, size_t capacity_kb);
    ~WAL();

    // Open/close
    bool open();
    void close();

    // Append entry
    bool append(CompositeKey key, const uint8_t *value, size_t value_size, bool is_tombstone);

    // Sync to disk
    bool sync();

    // Clear (after successful flush to SortedTable)
    bool clear();

    // Replay WAL on startup (callback for each entry)
    typedef void (*replay_callback_t)(CompositeKey key, const uint8_t *value, size_t value_size, bool is_tombstone,
                                      void *user_data);
    bool replay(replay_callback_t callback, void *user_data);

  private:
    bool build_filepath(char *dest, size_t dest_size, bool use_a_side) const;
    bool write_header();
    bool flush_buffer();
};

} // namespace tinylsm
} // namespace meshtastic
