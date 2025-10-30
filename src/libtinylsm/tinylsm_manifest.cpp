#include "tinylsm_manifest.h"
#include "configuration.h"
#include "tinylsm_fs.h"
#include "tinylsm_utils.h"
#include <cstdio>
#include <cstring>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Manifest Implementation
// ============================================================================

Manifest::Manifest(const char *base, const char *prefix)
    : generation(0), next_sequence(1), use_a(true), base_path(base), name_prefix(prefix)
{
}

bool Manifest::load()
{
    // Try to load from A, then B
    bool loaded_a = false;
    bool loaded_b = false;
    uint64_t gen_a = 0;
    uint64_t gen_b = 0;

    void *data_a = nullptr;
    void *data_b = nullptr;
    size_t size_a = 0;
    size_t size_b = 0;

    char path_a[constants::MAX_PATH];
    char path_b[constants::MAX_PATH];

    if (!build_filepath(path_a, sizeof(path_a), true) || !build_filepath(path_b, sizeof(path_b), false)) {
        LOG_ERROR("Failed to build manifest paths");
        return false;
    }

    // Try A
    if (FileSystem::exists(path_a)) {
        FileHandle fh;
        if (fh.open(path_a, "rb")) {
            long sz = fh.size();
            if (sz > 0) {
                data_a = malloc(sz);
                if (data_a && fh.read(data_a, sz) == (size_t)sz) {
                    size_a = sz;
                    // Quick peek at generation
                    if (size_a >= sizeof(uint64_t)) {
                        memcpy(&gen_a, data_a, sizeof(gen_a));
                        loaded_a = true;
                    }
                }
            }
            fh.close();
        }
    }

    // Try B
    if (FileSystem::exists(path_b)) {
        FileHandle fh;
        if (fh.open(path_b, "rb")) {
            long sz = fh.size();
            if (sz > 0) {
                data_b = malloc(sz);
                if (data_b && fh.read(data_b, sz) == (size_t)sz) {
                    size_b = sz;
                    // Quick peek at generation
                    if (size_b >= sizeof(uint64_t)) {
                        memcpy(&gen_b, data_b, sizeof(gen_b));
                        loaded_b = true;
                    }
                }
            }
            fh.close();
        }
    }

    // Choose the one with higher generation
    bool use_data_a = false;
    if (loaded_a && loaded_b) {
        use_data_a = (gen_a >= gen_b);
        LOG_INFO("MANIFEST: Both A (gen=%llu) and B (gen=%llu) found, using %s", gen_a, gen_b, use_data_a ? "A" : "B");
    } else if (loaded_a) {
        use_data_a = true;
        LOG_INFO("MANIFEST: Only A found (gen=%llu)", gen_a);
    } else if (loaded_b) {
        use_data_a = false;
        LOG_INFO("MANIFEST: Only B found (gen=%llu)", gen_b);
    } else {
        // Neither exists, start fresh
        LOG_INFO("MANIFEST: No existing manifest found, starting fresh");
        return true;
    }

    // Deserialize chosen manifest
    bool success = false;
    if (use_data_a) {
        success = deserialize(static_cast<const uint8_t *>(data_a), size_a);
        use_a = true;
    } else {
        success = deserialize(static_cast<const uint8_t *>(data_b), size_b);
        use_a = false;
    }

    if (data_a)
        free(data_a);
    if (data_b)
        free(data_b);

    if (!success) {
        LOG_ERROR("MANIFEST: Failed to deserialize");
        return false;
    }

    LOG_INFO("MANIFEST: Loaded successfully - generation=%llu, %zu tables tracked", generation, entries.size());
    return true;
}

bool Manifest::save()
{
    std::vector<uint8_t> data;
    if (!serialize(data)) {
        LOG_ERROR("MANIFEST: Failed to serialize");
        return false;
    }

    // Toggle A/B
    bool old_use_a = use_a;
    use_a = !use_a;
    generation++;

    char filepath[constants::MAX_PATH];
    if (!build_filepath(filepath, sizeof(filepath), use_a)) {
        LOG_ERROR("MANIFEST: Failed to build path");
        return false;
    }

    LOG_DEBUG("MANIFEST: Saving generation=%lu to %s (A/B switch: %s -> %s, %u tables, %u bytes)", (unsigned long)generation,
              filepath, old_use_a ? "A" : "B", use_a ? "A" : "B", (unsigned int)entries.size(), (unsigned int)data.size());

    if (!FileSystem::atomic_write(filepath, data.data(), data.size())) {
        LOG_ERROR("MANIFEST: Atomic write failed to %s", filepath);
        return false;
    }

    LOG_INFO("MANIFEST: Saved successfully - gen=%lu, %u tables", (unsigned long)generation, (unsigned int)entries.size());
    return true;
}

bool Manifest::add_table(const SortedTableMeta &meta)
{
    // Check if already exists
    for (const auto &entry : entries) {
        if (entry.table_meta.file_id == meta.file_id) {
            LOG_WARN("Table file_id=%llu already in manifest", meta.file_id);
            return false;
        }
    }

    ManifestEntry entry(meta, next_sequence++);
    entries.push_back(entry);

    LOG_DEBUG("Added table to manifest: file_id=%lu, level=%u, entries=%u, filename=%s", (unsigned long)meta.file_id,
              (unsigned int)meta.level, (unsigned int)meta.num_entries, meta.filename);
    return true;
}

bool Manifest::remove_table(uint64_t file_id)
{
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->table_meta.file_id == file_id) {
            LOG_DEBUG("Removed table from manifest: file_id=%llu", file_id);
            entries.erase(it);
            return true;
        }
    }

    LOG_WARN("Table file_id=%llu not found in manifest", file_id);
    return false;
}

std::vector<ManifestEntry> Manifest::get_tables_at_level(uint8_t level) const
{
    std::vector<ManifestEntry> result;
    for (const auto &entry : entries) {
        if (entry.table_meta.level == level) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<ManifestEntry> Manifest::get_tables_in_range(const KeyRange &range) const
{
    std::vector<ManifestEntry> result;
    for (const auto &entry : entries) {
        if (entry.table_meta.key_range.overlaps(range)) {
            result.push_back(entry);
        }
    }
    return result;
}

void Manifest::clear()
{
    entries.clear();
    generation = 0;
    next_sequence = 1;
}

bool Manifest::serialize(std::vector<uint8_t> &output) const
{
    // Format (simple binary):
    // - magic (4B)
    // - version (2B)
    // - generation (8B)
    // - next_sequence (8B)
    // - num_entries (4B)
    // - entries array

    size_t header_size = 4 + 2 + 8 + 8 + 4;
    size_t entry_size = sizeof(SortedTableMeta) + sizeof(uint64_t);
    output.resize(header_size + entries.size() * entry_size + 4); // +4 for CRC

    uint8_t *ptr = output.data();

    // Magic
    uint32_t magic = constants::MANIFEST_MAGIC;
    memcpy(ptr, &magic, 4);
    ptr += 4;

    // Version
    uint16_t version = constants::MANIFEST_VERSION;
    memcpy(ptr, &version, 2);
    ptr += 2;

    // Generation
    memcpy(ptr, &generation, 8);
    ptr += 8;

    // Next sequence
    memcpy(ptr, &next_sequence, 8);
    ptr += 8;

    // Num entries
    uint32_t num_entries = entries.size();
    memcpy(ptr, &num_entries, 4);
    ptr += 4;

    // Entries
    for (const auto &entry : entries) {
        memcpy(ptr, &entry.table_meta, sizeof(SortedTableMeta));
        ptr += sizeof(SortedTableMeta);
        memcpy(ptr, &entry.sequence, sizeof(uint64_t));
        ptr += sizeof(uint64_t);
    }

    // CRC
    uint32_t crc = CRC32::compute(output.data(), ptr - output.data());
    memcpy(ptr, &crc, 4);
    ptr += 4;

    output.resize(ptr - output.data());
    return true;
}

bool Manifest::deserialize(const uint8_t *data, size_t size)
{
    if (size < 4 + 2 + 8 + 8 + 4 + 4) {
        LOG_ERROR("Manifest too small");
        return false;
    }

    const uint8_t *ptr = data;

    // Magic
    uint32_t magic;
    memcpy(&magic, ptr, 4);
    ptr += 4;
    if (magic != constants::MANIFEST_MAGIC) {
        LOG_ERROR("Invalid manifest magic: 0x%08X", magic);
        return false;
    }

    // Version
    uint16_t version;
    memcpy(&version, ptr, 2);
    ptr += 2;
    if (version != constants::MANIFEST_VERSION) {
        LOG_ERROR("Unsupported manifest version: %u", version);
        return false;
    }

    // Generation
    memcpy(&generation, ptr, 8);
    ptr += 8;

    // Next sequence
    memcpy(&next_sequence, ptr, 8);
    ptr += 8;

    // Num entries
    uint32_t num_entries;
    memcpy(&num_entries, ptr, 4);
    ptr += 4;

    // Entries
    entries.clear();
    entries.reserve(num_entries);

    size_t entry_size = sizeof(SortedTableMeta) + sizeof(uint64_t);
    for (uint32_t i = 0; i < num_entries; i++) {
        if (ptr + entry_size > data + size - 4) {
            LOG_ERROR("Manifest corrupted: entry %u extends beyond data", i);
            return false;
        }

        ManifestEntry entry;
        memcpy(&entry.table_meta, ptr, sizeof(SortedTableMeta));
        ptr += sizeof(SortedTableMeta);
        memcpy(&entry.sequence, ptr, sizeof(uint64_t));
        ptr += sizeof(uint64_t);

        entries.push_back(entry);
    }

    // Verify CRC
    uint32_t stored_crc;
    memcpy(&stored_crc, ptr, 4);
    uint32_t computed_crc = CRC32::compute(data, ptr - data);
    if (stored_crc != computed_crc) {
        LOG_ERROR("Manifest CRC mismatch");
        return false;
    }

    return true;
}

bool Manifest::build_filepath(char *dest, size_t dest_size, bool use_a_side) const
{
    int written = snprintf(dest, dest_size, "%s/%s-%c.bin", base_path, name_prefix, use_a_side ? 'A' : 'B');
    return written > 0 && (size_t)written < dest_size;
}

} // namespace tinylsm
} // namespace meshtastic
