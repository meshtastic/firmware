#include "tinylsm_wal.h"
#include "configuration.h"
#include "tinylsm_utils.h"
#include <cstring>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// WAL Implementation
// ============================================================================

WAL::WAL(const char *base, size_t capacity_kb)
    : capacity_bytes(capacity_kb * 1024), current_bytes(0), use_a(true), base_path(base), is_open(false)
{
    buffer.reserve(capacity_bytes / 4); // Reserve some buffer space
}

WAL::~WAL()
{
    close();
}

bool WAL::open()
{
    if (is_open) {
        return true;
    }

    char filepath[constants::MAX_PATH];
    if (!build_filepath(filepath, sizeof(filepath), use_a)) {
        LOG_ERROR("WAL: Failed to build path");
        return false;
    }

    // Check if file exists first
    bool exists = FileSystem::exists(filepath);

    // Open in write mode (will create or append)
    // Note: Some platforms don't support "ab" mode on LittleFS, use "w" for compatibility
    const char *mode = "w";
    if (!file.open(filepath, mode)) {
        LOG_WARN("WAL: Failed to open %s, trying alternate mode", filepath);

        // Try "wb" mode
        if (!file.open(filepath, "wb")) {
            LOG_ERROR("WAL: Failed to create/open %s", filepath);
            return false;
        }
    }

    // If new file, write header
    if (!exists || file.size() == 0) {
        if (!write_header()) {
            LOG_ERROR("WAL: Failed to write header");
            file.close();
            return false;
        }
    } else {
        // Existing file, seek to end for appending
        file.seek(0, SEEK_END);
    }

    is_open = true;
    LOG_DEBUG("WAL: Opened %s (size=%ld bytes)", filepath, file.size());
    return true;
}

void WAL::close()
{
    if (is_open) {
        flush_buffer();
        file.close();
        is_open = false;
    }
}

bool WAL::append(CompositeKey key, const uint8_t *value, size_t value_size, bool is_tombstone)
{
    if (!is_open) {
        LOG_ERROR("WAL not open");
        return false;
    }

    // Encode entry: key (8B) + value_size (4B) + is_tombstone (1B) + value + CRC32 (4B)
    size_t entry_size = 8 + 4 + 1 + value_size + 4;

    if (current_bytes + entry_size > capacity_bytes) {
        // Ring buffer full, need to checkpoint/flush
        LOG_WARN("WAL ring buffer full, forcing checkpoint");
        // In a full implementation, this would trigger a memtable flush
        // For now, just clear and wrap
        if (!clear()) {
            return false;
        }
    }

    // Encode to buffer
    uint8_t key_buf[8];
    encode_key(key, key_buf);

    uint32_t vs = value_size;
    uint8_t tomb = is_tombstone ? 1 : 0;

    buffer.insert(buffer.end(), key_buf, key_buf + 8);
    buffer.insert(buffer.end(), reinterpret_cast<uint8_t *>(&vs), reinterpret_cast<uint8_t *>(&vs) + 4);
    buffer.push_back(tomb);
    if (value_size > 0) {
        buffer.insert(buffer.end(), value, value + value_size);
    }

    // Compute CRC for this entry
    uint32_t crc = CRC32::compute(buffer.data() + buffer.size() - (entry_size - 4), entry_size - 4);
    buffer.insert(buffer.end(), reinterpret_cast<uint8_t *>(&crc), reinterpret_cast<uint8_t *>(&crc) + 4);

    current_bytes += entry_size;

    // Flush buffer if it gets large enough
    if (buffer.size() >= 4096) {
        return flush_buffer();
    }

    return true;
}

bool WAL::sync()
{
    if (!is_open) {
        return false;
    }

    if (!flush_buffer()) {
        return false;
    }

    return file.sync();
}

bool WAL::clear()
{
    if (!is_open) {
        return false;
    }

    // Close current file
    file.close();

    // Toggle A/B
    use_a = !use_a;

    // Delete old file and create new one
    char filepath[constants::MAX_PATH];
    if (!build_filepath(filepath, sizeof(filepath), use_a)) {
        LOG_ERROR("Failed to build WAL path");
        return false;
    }

    // Remove if exists
    FileSystem::remove(filepath);

    // Reopen
    is_open = false;
    current_bytes = 0;
    buffer.clear();

    return open();
}

bool WAL::replay(replay_callback_t callback, void *user_data)
{
    char filepath_a[constants::MAX_PATH];
    char filepath_b[constants::MAX_PATH];

    if (!build_filepath(filepath_a, sizeof(filepath_a), true) || !build_filepath(filepath_b, sizeof(filepath_b), false)) {
        LOG_ERROR("WAL: Failed to build WAL paths");
        return false;
    }

    // Try both A and B
    const char *paths[] = {filepath_a, filepath_b};
    bool replayed = false;
    uint32_t total_entries = 0;

    for (int i = 0; i < 2; i++) {
        if (!FileSystem::exists(paths[i])) {
            continue;
        }

        LOG_INFO("WAL: Replaying %s...", paths[i]);

        FileHandle fh;
        if (!fh.open(paths[i], "rb")) {
            LOG_WARN("WAL: Failed to open %s", paths[i]);
            continue;
        }

        // Check file size for sanity (prevent boot loop on corrupted WAL)
        long file_size = fh.size();
        if (file_size < 0 || file_size > 1024 * 1024) { // Max 1 MB WAL
            LOG_ERROR("WAL: Suspicious file size %ld bytes for %s - deleting to prevent boot loop", file_size, paths[i]);
            fh.close();
            FileSystem::remove(paths[i]);
            continue;
        }

        LOG_DEBUG("WAL: File %s size=%ld bytes, reading header...", paths[i], file_size);

        // Read header
        uint32_t magic;
        uint16_t version;
        if (fh.read(&magic, sizeof(magic)) != sizeof(magic) || fh.read(&version, sizeof(version)) != sizeof(version)) {
            LOG_WARN("WAL: Failed to read header from %s", paths[i]);
            fh.close();
            continue;
        }

        if (magic != constants::WAL_MAGIC || version != constants::WAL_VERSION) {
            LOG_WARN("WAL: Invalid header in %s (magic=0x%08X expected 0x%08X, version=%u expected %u) - deleting", paths[i],
                     magic, constants::WAL_MAGIC, version, constants::WAL_VERSION);
            fh.close();
            FileSystem::remove(paths[i]); // Delete invalid WAL
            continue;
        }

        LOG_DEBUG("WAL: Header valid, replaying entries...");

        // Read entries
        uint32_t entries_in_file = 0;
        const uint32_t MAX_ENTRIES_PER_WAL = 2000; // Safety limit
        const uint32_t MAX_VALUE_SIZE = 4096;      // Safety limit (4 KB)

        LOG_DEBUG("WAL: Starting entry loop, file_size=%ld, position=%ld", file_size, fh.tell());

        while (entries_in_file < MAX_ENTRIES_PER_WAL) {
            long entry_start_offset = fh.tell();

            // Check if we're near EOF (need at least 13 bytes: 8 key + 4 size + 1 tombstone)
            if (entry_start_offset + 13 > file_size) {
                LOG_DEBUG("WAL: Reached end of file at offset %ld", entry_start_offset);
                break;
            }

            uint8_t key_buf[8];
            uint32_t value_size;
            uint8_t is_tombstone;

            // Read key
            size_t key_read = fh.read(key_buf, 8);
            if (key_read != 8) {
                if (key_read == 0 && entry_start_offset >= file_size - 8) {
                    LOG_DEBUG("WAL: Clean EOF at offset %ld", entry_start_offset);
                } else {
                    LOG_WARN("WAL: Incomplete key read (%u/8 bytes) at offset %ld", key_read, entry_start_offset);
                }
                break; // EOF or error
            }

            // Read value_size and tombstone flag
            if (fh.read(&value_size, 4) != 4 || fh.read(&is_tombstone, 1) != 1) {
                LOG_WARN("WAL: Incomplete entry header at offset %ld, stopping replay", entry_start_offset);
                break;
            }

            LOG_TRACE("WAL: Entry %u at offset %ld: key=0x%02X%02X..., value_size=%u, tombstone=%u", entries_in_file,
                      entry_start_offset, key_buf[0], key_buf[1], value_size, is_tombstone);

            // CRITICAL: Sanity check BEFORE allocating vector
            if (value_size > MAX_VALUE_SIZE) {
                LOG_ERROR("WAL: CORRUPTION DETECTED! value_size=%u exceeds max=%u at offset %ld", value_size, MAX_VALUE_SIZE,
                          entry_start_offset);
                LOG_ERROR("WAL: This would crash device - DELETING %s to break boot loop", paths[i]);
                fh.close();
                FileSystem::remove(paths[i]);
                return false; // Abort - don't continue with corrupted data
            }

            // Safe to allocate now
            std::vector<uint8_t> value;
            if (value_size > 0) {
                LOG_TRACE("WAL: Allocating %u bytes for value...", value_size);
                value.resize(value_size);

                size_t bytes_read = fh.read(value.data(), value_size);
                if (bytes_read != value_size) {
                    LOG_WARN("WAL: Failed to read value (%u bytes expected, got %u) at offset %ld", value_size, bytes_read,
                             entry_start_offset);
                    break;
                }
                LOG_TRACE("WAL: Value read successfully");
            }

            // Read CRC
            uint32_t stored_crc;
            if (fh.read(&stored_crc, 4) != 4) {
                LOG_WARN("Failed to read CRC, stopping replay");
                break;
            }

            // Verify CRC (build entry data for verification)
            std::vector<uint8_t> entry_data;
            entry_data.reserve(8 + 4 + 1 + value_size); // Pre-allocate
            entry_data.insert(entry_data.end(), key_buf, key_buf + 8);
            entry_data.insert(entry_data.end(), reinterpret_cast<uint8_t *>(&value_size),
                              reinterpret_cast<uint8_t *>(&value_size) + 4);
            entry_data.push_back(is_tombstone);
            if (value_size > 0) {
                entry_data.insert(entry_data.end(), value.begin(), value.end());
            }

            uint32_t computed_crc = CRC32::compute(entry_data.data(), entry_data.size());
            if (stored_crc != computed_crc) {
                LOG_WARN("WAL: Entry CRC mismatch (stored=0x%08X, computed=0x%08X), stopping replay at entry %u", stored_crc,
                         computed_crc, entries_in_file);
                break;
            }

            // Replay entry
            CompositeKey key = decode_key(key_buf);
            callback(key, value.data(), value_size, is_tombstone != 0, user_data);
            replayed = true;
            entries_in_file++;
            total_entries++;
        }

        fh.close();

        if (entries_in_file > 0) {
            LOG_INFO("WAL: Replayed %u entries from %s", entries_in_file, paths[i]);
        } else {
            LOG_DEBUG("WAL: No valid entries in %s", paths[i]);
        }
    }

    if (replayed && total_entries > 0) {
        LOG_INFO("WAL: Replay completed - %u total entries restored", total_entries);
    } else {
        LOG_DEBUG("WAL: No entries to replay");
    }

    return replayed;
}

bool WAL::build_filepath(char *dest, size_t dest_size, bool use_a_side) const
{
    int written = snprintf(dest, dest_size, "%s/wal-%c.bin", base_path, use_a_side ? 'A' : 'B');
    return written > 0 && (size_t)written < dest_size;
}

bool WAL::write_header()
{
    uint32_t magic = constants::WAL_MAGIC;
    uint16_t version = constants::WAL_VERSION;

    if (file.write(&magic, sizeof(magic)) != sizeof(magic) || file.write(&version, sizeof(version)) != sizeof(version)) {
        LOG_ERROR("Failed to write WAL header");
        return false;
    }

    return true;
}

bool WAL::flush_buffer()
{
    if (buffer.empty()) {
        return true;
    }

    if (file.write(buffer.data(), buffer.size()) != buffer.size()) {
        LOG_ERROR("Failed to flush WAL buffer");
        return false;
    }

    buffer.clear();
    return true;
}

} // namespace tinylsm
} // namespace meshtastic
