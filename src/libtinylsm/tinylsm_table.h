#pragma once

#include "tinylsm_config.h"
#include "tinylsm_fs.h"
#include "tinylsm_memtable.h"
#include "tinylsm_types.h"
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// SortedTable Metadata
// ============================================================================

struct SortedTableMeta {
    uint64_t file_id; // Unique ID (sequence number)
    uint8_t level;
    uint8_t shard;
    KeyRange key_range;
    size_t file_size;
    size_t num_entries;
    char filename[constants::MAX_FILENAME];

    SortedTableMeta() : file_id(0), level(0), shard(0), key_range(), file_size(0), num_entries(0) { filename[0] = '\0'; }
};

// ============================================================================
// SortedTable Footer (stored at end of file)
// ============================================================================

struct SortedTableFooter {
    uint32_t magic;   // SSTABLE_MAGIC
    uint16_t version; // SSTABLE_VERSION
    uint16_t flags;   // Reserved for future use

    uint64_t index_offset; // Offset to fence index
    uint32_t index_size;   // Size of fence index

    uint64_t filter_offset; // Offset to filter (0 if no filter)
    uint32_t filter_size;   // Size of filter

    uint32_t num_entries; // Total number of entries
    uint32_t num_blocks;  // Total number of data blocks

    uint64_t min_key; // Minimum key (for quick range checks)
    uint64_t max_key; // Maximum key

    uint32_t footer_crc; // CRC32 of footer (excluding this field)
    uint32_t table_crc;  // CRC32 of entire table (excluding footer)

    SortedTableFooter()
        : magic(constants::SSTABLE_MAGIC), version(constants::SSTABLE_VERSION), flags(0), index_offset(0), index_size(0),
          filter_offset(0), filter_size(0), num_entries(0), num_blocks(0), min_key(0), max_key(0), footer_crc(0), table_crc(0)
    {
    }
};

// ============================================================================
// Block Header (at start of each data block)
// ============================================================================

struct BlockHeader {
    uint32_t uncompressed_size; // Size of data (not including header/crc)
    uint32_t compressed_size;   // Same as uncompressed (no compression for now)
    uint32_t num_entries;       // Number of entries in this block
    uint32_t flags;             // Reserved

    BlockHeader() : uncompressed_size(0), compressed_size(0), num_entries(0), flags(0) {}
};

// ============================================================================
// Fence Index Entry (points to each block)
// ============================================================================

struct FenceEntry {
    uint64_t first_key;    // First key in block
    uint64_t block_offset; // Offset to block in file

    FenceEntry() : first_key(0), block_offset(0) {}
    FenceEntry(uint64_t k, uint64_t o) : first_key(k), block_offset(o) {}
};

// ============================================================================
// SortedTable Writer
// ============================================================================

class SortedTableWriter
{
  private:
    FileHandle file;
    SortedTableMeta meta;
    size_t block_size;
    bool enable_filter;

    // Current block being built
    std::vector<uint8_t> block_buffer;
    uint32_t block_entries;

    // Fence index
    std::vector<FenceEntry> fence_index;

    // Filter data (if enabled)
    std::vector<uint8_t> filter_data;

    // Track all keys for bloom filter
    std::vector<CompositeKey> keys_written;

    // Statistics
    CompositeKey min_key_seen;
    CompositeKey max_key_seen;
    uint32_t total_entries;
    uint32_t total_blocks;

    bool finalized;

    // Base path for rename in finalize()
    char base_path[constants::MAX_PATH];

  public:
    SortedTableWriter(const SortedTableMeta &meta, size_t block_size, bool enable_filter);
    ~SortedTableWriter();

    // Open file for writing
    bool open(const char *base_path);

    // Add entry (must be called in sorted key order)
    bool add(CompositeKey key, const uint8_t *value, size_t value_size, bool is_tombstone);

    // Finish writing and close file
    bool finalize();

    // Get metadata
    const SortedTableMeta &get_meta() const { return meta; }

  private:
    bool flush_block();
    bool write_index();
    bool write_filter();
    bool write_footer(const SortedTableFooter &footer);
};

// ============================================================================
// SortedTable Reader
// ============================================================================

class SortedTableReader
{
  private:
    SortedTableMeta meta;
    SortedTableFooter footer;
    std::vector<FenceEntry> fence_index;
    std::vector<uint8_t> filter_data;
    bool filter_loaded;

    FileHandle file;
    bool is_open;

  public:
    SortedTableReader();
    ~SortedTableReader();

    // Open and read metadata
    bool open(const char *filepath);
    void close();

    // Lookup key
    bool get(CompositeKey key, uint8_t **value, size_t *value_size, bool *is_tombstone);

    // Check if key might exist (using filter)
    bool maybe_contains(CompositeKey key);

    // Metadata access
    const SortedTableMeta &get_meta() const { return meta; }
    const SortedTableFooter &get_footer() const { return footer; }
    const KeyRange &get_key_range() const { return meta.key_range; }

    // Iterator support (for compaction)
    struct Iterator {
        SortedTableReader *reader;
        size_t block_index;
        size_t entry_index_in_block;
        std::vector<uint8_t> block_data;
        CompositeKey current_key;
        ValueBlob current_value;
        bool current_is_tombstone;
        bool valid_flag;

        Iterator(SortedTableReader *r);

        bool valid() const { return valid_flag; }
        void next();
        CompositeKey key() const { return current_key; }
        const uint8_t *value() const { return current_value.ptr(); }
        size_t value_size() const { return current_value.size(); }
        bool is_tombstone() const { return current_is_tombstone; }

      private:
        bool load_block(size_t block_idx);
        bool parse_next_entry();
    };

    Iterator begin();

  private:
    bool read_footer();
    bool read_index();
    bool read_filter();
    bool read_block(size_t block_offset, std::vector<uint8_t> &buffer);
    bool search_block(const std::vector<uint8_t> &block_data, CompositeKey key, uint8_t **value, size_t *value_size,
                      bool *is_tombstone);
};

// ============================================================================
// Helper: Flush memtable to SortedTable
// ============================================================================

bool flush_memtable_to_sstable(const Memtable &memtable, SortedTableMeta &meta, const char *base_path, size_t block_size,
                               bool enable_filter);

} // namespace tinylsm
} // namespace meshtastic
