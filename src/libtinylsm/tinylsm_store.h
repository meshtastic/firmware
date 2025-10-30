#pragma once

#include "tinylsm_compact.h"
#include "tinylsm_config.h"
#include "tinylsm_manifest.h"
#include "tinylsm_memtable.h"
#include "tinylsm_table.h"
#include "tinylsm_types.h"
#include "tinylsm_wal.h"
#include <memory>
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// LSM Family (one instance per durable/ephemeral)
// ============================================================================

class LSMFamily
{
  private:
    StoreConfig config;
    const char *base_path;
    bool is_ephemeral;

    std::unique_ptr<Memtable> memtable;
    std::unique_ptr<Manifest> manifest;
    std::unique_ptr<Compactor> compactor;
    std::unique_ptr<WAL> wal; // Durable only

    // Shards (if enabled)
    std::vector<std::unique_ptr<Memtable>> shard_memtables;
    std::vector<std::unique_ptr<Manifest>> shard_manifests;

    bool initialized;

  public:
    LSMFamily(const StoreConfig &cfg, const char *base, bool ephemeral);
    ~LSMFamily();

    // Lifecycle
    bool init();
    void shutdown();

    // GET/PUT
    GetResult<ValueBlob> get(CompositeKey key);
    bool put(CompositeKey key, const uint8_t *value, size_t value_size, bool sync_immediately = false);
    bool del(CompositeKey key);

    // Flush memtable to SortedTable
    bool flush(uint8_t shard = 0);

    // Trigger compaction
    bool compact();

    // Statistics
    void update_stats(StoreStats &stats) const;

    // Cooperative tick for background work (nRF52)
    void tick();

  private:
    bool flush_memtable(Memtable *mt, Manifest *mf, uint8_t shard);
    bool replay_wal();
    uint8_t select_shard(CompositeKey key) const;
};

// ============================================================================
// NodeDBStore (main public API)
// ============================================================================

class NodeDBStore
{
  private:
    StoreConfig config;
    std::unique_ptr<LSMFamily> durable_lsm;
    std::unique_ptr<LSMFamily> ephemeral_lsm;

    bool initialized;
    bool low_battery_mode;

  public:
    NodeDBStore();
    ~NodeDBStore();

    // Lifecycle
    bool init(const StoreConfig &cfg);
    void shutdown();

    // Durable operations
    GetResult<DurableRecord> getDurable(uint32_t node_id);
    bool putDurable(const DurableRecord &dr, bool sync_immediately = false);

    // Ephemeral operations
    GetResult<EphemeralRecord> getEphemeral(uint32_t node_id);
    bool putEphemeral(const EphemeralRecord &er);

    // Maintenance
    void tick(); // Cooperative background work
    void requestCheckpointEphemeral();
    void requestCompact();
    void setLowBattery(bool on);

    // Statistics
    StoreStats stats() const;

  private:
    bool encode_durable(const DurableRecord &dr, std::vector<uint8_t> &output);
    bool decode_durable(const uint8_t *data, size_t size, DurableRecord &dr);
    bool encode_ephemeral(const EphemeralRecord &er, std::vector<uint8_t> &output);
    bool decode_ephemeral(const uint8_t *data, size_t size, EphemeralRecord &er);
};

} // namespace tinylsm
} // namespace meshtastic
