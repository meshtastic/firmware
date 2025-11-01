#include "tinylsm_store.h"
#include "configuration.h"
#include "tinylsm_fs.h"
#include "tinylsm_types.h"
#include "tinylsm_utils.h"
#include <cstring>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// LSMFamily Implementation
// ============================================================================

LSMFamily::LSMFamily(const StoreConfig &cfg, const char *base, bool ephemeral)
    : config(cfg), base_path(base), is_ephemeral(ephemeral), initialized(false)
{
}

LSMFamily::~LSMFamily()
{
    shutdown();
}

bool LSMFamily::init()
{
    if (initialized) {
        return true;
    }

    uint32_t start_time = millis();
    LOG_INFO("LSM INIT START: %s", is_ephemeral ? "EPHEMERAL" : "DURABLE");
    LOG_INFO("  Path: %s", base_path);
    LOG_INFO("  Memtable: %u KB", is_ephemeral ? config.memtable_ephemeral_kb : config.memtable_durable_kb);
    LOG_INFO("  Shards: %u, Bloom: %s", config.shards, config.enable_bloom ? "enabled" : "disabled");

    // Create directory
    if (!FileSystem::mkdir(base_path)) {
        LOG_ERROR("LSM INIT: Failed to create directory: %s", base_path);
        return false;
    }

    // Create manifest
    const char *manifest_prefix = is_ephemeral ? "manifest-e" : "manifest-d";
    manifest.reset(new Manifest(base_path, manifest_prefix));
    if (!manifest->load()) {
        LOG_ERROR("Failed to load manifest");
        return false;
    }

    // Create compactor
    compactor.reset(new Compactor(&config, base_path));

    // Create memtable(s)
    if (config.shards > 1 && !is_ephemeral) {
        // Sharded (durable only, ESP32)
        shard_memtables.reserve(config.shards);
        shard_manifests.reserve(config.shards);

        size_t memtable_kb = is_ephemeral ? config.memtable_ephemeral_kb : config.memtable_durable_kb;
        size_t per_shard_kb = memtable_kb / config.shards;

        for (uint8_t i = 0; i < config.shards; i++) {
            shard_memtables.emplace_back(new Memtable(per_shard_kb));

            // Each shard has its own sub-manifest (optional optimization, for now share main manifest)
            // shard_manifests.push_back(...);
        }
    } else {
        // Single memtable
        size_t memtable_kb = is_ephemeral ? config.memtable_ephemeral_kb : config.memtable_durable_kb;
        memtable.reset(new Memtable(memtable_kb));
    }

    // Create WAL (durable only, optional)
    if (!is_ephemeral && config.wal_ring_kb > 0) {
        wal.reset(new WAL(base_path, config.wal_ring_kb));
        if (!wal->open()) {
            LOG_WARN("Failed to open WAL, continuing without it (durable writes will be less safe)");
            wal.reset(); // Clear WAL, continue without it
        } else {
            // TEMPORARY: Skip WAL replay to break boot loop
            // TODO: Re-enable after debugging WAL corruption
            LOG_WARN("WAL replay DISABLED temporarily to prevent boot loop");
            LOG_WARN("Deleting WAL files for clean start...");

            char wal_a[constants::MAX_PATH];
            char wal_b[constants::MAX_PATH];
            snprintf(wal_a, sizeof(wal_a), "%s/wal-A.bin", base_path);
            snprintf(wal_b, sizeof(wal_b), "%s/wal-B.bin", base_path);

            if (FileSystem::exists(wal_a)) {
                FileSystem::remove(wal_a);
                LOG_INFO("Deleted %s", wal_a);
            }
            if (FileSystem::exists(wal_b)) {
                FileSystem::remove(wal_b);
                LOG_INFO("Deleted %s", wal_b);
            }

            // Disable WAL for this session
            wal.reset();
            LOG_INFO("Continuing without WAL (data loss possible on power failure)");
        }
    }

    initialized = true;
    uint32_t elapsed = millis() - start_time;
    LOG_INFO("LSM INIT COMPLETE: %s", is_ephemeral ? "EPHEMERAL" : "DURABLE");
    LOG_INFO("  %u SortedTables loaded", manifest->get_entries().size());
    LOG_INFO("  Initialized in %u ms", elapsed);
    return true;
}

void LSMFamily::shutdown()
{
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down %s LSM", is_ephemeral ? "ephemeral" : "durable");

    // Flush memtables
    if (config.shards > 1 && !shard_memtables.empty()) {
        for (size_t i = 0; i < shard_memtables.size(); i++) {
            if (shard_memtables[i] && !shard_memtables[i]->is_empty()) {
                flush_memtable(shard_memtables[i].get(), manifest.get(), i);
            }
        }
    } else if (memtable && !memtable->is_empty()) {
        flush_memtable(memtable.get(), manifest.get(), 0);
    }

    // Save manifest
    if (manifest) {
        manifest->save();
    }

    // Close WAL
    if (wal) {
        wal->sync();
        wal->close();
    }

    initialized = false;
}

GetResult<ValueBlob> LSMFamily::get(CompositeKey key)
{
    GetResult<ValueBlob> empty_result;
    empty_result.found = false;

    if (!initialized) {
        return empty_result;
    }

    uint32_t node_id = key.node_id();
    uint16_t field_tag = key.field_tag();

    // 1. Check memtable
    Memtable *mt = nullptr;
    if (config.shards > 1 && !shard_memtables.empty()) {
        uint8_t shard = select_shard(key);
        mt = shard_memtables[shard].get();
        LOG_TRACE("LSM GET node=0x%08X field=%s shard=%u: checking memtable", node_id, field_tag_name(field_tag), shard);
    } else {
        mt = memtable.get();
        LOG_TRACE("LSM GET node=0x%08X field=%s: checking memtable", node_id, field_tag_name(field_tag));
    }

    if (mt) {
        uint8_t *value_ptr;
        size_t value_size;
        bool is_tombstone;

        if (mt->get(key, &value_ptr, &value_size, &is_tombstone)) {
            if (is_tombstone) {
                LOG_DEBUG("LSM GET node=0x%08X field=%s: found tombstone in memtable", node_id, field_tag_name(field_tag));
                GetResult<ValueBlob> deleted_result;
                deleted_result.found = false;
                return deleted_result; // Deleted
            }
            LOG_DEBUG("LSM GET node=0x%08X field=%s: HIT in memtable (%u bytes)", node_id, field_tag_name(field_tag), value_size);
            ValueBlob blob(value_ptr, value_size, true);
            GetResult<ValueBlob> result;
            result.found = true;
            result.value = std::move(blob);
            return result;
        }
    }

    LOG_TRACE("LSM GET node=0x%08X field=%s: memtable MISS, checking %u SortedTables", node_id, field_tag_name(field_tag),
              manifest->get_entries().size());

    // 2. Check SortedTables (in order, newest first)
    auto entries = manifest->get_entries();

    // Filter by key range
    std::vector<ManifestEntry> candidates;
    for (const auto &entry : entries) {
        if (entry.table_meta.key_range.contains(key)) {
            candidates.push_back(entry);
        }
    }

    // Sort by sequence (newest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const ManifestEntry &a, const ManifestEntry &b) { return a.sequence > b.sequence; });

    // Search each candidate
    for (const auto &entry : candidates) {
        char filepath[constants::MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", base_path, entry.table_meta.filename);

        SortedTableReader reader;
        if (!reader.open(filepath)) {
            LOG_WARN("Failed to open SortedTable: %s", filepath);
            continue;
        }

        uint8_t *value_ptr;
        size_t value_size;
        bool is_tombstone;

        if (reader.get(key, &value_ptr, &value_size, &is_tombstone)) {
            if (is_tombstone) {
                LOG_DEBUG("LSM GET node=0x%08X field=%s: found tombstone in SortedTable %s", node_id, field_tag_name(field_tag),
                          entry.table_meta.filename);
                GetResult<ValueBlob> deleted_result;
                deleted_result.found = false;
                return deleted_result; // Deleted
            }
            LOG_DEBUG("LSM GET node=0x%08X field=%s: HIT in SortedTable %s (%u bytes)", node_id, field_tag_name(field_tag),
                      entry.table_meta.filename, value_size);
            ValueBlob blob(value_ptr, value_size, true);
            GetResult<ValueBlob> result;
            result.found = true;
            result.value = std::move(blob);
            return result;
        }
    }

    LOG_DEBUG("LSM GET node=0x%08X field=%s: NOT FOUND (checked memtable + %u SortedTables)", node_id, field_tag_name(field_tag),
              candidates.size());
    GetResult<ValueBlob> not_found;
    not_found.found = false;
    return not_found; // Not found
}

bool LSMFamily::put(CompositeKey key, const uint8_t *value, size_t value_size, bool sync_immediately)
{
    if (!initialized) {
        return false;
    }

    // Select memtable
    Memtable *mt = nullptr;
    Manifest *mf = nullptr;
    uint8_t shard = 0;

    if (config.shards > 1 && !shard_memtables.empty()) {
        shard = select_shard(key);
        mt = shard_memtables[shard].get();
        mf = manifest.get(); // For now, share manifest
    } else {
        mt = memtable.get();
        mf = manifest.get();
        shard = 0;
    }

    // Write to WAL first (durable only)
    if (wal && !is_ephemeral) {
        if (!wal->append(key, value, value_size, false)) {
            LOG_ERROR("Failed to append to WAL");
            return false;
        }

        if (sync_immediately) {
            if (!wal->sync()) {
                LOG_ERROR("Failed to sync WAL");
                return false;
            }
        }
    }

    // Insert into memtable
    if (!mt->put(key, value, value_size)) {
        LOG_ERROR("LSM PUT node=0x%08X field=%s FAILED: memtable insert error", key.node_id(), field_tag_name(key.field_tag()));
        return false;
    }

    LOG_TRACE("LSM PUT node=0x%08X field=%s: written to memtable (%u bytes, memtable now %u/%u KB)", key.node_id(),
              field_tag_name(key.field_tag()), value_size, mt->size_bytes() / 1024, mt->capacity() / 1024);

    // Check if memtable is full
    if (mt->is_full()) {
        LOG_INFO("LSM: Memtable FULL (shard=%u, %u entries, %u KB), triggering flush", shard, mt->size_entries(),
                 mt->size_bytes() / 1024);
        if (!flush_memtable(mt, mf, shard)) {
            LOG_ERROR("LSM PUT: Flush failed! Memtable is full, cannot accept more writes");
            return false;
        }
    }

    return true;
}

bool LSMFamily::del(CompositeKey key)
{
    if (!initialized) {
        return false;
    }

    // Select memtable
    Memtable *mt = nullptr;
    uint8_t shard = 0;

    if (config.shards > 1 && !shard_memtables.empty()) {
        shard = select_shard(key);
        mt = shard_memtables[shard].get();
    } else {
        mt = memtable.get();
    }

    // Write to WAL first (durable only)
    if (wal && !is_ephemeral) {
        if (!wal->append(key, nullptr, 0, true)) {
            LOG_ERROR("Failed to append tombstone to WAL");
            return false;
        }
    }

    // Insert tombstone into memtable
    return mt->del(key);
}

bool LSMFamily::flush(uint8_t shard_id)
{
    if (!initialized) {
        return false;
    }

    if (config.shards > 1 && !shard_memtables.empty()) {
        if (shard_id >= shard_memtables.size()) {
            return false;
        }
        return flush_memtable(shard_memtables[shard_id].get(), manifest.get(), shard_id);
    } else {
        return flush_memtable(memtable.get(), manifest.get(), 0);
    }
}

bool LSMFamily::compact()
{
    if (!initialized || !compactor) {
        return false;
    }

    CompactionTask task;
    task.is_ephemeral = is_ephemeral;

    if (!compactor->select_compaction(*manifest, task)) {
        // No compaction needed
        return true;
    }

    uint32_t ttl = is_ephemeral ? config.ttl_ephemeral_sec : 0;
    return compactor->compact(task, *manifest, ttl);
}

void LSMFamily::update_stats(StoreStats &stats) const
{
    if (!initialized) {
        return;
    }

    if (is_ephemeral) {
        if (memtable) {
            // memtable bytes not tracked in StoreStats
            stats.ephemeral_memtable_entries = memtable->size_entries();
        }
        stats.ephemeral_sstables = manifest->get_entries().size();
    } else {
        if (memtable) {
            // memtable bytes not tracked in StoreStats
            stats.durable_memtable_entries = memtable->size_entries();
        }
        stats.durable_sstables = manifest->get_entries().size();
    }
}

void LSMFamily::tick()
{
    if (!initialized) {
        return;
    }

    // Check if flush needed (ephemeral time-based flush)
    if (is_ephemeral && memtable && memtable->should_flush(config.flush_interval_sec_ephem)) {
        uint32_t now = get_epoch_time();
        uint32_t last_flush = memtable->get_last_flush_time();
        uint32_t time_since_flush = (now > last_flush) ? (now - last_flush) : 0;

        LOG_INFO("LSM TICK: Time-based flush triggered for EPHEMERAL (%u seconds since last flush, %u entries buffered)",
                 time_since_flush, memtable->size_entries());

        if (!flush()) {
            LOG_ERROR("LSM TICK: Flush failed! Will retry on next tick");
            // Don't keep trying immediately - wait for next tick
            memtable->set_last_flush_time(now); // Update to prevent immediate retry
        }
    }

    // Opportunistically trigger compaction
    CompactionTask task;
    task.is_ephemeral = is_ephemeral;
    if (compactor && compactor->select_compaction(*manifest, task)) {
        LOG_INFO("LSM TICK: Background compaction triggered for %s LSM (%u tables selected)",
                 is_ephemeral ? "EPHEMERAL" : "DURABLE", task.input_file_ids.size());
        compact();
    }
}

bool LSMFamily::flush_memtable(Memtable *mt, Manifest *mf, uint8_t shard)
{
    if (!mt || mt->is_empty()) {
        return true;
    }

    uint32_t start_time = millis();
    LOG_INFO("LSM FLUSH START: %s memtable (shard=%u, %u entries, %u KB)", is_ephemeral ? "EPHEMERAL" : "DURABLE", shard,
             mt->size_entries(), mt->size_bytes() / 1024);

    // Create SortedTable metadata
    SortedTableMeta meta;
    meta.file_id = mf->allocate_file_id();
    meta.level = 0; // New tables always go to L0
    meta.shard = shard;

    // Flush
    if (!flush_memtable_to_sstable(*mt, meta, base_path, config.block_size_bytes, config.enable_bloom)) {
        LOG_ERROR("Failed to flush memtable to SortedTable");
        return false;
    }

    // Add to manifest
    mf->add_table(meta);

    // Save manifest
    if (!mf->save()) {
        LOG_ERROR("Failed to save manifest after flush");
        return false;
    }

    // Clear WAL
    if (wal && !is_ephemeral) {
        wal->clear();
    }

    // Clear memtable
    mt->clear();
    mt->set_last_flush_time(get_epoch_time());

    uint32_t elapsed = millis() - start_time;
    LOG_INFO("LSM FLUSH COMPLETE: %s SortedTable created: %s (%u entries, %u bytes) in %u ms",
             is_ephemeral ? "EPHEMERAL" : "DURABLE", meta.filename, meta.num_entries, meta.file_size, elapsed);
    return true;
}

bool LSMFamily::replay_wal()
{
    if (!wal) {
        return true;
    }

    LOG_INFO("Replaying WAL...");

    auto callback = [](CompositeKey key, const uint8_t *value, size_t value_size, bool is_tombstone, void *user_data) {
        LSMFamily *self = static_cast<LSMFamily *>(user_data);
        Memtable *mt = self->memtable.get();

        if (is_tombstone) {
            mt->del(key);
        } else {
            mt->put(key, value, value_size);
        }
    };

    return wal->replay(callback, this);
}

uint8_t LSMFamily::select_shard(CompositeKey key) const
{
    return meshtastic::tinylsm::select_shard(key, config.shards);
}

// ============================================================================
// NodeDBStore Implementation
// ============================================================================

NodeDBStore::NodeDBStore() : initialized(false), low_battery_mode(false) {}

NodeDBStore::~NodeDBStore()
{
    shutdown();
}

bool NodeDBStore::init(const StoreConfig &cfg)
{
    if (initialized) {
        return true;
    }

    LOG_INFO("Initializing NodeDBStore");

    config = cfg;

    // Initialize filesystem
    if (!FileSystem::init(config.base_path)) {
        LOG_ERROR("Failed to initialize filesystem");
        return false;
    }

    // Create LSM families
    durable_lsm.reset(new LSMFamily(config, config.durable_path, false));
    if (!durable_lsm->init()) {
        LOG_ERROR("Failed to initialize durable LSM");
        return false;
    }

    ephemeral_lsm.reset(new LSMFamily(config, config.ephemeral_path, true));
    if (!ephemeral_lsm->init()) {
        LOG_ERROR("Failed to initialize ephemeral LSM");
        return false;
    }

    initialized = true;
    LOG_INFO("NodeDBStore initialized");
    return true;
}

void NodeDBStore::shutdown()
{
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down NodeDBStore");

    if (ephemeral_lsm) {
        ephemeral_lsm->shutdown();
    }

    if (durable_lsm) {
        durable_lsm->shutdown();
    }

    initialized = false;
}

GetResult<DurableRecord> NodeDBStore::getDurable(uint32_t node_id)
{
    if (!initialized) {
        return GetResult<DurableRecord>();
    }

    CompositeKey key(node_id, static_cast<FieldTag>(FieldTagEnum::WHOLE_DURABLE));
    auto result = durable_lsm->get(key);

    if (!result.found) {
        return GetResult<DurableRecord>();
    }

    DurableRecord dr;
    if (!decode_durable(result.value.ptr(), result.value.size(), dr)) {
        return GetResult<DurableRecord>();
    }

    return GetResult<DurableRecord>(true, dr);
}

bool NodeDBStore::putDurable(const DurableRecord &dr, bool sync_immediately)
{
    if (!initialized) {
        return false;
    }

    std::vector<uint8_t> encoded;
    if (!encode_durable(dr, encoded)) {
        return false;
    }

    CompositeKey key(dr.node_id, static_cast<FieldTag>(FieldTagEnum::WHOLE_DURABLE));
    return durable_lsm->put(key, encoded.data(), encoded.size(), sync_immediately);
}

GetResult<EphemeralRecord> NodeDBStore::getEphemeral(uint32_t node_id)
{
    if (!initialized) {
        return GetResult<EphemeralRecord>();
    }

    CompositeKey key(node_id, static_cast<FieldTag>(FieldTagEnum::LAST_HEARD));
    auto result = ephemeral_lsm->get(key);

    if (!result.found) {
        return GetResult<EphemeralRecord>();
    }

    EphemeralRecord er;
    if (!decode_ephemeral(result.value.ptr(), result.value.size(), er)) {
        return GetResult<EphemeralRecord>();
    }

    return GetResult<EphemeralRecord>(true, er);
}

bool NodeDBStore::putEphemeral(const EphemeralRecord &er)
{
    if (!initialized) {
        return false;
    }

    std::vector<uint8_t> encoded;
    if (!encode_ephemeral(er, encoded)) {
        return false;
    }

    CompositeKey key(er.node_id, static_cast<FieldTag>(FieldTagEnum::LAST_HEARD));
    return ephemeral_lsm->put(key, encoded.data(), encoded.size(), false);
}

void NodeDBStore::tick()
{
    if (!initialized) {
        return;
    }

    if (durable_lsm) {
        durable_lsm->tick();
    }

    if (ephemeral_lsm) {
        ephemeral_lsm->tick();
    }
}

void NodeDBStore::requestCheckpointEphemeral()
{
    if (!initialized || !ephemeral_lsm) {
        return;
    }

    LOG_INFO("Checkpoint requested for ephemeral LSM");
    ephemeral_lsm->flush();
}

void NodeDBStore::requestCompact()
{
    if (!initialized) {
        return;
    }

    LOG_INFO("Compaction requested");

    if (durable_lsm) {
        durable_lsm->compact();
    }

    if (ephemeral_lsm) {
        ephemeral_lsm->compact();
    }
}

void NodeDBStore::setLowBattery(bool on)
{
    low_battery_mode = on;

    if (on && config.enable_low_battery_flush) {
        LOG_WARN("Low battery mode enabled, flushing ephemeral data");
        requestCheckpointEphemeral();
    }
}

StoreStats NodeDBStore::stats() const
{
    StoreStats s;

    if (durable_lsm) {
        durable_lsm->update_stats(s);
    }

    if (ephemeral_lsm) {
        ephemeral_lsm->update_stats(s);
    }

    return s;
}

bool NodeDBStore::encode_durable(const meshtastic::tinylsm::DurableRecord &dr, std::vector<uint8_t> &output)
{
    // Simple binary encoding
    output.resize(sizeof(meshtastic::tinylsm::DurableRecord));
    memcpy(output.data(), &dr, sizeof(meshtastic::tinylsm::DurableRecord));
    return true;
}

bool NodeDBStore::decode_durable(const uint8_t *data, size_t size, meshtastic::tinylsm::DurableRecord &dr)
{
    if (size != sizeof(meshtastic::tinylsm::DurableRecord)) {
        return false;
    }
    memcpy(&dr, data, sizeof(meshtastic::tinylsm::DurableRecord));
    return true;
}

bool NodeDBStore::encode_ephemeral(const meshtastic::tinylsm::EphemeralRecord &er, std::vector<uint8_t> &output)
{
    // Simple binary encoding
    output.resize(sizeof(meshtastic::tinylsm::EphemeralRecord));
    memcpy(output.data(), &er, sizeof(meshtastic::tinylsm::EphemeralRecord));
    return true;
}

bool NodeDBStore::decode_ephemeral(const uint8_t *data, size_t size, meshtastic::tinylsm::EphemeralRecord &er)
{
    if (size != sizeof(meshtastic::tinylsm::EphemeralRecord)) {
        return false;
    }
    memcpy(&er, data, sizeof(meshtastic::tinylsm::EphemeralRecord));
    return true;
}

} // namespace tinylsm
} // namespace meshtastic
