#include "tinylsm_compact.h"
#include "configuration.h"
#include "tinylsm_fs.h"
#include "tinylsm_utils.h"
#include <algorithm>
#include <map>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Compactor Implementation
// ============================================================================

Compactor::Compactor(const StoreConfig *cfg, const char *base) : config(cfg), base_path(base) {}

bool Compactor::select_compaction(Manifest &manifest, CompactionTask &task)
{
    // For now, only size-tiered
    return select_size_tiered(manifest, task);
}

bool Compactor::select_size_tiered(Manifest &manifest, CompactionTask &task)
{
    // Group tables by level and find candidates for merging
    std::map<uint8_t, std::vector<ManifestEntry>> level_map;

    for (const auto &entry : manifest.get_entries()) {
        level_map[entry.table_meta.level].push_back(entry);
    }

    // Check each level for compaction opportunities
    for (auto &pair : level_map) {
        uint8_t level = pair.first;
        std::vector<ManifestEntry> &tables = pair.second;

        if (tables.size() < config->size_tier_K) {
            continue; // Not enough tables to compact
        }

        // Sort by size
        std::sort(tables.begin(), tables.end(),
                  [](const ManifestEntry &a, const ManifestEntry &b) { return a.table_meta.file_size < b.table_meta.file_size; });

        // Find K similar-sized tables
        for (size_t i = 0; i + config->size_tier_K <= tables.size(); i++) {
            size_t min_size = tables[i].table_meta.file_size;
            size_t max_size = tables[i + config->size_tier_K - 1].table_meta.file_size;

            // Check if sizes are similar (within 2x)
            if (max_size <= min_size * 2) {
                // Found candidates
                task.input_file_ids.clear();
                for (size_t j = i; j < i + config->size_tier_K; j++) {
                    task.input_file_ids.push_back(tables[j].table_meta.file_id);
                }
                task.output_level = level + 1;
                task.shard = tables[i].table_meta.shard;

                LOG_INFO("Selected compaction: level=%u, %u tables", level, task.input_file_ids.size());
                return true;
            }
        }
    }

    return false; // No compaction needed
}

bool Compactor::compact(const CompactionTask &task, Manifest &manifest, uint32_t ttl_sec)
{
    if (task.input_file_ids.empty()) {
        return false;
    }

    uint32_t start_time = millis();
    LOG_INFO("COMPACTION START: %s LSM, %u input tables -> level %u, shard=%u", task.is_ephemeral ? "EPHEMERAL" : "DURABLE",
             task.input_file_ids.size(), task.output_level, task.shard);

    // Open all input tables (using pointers since SortedTableReader isn't copyable)
    std::vector<SortedTableReader *> readers;
    readers.reserve(task.input_file_ids.size());

    for (uint64_t file_id : task.input_file_ids) {
        // Find table in manifest
        const auto &entries = manifest.get_entries();
        auto it = std::find_if(entries.begin(), entries.end(),
                               [file_id](const ManifestEntry &e) { return e.table_meta.file_id == file_id; });

        if (it == entries.end()) {
            LOG_ERROR("Input table file_id=%llu not found in manifest", file_id);
            // Clean up already opened readers
            for (auto *r : readers)
                delete r;
            return false;
        }

        // Build filepath
        char filepath[constants::MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", base_path, it->table_meta.filename);

        SortedTableReader *reader = new SortedTableReader();
        if (!reader->open(filepath)) {
            LOG_ERROR("Failed to open input table: %s", filepath);
            delete reader;
            // Clean up already opened readers
            for (auto *r : readers)
                delete r;
            return false;
        }

        readers.push_back(reader);
    }

    // Create merge iterator
    MergeIterator merge_it;
    for (auto *reader : readers) {
        merge_it.add_stream(reader->begin());
    }

    // Create output SortedTable
    SortedTableMeta output_meta;
    output_meta.file_id = manifest.allocate_file_id();
    output_meta.level = task.output_level;
    output_meta.shard = task.shard;

    SortedTableWriter writer(output_meta, config->block_size_bytes, config->enable_bloom);
    if (!writer.open(base_path)) {
        LOG_ERROR("Failed to open output SortedTable");
        // Clean up readers before returning
        for (auto *r : readers)
            delete r;
        return false;
    }

    // Merge entries
    CompositeKey last_key(0);
    size_t entries_written = 0;
    size_t entries_dropped_ttl = 0;
    size_t entries_dropped_tombstone = 0;

    while (merge_it.valid()) {
        CompositeKey key = merge_it.key();

        // Skip duplicates (keep newest)
        if (entries_written > 0 && key == last_key) {
            merge_it.next();
            continue;
        }

        // Check TTL for ephemeral data
        if (task.is_ephemeral && ttl_sec > 0) {
            // Extract timestamp from ephemeral record (assuming last_heard_epoch field)
            // For simplicity, skip TTL check for now - would need to parse value
            // In production, extract timestamp and check:
            // if (is_expired(timestamp, ttl_sec)) { skip }
        }

        // Skip tombstones during compaction (they've done their job)
        if (merge_it.is_tombstone()) {
            entries_dropped_tombstone++;
            merge_it.next();
            continue;
        }

        // Write entry
        if (!writer.add(key, merge_it.value(), merge_it.value_size(), false)) {
            LOG_ERROR("Failed to add entry to output SortedTable");
            // Clean up readers before returning
            for (auto *r : readers)
                delete r;
            return false;
        }

        last_key = key;
        entries_written++;
        merge_it.next();
    }

    // Finalize output
    if (!writer.finalize()) {
        LOG_ERROR("COMPACTION: Failed to finalize output SortedTable");
        // Clean up readers before returning
        for (auto *r : readers)
            delete r;
        return false;
    }

    uint32_t elapsed = millis() - start_time;
    LOG_INFO("COMPACTION: Merged %u entries, dropped %u tombstones + %u expired (TTL) in %u ms", entries_written,
             entries_dropped_tombstone, entries_dropped_ttl, elapsed);

    // Update manifest: add output, remove inputs
    manifest.add_table(writer.get_meta());
    for (uint64_t file_id : task.input_file_ids) {
        manifest.remove_table(file_id);
    }

    // Delete input files and clean up readers
    for (uint64_t file_id : task.input_file_ids) {
        char filepath[constants::MAX_PATH];
        // Find filename from readers
        for (auto *reader : readers) {
            if (reader->get_meta().file_id == file_id) {
                snprintf(filepath, sizeof(filepath), "%s/%s", base_path, reader->get_meta().filename);
                FileSystem::remove(filepath);
                LOG_DEBUG("Deleted input table: %s", filepath);
                break;
            }
        }
    }

    // Clean up reader pointers
    for (auto *reader : readers) {
        delete reader;
    }

    LOG_INFO("COMPACTION COMPLETE: Output SortedTable %s (%u bytes) at level %u", writer.get_meta().filename,
             writer.get_meta().file_size, task.output_level);
    return true;
}

// ============================================================================
// MergeIterator Implementation
// ============================================================================

void Compactor::MergeIterator::add_stream(SortedTableReader::Iterator &&iter)
{
    if (iter.valid()) {
        streams.emplace_back(std::move(iter));
    }
}

bool Compactor::MergeIterator::valid() const
{
    for (const auto &stream : streams) {
        if (stream.valid) {
            return true;
        }
    }
    return false;
}

void Compactor::MergeIterator::next()
{
    if (!valid()) {
        return;
    }

    // Advance current stream
    if (current_stream < streams.size() && streams[current_stream].valid) {
        streams[current_stream].it.next();
        streams[current_stream].valid = streams[current_stream].it.valid();
    }

    // Find next smallest key
    find_next_smallest();
}

CompositeKey Compactor::MergeIterator::key() const
{
    if (current_stream < streams.size() && streams[current_stream].valid) {
        return streams[current_stream].it.key();
    }
    return CompositeKey(0);
}

const uint8_t *Compactor::MergeIterator::value() const
{
    if (current_stream < streams.size() && streams[current_stream].valid) {
        return streams[current_stream].it.value();
    }
    return nullptr;
}

size_t Compactor::MergeIterator::value_size() const
{
    if (current_stream < streams.size() && streams[current_stream].valid) {
        return streams[current_stream].it.value_size();
    }
    return 0;
}

bool Compactor::MergeIterator::is_tombstone() const
{
    if (current_stream < streams.size() && streams[current_stream].valid) {
        return streams[current_stream].it.is_tombstone();
    }
    return false;
}

void Compactor::MergeIterator::find_next_smallest()
{
    // Find stream with smallest key
    current_stream = 0;
    CompositeKey min_key(UINT64_MAX);
    bool found = false;

    for (size_t i = 0; i < streams.size(); i++) {
        if (streams[i].valid && streams[i].it.key() < min_key) {
            min_key = streams[i].it.key();
            current_stream = i;
            found = true;
        }
    }

    if (!found) {
        current_stream = streams.size(); // Invalidate
    }
}

} // namespace tinylsm
} // namespace meshtastic
