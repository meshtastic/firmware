#include "tinylsm_table.h"
#include "FSCommon.h"
#include "configuration.h"
#include "tinylsm_filter.h"
#include "tinylsm_fs.h"
#include "tinylsm_types.h"
#include "tinylsm_utils.h"
#include <cstring>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// SortedTableWriter Implementation
// ============================================================================

SortedTableWriter::SortedTableWriter(const SortedTableMeta &m, size_t blk_size, bool en_filter)
    : meta(m), block_size(blk_size), enable_filter(en_filter), block_entries(0), min_key_seen(UINT64_MAX), max_key_seen(0),
      total_entries(0), total_blocks(0), finalized(false)
{
    block_buffer.reserve(block_size + 1024); // Some headroom
    memset(base_path, 0, sizeof(base_path));
}

SortedTableWriter::~SortedTableWriter()
{
    if (!finalized && file.isOpen()) {
        LOG_WARN("SortedTableWriter destroyed without finalize()");
        file.close();
    }
}

bool SortedTableWriter::open(const char *path)
{
    // Store base path for later use in finalize()
    strncpy(base_path, path, sizeof(base_path) - 1);
    base_path[sizeof(base_path) - 1] = '\0';

    // Ensure directory exists
    if (!FileSystem::exists(base_path)) {
        LOG_WARN("SortedTable: Base path %s doesn't exist, creating it", base_path);
        if (!FileSystem::mkdir(base_path)) {
            LOG_ERROR("SortedTable: Failed to create directory %s", base_path);
            return false;
        }
    }

    // Build filename based on level and file_id
    // Use 'e' for ephemeral path, 'd' for durable path (detect from base_path)
    char prefix = (strstr(base_path, "nodedb_e") != nullptr) ? 'e' : 'd';

    char filepath[constants::MAX_PATH];
    // Fix: Use proper format for uint64_t on embedded platforms
    snprintf(filepath, sizeof(filepath), "%s/%c-L%u-%lu.sst", base_path, prefix, meta.level, (unsigned long)meta.file_id);

    strncpy(meta.filename, PathUtil::filename(filepath), sizeof(meta.filename) - 1);

    // Open temp file
    char temp_filepath[constants::MAX_PATH];
    snprintf(temp_filepath, sizeof(temp_filepath), "%s.tmp", filepath);

    LOG_DEBUG("SortedTable: Opening temp file %s", temp_filepath);

    // Use "wb" mode - FileHandle will convert to Arduino File API mode
    if (!file.open(temp_filepath, "wb")) {
        LOG_ERROR("SortedTable: Failed to open temp file: %s (check filesystem is mounted)", temp_filepath);
        return false;
    }

    LOG_DEBUG("SortedTable: Temp file opened successfully");
    return true;
}

bool SortedTableWriter::add(CompositeKey key, const uint8_t *value, size_t value_size, bool is_tombstone)
{
    if (finalized) {
        LOG_ERROR("Cannot add to finalized SortedTable");
        return false;
    }

    // Track min/max keys
    if (total_entries == 0) {
        min_key_seen = key;
    }
    max_key_seen = key;
    total_entries++;

    // Track key for bloom filter
    if (enable_filter) {
        keys_written.push_back(key);
    }

    // Encode entry: key (8B) + value_size (varint) + value + tombstone_flag (1B)
    uint8_t key_buf[8];
    encode_key(key, key_buf);

    uint8_t size_buf[5];
    size_t size_len = encode_varint32(value_size, size_buf);

    uint8_t tombstone_flag = is_tombstone ? 1 : 0;

    // Check if adding this entry would overflow current block
    size_t entry_size = 8 + size_len + value_size + 1;
    if (block_buffer.size() + entry_size > block_size && block_entries > 0) {
        // Flush current block
        if (!flush_block()) {
            return false;
        }
    }

    // Append to block buffer
    block_buffer.insert(block_buffer.end(), key_buf, key_buf + 8);
    block_buffer.insert(block_buffer.end(), size_buf, size_buf + size_len);
    if (value_size > 0) {
        block_buffer.insert(block_buffer.end(), value, value + value_size);
    }
    block_buffer.push_back(tombstone_flag);

    block_entries++;
    return true;
}

bool SortedTableWriter::flush_block()
{
    if (block_buffer.empty()) {
        return true;
    }

    // Record fence entry (first key in block)
    uint64_t block_offset = file.tell();
    CompositeKey first_key = decode_key(block_buffer.data());
    fence_index.push_back(FenceEntry(first_key.value, block_offset));

    // Build block header
    BlockHeader header;
    header.uncompressed_size = block_buffer.size();
    header.compressed_size = block_buffer.size(); // No compression
    header.num_entries = block_entries;
    header.flags = 0;

    // Write header
    if (file.write(&header, sizeof(header)) != sizeof(header)) {
        LOG_ERROR("Failed to write block header");
        return false;
    }

    // Write block data
    if (file.write(block_buffer.data(), block_buffer.size()) != block_buffer.size()) {
        LOG_ERROR("Failed to write block data");
        return false;
    }

    // Write block CRC
    uint32_t block_crc = CRC32::compute(block_buffer.data(), block_buffer.size());
    if (file.write(&block_crc, sizeof(block_crc)) != sizeof(block_crc)) {
        LOG_ERROR("Failed to write block CRC");
        return false;
    }

    // Clear buffer for next block
    block_buffer.clear();
    block_entries = 0;
    total_blocks++;

    return true;
}

bool SortedTableWriter::write_index()
{
    // Note: index_offset would be useful for random access,
    // but we currently seek from footer, so it's not needed

    // Write number of fence entries
    uint32_t num_entries = fence_index.size();
    if (file.write(&num_entries, sizeof(num_entries)) != sizeof(num_entries)) {
        LOG_ERROR("Failed to write index entry count");
        return false;
    }

    // Write fence entries
    for (const auto &entry : fence_index) {
        uint64_t key_be = htobe64_local(entry.first_key);
        uint64_t offset_be = htobe64_local(entry.block_offset);

        if (file.write(&key_be, sizeof(key_be)) != sizeof(key_be) ||
            file.write(&offset_be, sizeof(offset_be)) != sizeof(offset_be)) {
            LOG_ERROR("Failed to write fence entry");
            return false;
        }
    }

    return true;
}

bool SortedTableWriter::write_filter()
{
    if (!enable_filter) {
        return true;
    }

    // Build actual bloom filter from all keys written
    BloomFilter bloom(keys_written.size(), 8.0f); // 8 bits per key

    for (const auto &key : keys_written) {
        bloom.add(key);
    }

    // Serialize filter
    if (!bloom.serialize(filter_data)) {
        LOG_ERROR("Failed to serialize bloom filter");
        return false;
    }

    LOG_DEBUG("Bloom filter built: %u keys, %u bytes (%.1f bits/key, %u hash funcs)", keys_written.size(), filter_data.size(),
              (float)(filter_data.size() * 8) / keys_written.size(), constants::BLOOM_NUM_HASHES);

    // Write filter size
    uint32_t filter_size = filter_data.size();
    if (file.write(&filter_size, sizeof(filter_size)) != sizeof(filter_size)) {
        LOG_ERROR("Failed to write filter size");
        return false;
    }

    // Write filter data
    if (!filter_data.empty()) {
        if (file.write(filter_data.data(), filter_data.size()) != filter_data.size()) {
            LOG_ERROR("Failed to write filter data");
            return false;
        }
    }

    return true;
}

bool SortedTableWriter::write_footer(const SortedTableFooter &footer)
{
    // Write footer (except CRCs)
    SortedTableFooter footer_copy = footer;

    // Compute footer CRC (excluding footer_crc and table_crc fields)
    size_t footer_crc_offset = offsetof(SortedTableFooter, footer_crc);
    footer_copy.footer_crc = CRC32::compute(reinterpret_cast<const uint8_t *>(&footer_copy), footer_crc_offset);

    // Write footer
    if (file.write(&footer_copy, sizeof(footer_copy)) != sizeof(footer_copy)) {
        LOG_ERROR("Failed to write footer");
        return false;
    }

    return true;
}

bool SortedTableWriter::finalize()
{
    if (finalized) {
        return true;
    }

    // Flush remaining block
    if (!flush_block()) {
        return false;
    }

    // Write index
    uint64_t index_offset_val = file.tell();
    if (!write_index()) {
        return false;
    }
    uint64_t index_end = file.tell();
    uint32_t index_size = index_end - index_offset_val;

    // Write filter (if enabled)
    uint64_t filter_offset_val = 0;
    uint32_t filter_size = 0;
    if (enable_filter) {
        filter_offset_val = file.tell();
        if (!write_filter()) {
            return false;
        }
        uint64_t filter_end = file.tell();
        filter_size = filter_end - filter_offset_val;
    }

    // Build footer
    SortedTableFooter footer;
    footer.index_offset = index_offset_val;
    footer.index_size = index_size;
    footer.filter_offset = filter_offset_val;
    footer.filter_size = filter_size;
    footer.num_entries = total_entries;
    footer.num_blocks = total_blocks;
    footer.min_key = min_key_seen.value;
    footer.max_key = max_key_seen.value;

    // Write footer
    if (!write_footer(footer)) {
        return false;
    }

    // Sync and close temp file
    file.sync();
    long file_size = file.tell();
    file.close();

    // Rename temp to final (need full paths for LittleFS)
    char temp_filepath[constants::MAX_PATH];
    char final_filepath[constants::MAX_PATH];
    snprintf(temp_filepath, sizeof(temp_filepath), "%s/%s.tmp", base_path, meta.filename);
    snprintf(final_filepath, sizeof(final_filepath), "%s/%s", base_path, meta.filename);

    LOG_DEBUG("SortedTable: Renaming %s → %s", temp_filepath, final_filepath);

    if (!FileSystem::rename(temp_filepath, final_filepath)) {
        LOG_ERROR("Failed to rename SortedTable to final name (from='%s' to='%s')", temp_filepath, final_filepath);
        return false;
    }

    LOG_DEBUG("SortedTable: Rename successful, file=%s, size=%ld bytes", meta.filename, file_size);

    // Update metadata
    meta.file_size = file_size;
    meta.num_entries = total_entries;
    meta.key_range = KeyRange(min_key_seen, max_key_seen);

    finalized = true;
    LOG_DEBUG("Finalized SortedTable %s: %u entries, %u blocks, %ld bytes", meta.filename, total_entries, total_blocks,
              file_size);

    return true;
}

// ============================================================================
// SortedTableReader Implementation
// ============================================================================

SortedTableReader::SortedTableReader() : filter_loaded(false), is_open(false) {}

SortedTableReader::~SortedTableReader()
{
    close();
}

bool SortedTableReader::open(const char *filepath)
{
    if (!file.open(filepath, "rb")) {
        LOG_ERROR("Failed to open SortedTable: %s", filepath);
        return false;
    }

    strncpy(meta.filename, PathUtil::filename(filepath), sizeof(meta.filename) - 1);

    // Read footer
    if (!read_footer()) {
        file.close();
        return false;
    }

    // Read index
    if (!read_index()) {
        file.close();
        return false;
    }

    // Optionally read filter
    if (footer.filter_size > 0) {
        read_filter(); // Non-fatal if fails
    }

    meta.file_size = file.size();
    meta.num_entries = footer.num_entries;
    meta.key_range = KeyRange(CompositeKey(footer.min_key), CompositeKey(footer.max_key));

    is_open = true;
    return true;
}

void SortedTableReader::close()
{
    if (is_open) {
        file.close();
        is_open = false;
    }
}

bool SortedTableReader::read_footer()
{
    // Seek to end - sizeof(footer)
    long file_sz = file.size();
    if (file_sz < (long)sizeof(SortedTableFooter)) {
        LOG_ERROR("File too small to contain footer");
        return false;
    }

    if (!file.seek(file_sz - sizeof(SortedTableFooter), SEEK_SET)) {
        LOG_ERROR("Failed to seek to footer");
        return false;
    }

    if (file.read(&footer, sizeof(footer)) != sizeof(footer)) {
        LOG_ERROR("Failed to read footer");
        return false;
    }

    // Validate magic
    if (footer.magic != constants::SSTABLE_MAGIC) {
        LOG_ERROR("Invalid SortedTable magic: 0x%08X", footer.magic);
        return false;
    }

    // Validate version
    if (footer.version != constants::SSTABLE_VERSION) {
        LOG_ERROR("Unsupported SortedTable version: %u", footer.version);
        return false;
    }

    // TODO: Validate CRCs

    return true;
}

bool SortedTableReader::read_index()
{
    if (!file.seek(footer.index_offset, SEEK_SET)) {
        LOG_ERROR("Failed to seek to index");
        return false;
    }

    uint32_t num_entries;
    if (file.read(&num_entries, sizeof(num_entries)) != sizeof(num_entries)) {
        LOG_ERROR("Failed to read index entry count");
        return false;
    }

    fence_index.resize(num_entries);
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t key_be, offset_be;
        if (file.read(&key_be, sizeof(key_be)) != sizeof(key_be) ||
            file.read(&offset_be, sizeof(offset_be)) != sizeof(offset_be)) {
            LOG_ERROR("Failed to read fence entry %u", i);
            return false;
        }

        fence_index[i].first_key = be64toh_local(key_be);
        fence_index[i].block_offset = be64toh_local(offset_be);
    }

    return true;
}

bool SortedTableReader::read_filter()
{
    if (footer.filter_size == 0) {
        return true;
    }

    if (!file.seek(footer.filter_offset, SEEK_SET)) {
        LOG_WARN("Failed to seek to filter");
        return false;
    }

    uint32_t filter_size;
    if (file.read(&filter_size, sizeof(filter_size)) != sizeof(filter_size)) {
        LOG_WARN("Failed to read filter size");
        return false;
    }

    filter_data.resize(filter_size);
    if (file.read(filter_data.data(), filter_size) != filter_size) {
        LOG_WARN("Failed to read filter data");
        return false;
    }

    filter_loaded = true;
    return true;
}

bool SortedTableReader::maybe_contains(CompositeKey key)
{
    // Quick range check (fast, always do this first)
    if (key < meta.key_range.start || key > meta.key_range.end) {
        LOG_TRACE("Bloom: key 0x%08X:%u outside range of %s → SKIP", key.node_id(), key.field_tag(), meta.filename);
        return false; // Definitely not in this table
    }

    // Check bloom filter if available
    if (filter_loaded && !filter_data.empty()) {
        BloomFilter bloom;
        if (bloom.deserialize(filter_data.data(), filter_data.size())) {
            bool maybe = bloom.maybe_contains(key);
            if (!maybe) {
                // Bloom filter says DEFINITELY NOT HERE
                LOG_TRACE("Bloom: key 0x%08X:%u NEGATIVE for %s → SKIP flash read (filter saved I/O!)", key.node_id(),
                          key.field_tag(), meta.filename);
                return false;
            } else {
                // Bloom filter says MAYBE HERE (could be false positive)
                LOG_TRACE("Bloom: key 0x%08X:%u maybe in %s → will read flash", key.node_id(), key.field_tag(), meta.filename);
            }
        }
    }

    return true; // Maybe present, need to actually read the table
}

bool SortedTableReader::get(CompositeKey key, uint8_t **value, size_t *value_size, bool *is_tombstone)
{
    if (!is_open) {
        return false;
    }

    // Check if key might exist
    if (!maybe_contains(key)) {
        return false;
    }

    // Binary search fence index to find block
    size_t left = 0;
    size_t right = fence_index.size();
    size_t block_idx = 0;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (CompositeKey(fence_index[mid].first_key) <= key) {
            block_idx = mid;
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    // Read block
    std::vector<uint8_t> block_data;
    if (!read_block(fence_index[block_idx].block_offset, block_data)) {
        return false;
    }

    // Search block
    return search_block(block_data, key, value, value_size, is_tombstone);
}

bool SortedTableReader::read_block(size_t block_offset, std::vector<uint8_t> &buffer)
{
    if (!file.seek(block_offset, SEEK_SET)) {
        LOG_ERROR("Failed to seek to block");
        return false;
    }

    // Read block header
    BlockHeader header;
    if (file.read(&header, sizeof(header)) != sizeof(header)) {
        LOG_ERROR("Failed to read block header");
        return false;
    }

    // Read block data
    buffer.resize(header.uncompressed_size);
    if (file.read(buffer.data(), header.uncompressed_size) != header.uncompressed_size) {
        LOG_ERROR("Failed to read block data");
        return false;
    }

    // Read and verify CRC
    uint32_t stored_crc;
    if (file.read(&stored_crc, sizeof(stored_crc)) != sizeof(stored_crc)) {
        LOG_ERROR("Failed to read block CRC");
        return false;
    }

    uint32_t computed_crc = CRC32::compute(buffer.data(), buffer.size());
    if (stored_crc != computed_crc) {
        LOG_ERROR("Block CRC mismatch");
        return false;
    }

    return true;
}

bool SortedTableReader::search_block(const std::vector<uint8_t> &block_data, CompositeKey key, uint8_t **value,
                                     size_t *value_size, bool *is_tombstone)
{
    const uint8_t *ptr = block_data.data();
    const uint8_t *end = ptr + block_data.size();

    while (ptr < end) {
        // Parse entry: key (8B) + value_size (varint) + value + tombstone_flag (1B)
        if (ptr + 8 > end) {
            break;
        }

        CompositeKey entry_key = decode_key(ptr);
        ptr += 8;

        uint32_t entry_value_size;
        size_t varint_len = decode_varint32(ptr, end - ptr, &entry_value_size);
        if (varint_len == 0) {
            LOG_ERROR("Failed to decode value size varint");
            return false;
        }
        ptr += varint_len;

        if (ptr + entry_value_size + 1 > end) {
            LOG_ERROR("Corrupted block: value extends beyond block boundary");
            return false;
        }

        const uint8_t *entry_value = ptr;
        ptr += entry_value_size;

        uint8_t entry_tombstone = *ptr;
        ptr++;

        // Check if this is the key we're looking for
        if (entry_key == key) {
            *value = const_cast<uint8_t *>(entry_value);
            *value_size = entry_value_size;
            *is_tombstone = (entry_tombstone != 0);
            return true;
        }

        // If we've passed the key, it's not in this block
        if (entry_key > key) {
            return false;
        }
    }

    return false;
}

SortedTableReader::Iterator SortedTableReader::begin()
{
    return Iterator(this);
}

SortedTableReader::Iterator::Iterator(SortedTableReader *r)
    : reader(r), block_index(0), entry_index_in_block(0), current_is_tombstone(false), valid_flag(false)
{
    if (reader->fence_index.empty()) {
        return;
    }

    // Load first block
    if (load_block(0)) {
        parse_next_entry();
    }
}

bool SortedTableReader::Iterator::load_block(size_t block_idx)
{
    if (block_idx >= reader->fence_index.size()) {
        valid_flag = false;
        return false;
    }

    block_index = block_idx;
    entry_index_in_block = 0;

    if (!reader->read_block(reader->fence_index[block_idx].block_offset, block_data)) {
        valid_flag = false;
        return false;
    }

    return true;
}

bool SortedTableReader::Iterator::parse_next_entry()
{
    if (block_data.empty()) {
        valid_flag = false;
        return false;
    }

    // Calculate offset in block
    size_t offset = 0;
    for (size_t i = 0; i < entry_index_in_block; i++) {
        // Skip entries to get to current position
        // This is inefficient but simple; could be optimized with an entry offset cache
        if (offset >= block_data.size()) {
            valid_flag = false;
            return false;
        }

        // Parse and skip entry
        offset += 8; // Key
        uint32_t val_size;
        size_t varint_len = decode_varint32(block_data.data() + offset, block_data.size() - offset, &val_size);
        if (varint_len == 0) {
            valid_flag = false;
            return false;
        }
        offset += varint_len + val_size + 1; // varint + value + tombstone
    }

    // Parse current entry
    if (offset + 8 > block_data.size()) {
        valid_flag = false;
        return false;
    }

    current_key = decode_key(block_data.data() + offset);
    offset += 8;

    uint32_t val_size;
    size_t varint_len = decode_varint32(block_data.data() + offset, block_data.size() - offset, &val_size);
    if (varint_len == 0) {
        valid_flag = false;
        return false;
    }
    offset += varint_len;

    // Copy value
    current_value = ValueBlob(block_data.data() + offset, val_size, true);
    offset += val_size;

    current_is_tombstone = (block_data[offset] != 0);

    valid_flag = true;
    return true;
}

void SortedTableReader::Iterator::next()
{
    if (!valid_flag) {
        return;
    }

    entry_index_in_block++;

    // Try to parse next entry in current block
    if (!parse_next_entry()) {
        // Move to next block
        if (!load_block(block_index + 1)) {
            valid_flag = false;
            return;
        }
        parse_next_entry();
    }
}

// ============================================================================
// Helper: Flush memtable to SortedTable
// ============================================================================

bool flush_memtable_to_sstable(const Memtable &memtable, SortedTableMeta &meta, const char *base_path, size_t block_size,
                               bool enable_filter)
{
    SortedTableWriter writer(meta, block_size, enable_filter);

    if (!writer.open(base_path)) {
        return false;
    }

    // Iterate through memtable (already sorted)
    for (auto it = memtable.begin(); it.valid(); it.next()) {
        if (!writer.add(it.key(), it.value(), it.value_size(), it.is_tombstone())) {
            LOG_ERROR("Failed to add entry to SortedTable");
            return false;
        }
    }

    if (!writer.finalize()) {
        LOG_ERROR("Failed to finalize SortedTable");
        return false;
    }

    // Copy updated meta back (finalize() updates file_size, num_entries, key_range, etc.)
    meta = writer.get_meta();

    LOG_DEBUG("Flush complete: file=%s, entries=%u, size=%u bytes, range=[0x%08lX%08lX - 0x%08lX%08lX]", meta.filename,
              meta.num_entries, meta.file_size, (unsigned long)(meta.key_range.start.value >> 32),
              (unsigned long)(meta.key_range.start.value & 0xFFFFFFFF), (unsigned long)(meta.key_range.end.value >> 32),
              (unsigned long)(meta.key_range.end.value & 0xFFFFFFFF));

    return true;
}

} // namespace tinylsm
} // namespace meshtastic
