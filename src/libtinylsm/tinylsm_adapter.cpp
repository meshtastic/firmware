#include "tinylsm_adapter.h"
#include "configuration.h"
#include "memGet.h"
#include "tinylsm_dump.h"
#include <cstring>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Global Instance
// ============================================================================

NodeDBAdapter *g_nodedb_adapter = nullptr;

// ============================================================================
// NodeDBAdapter Implementation
// ============================================================================

NodeDBAdapter::NodeDBAdapter() : initialized(false) {}

NodeDBAdapter::~NodeDBAdapter()
{
    if (store) {
        store->shutdown();
    }
}

bool NodeDBAdapter::init()
{
    if (initialized) {
        return true;
    }

    uint32_t start_time = millis();
    LOG_INFO("NodeDB LSM Storage Initializing...");

    StoreConfig config = detectPlatformConfig();

    LOG_INFO("Platform config: memtable durable=%u KB, ephemeral=%u KB, shards=%u", config.memtable_durable_kb,
             config.memtable_ephemeral_kb, config.shards);

    store.reset(new NodeDBStore());
    if (!store->init(config)) {
        LOG_ERROR("NodeDB LSM initialization FAILED");
        return false;
    }

    initialized = true;
    uint32_t elapsed = millis() - start_time;
    LOG_INFO("NodeDB LSM adapter initialized in %u ms", elapsed);
    LOG_INFO("  Ready for node storage operations");

    // Log initial stats
    logStats();

    return true;
}

bool NodeDBAdapter::saveNode(const meshtastic_NodeInfoLite *node)
{
    if (!initialized || !node) {
        return false;
    }

    DurableRecord dr;
    EphemeralRecord er;

    if (!nodeInfoToRecords(node, dr, er)) {
        return false;
    }

    LOG_TRACE("NodeDB-LSM: Saving node 0x%08X (%s) - last_heard=%u, hop_limit=%u, channel=%u", node->num, node->user.long_name,
              er.last_heard_epoch, er.hop_limit, er.channel);

    // Save durable first (with sync if critical)
    if (!store->putDurable(dr, false)) {
        LOG_ERROR("NodeDB-LSM: Failed to save DURABLE for node 0x%08X", node->num);
        return false;
    }

    // Save ephemeral
    if (!store->putEphemeral(er)) {
        LOG_ERROR("NodeDB-LSM: Failed to save EPHEMERAL for node 0x%08X", node->num);
        return false;
    }

    return true;
}

bool NodeDBAdapter::loadNode(uint32_t node_id, meshtastic_NodeInfoLite *node)
{
    if (!initialized || !node) {
        return false;
    }

    LOG_TRACE("NodeDB-LSM: Loading node 0x%08X", node_id);

    // Load durable
    auto dr_result = store->getDurable(node_id);
    if (!dr_result.found) {
        LOG_DEBUG("NodeDB-LSM: Node 0x%08X NOT FOUND in durable LSM", node_id);
        return false;
    }

    // Load ephemeral (optional)
    auto er_result = store->getEphemeral(node_id);

    // If ephemeral missing, use defaults
    EphemeralRecord er;
    if (er_result.found) {
        er = er_result.value;
        LOG_TRACE("NodeDB-LSM: Loaded EPHEMERAL for node 0x%08X (last_heard=%u, hop_limit=%u)", node_id, er.last_heard_epoch,
                  er.hop_limit);
    } else {
        LOG_TRACE("NodeDB-LSM: No EPHEMERAL data for node 0x%08X, using defaults", node_id);
        er.node_id = node_id;
    }

    LOG_DEBUG("NodeDB-LSM: Loaded node 0x%08X (%s)", node_id, dr_result.value.long_name);
    return recordsToNodeInfo(dr_result.value, er, node);
}

bool NodeDBAdapter::deleteNode(uint32_t node_id)
{
    if (!initialized) {
        return false;
    }

    // Delete by inserting tombstones
    // This is simplified; in production, you'd need to delete all field tags for this node

    LOG_INFO("Deleting node %u", node_id);
    // Actual deletion would iterate through all field tags and insert tombstones
    // For now, this is a placeholder

    return true;
}

bool NodeDBAdapter::forEachNode(node_callback_t callback, void *user_data)
{
    if (!initialized || !callback) {
        return false;
    }

    // This requires iterating through all SortedTables and memtables
    // For a complete implementation, we'd need:
    // 1. Get all unique node IDs from durable LSM
    // 2. For each node ID, load and reconstruct NodeInfoLite
    // 3. Invoke callback

    // Placeholder: This would be implemented by scanning the durable LSM's
    // memtable and SortedTables, collecting unique node IDs, then loading each

    LOG_WARN("forEachNode not fully implemented");
    return false;
}

void NodeDBAdapter::tick()
{
    if (!initialized || !store) {
        return;
    }

#if defined(ARCH_NRF52)
    // On nRF52, check if we should dump LSM for USB/DFU
    static bool dump_checked = false;
    static uint32_t last_dump_check = 0;

    // Check every 30 seconds
    if (millis() - last_dump_check > 30000) {
        last_dump_check = millis();

        if (!dump_checked && LSMDumpManager::shouldDump()) {
            LOG_WARN("NRF52: USB connected, dumping LSM to free flash for DFU");
            LSMDumpManager::dumpForFirmwareUpdate();
            dump_checked = true;
        }
    }
#endif

    store->tick();
}

void NodeDBAdapter::flush()
{
    if (initialized && store) {
        store->requestCheckpointEphemeral();
    }
}

void NodeDBAdapter::compact()
{
    if (initialized && store) {
        store->requestCompact();
    }
}

void NodeDBAdapter::logStats()
{
    if (!initialized || !store) {
        return;
    }

    StoreStats s = store->stats();

    LOG_INFO("=== NodeDB LSM Storage Stats ===");
    LOG_INFO("DURABLE: memtable=%u entries, %u SortedTables, %u KB", s.durable_memtable_entries, s.durable_sstables,
             s.durable_total_bytes / 1024);
    LOG_INFO("EPHEMERAL: memtable=%u entries, %u SortedTables, %u KB", s.ephemeral_memtable_entries, s.ephemeral_sstables,
             s.ephemeral_total_bytes / 1024);

    if (s.cache_hits + s.cache_misses > 0) {
        float hit_rate = 100.0f * s.cache_hits / (s.cache_hits + s.cache_misses);
        LOG_INFO("CACHE: hits=%u misses=%u (%.1f%%)", s.cache_hits, s.cache_misses, hit_rate);
    }

    if (s.compactions_total > 0) {
        LOG_INFO("COMPACTION: %u total", s.compactions_total);
    }

    LOG_INFO("WEAR: %u SortedTables written, %u deleted", s.sstables_written, s.sstables_deleted);
    LOG_INFO("=================================");
}

bool NodeDBAdapter::nodeInfoToRecords(const meshtastic_NodeInfoLite *node, DurableRecord &dr, EphemeralRecord &er)
{
    if (!node) {
        return false;
    }

    // Durable record (identity & configuration - rarely changes)
    dr.node_id = node->num;
    strncpy(dr.long_name, node->user.long_name, sizeof(dr.long_name) - 1);
    strncpy(dr.short_name, node->user.short_name, sizeof(dr.short_name) - 1);
    memcpy(dr.public_key, node->user.public_key.bytes, std::min(sizeof(dr.public_key), sizeof(node->user.public_key.bytes)));
    dr.hw_model = node->user.hw_model;

    // Ephemeral record (hot path - routing & metrics)
    er.node_id = node->num;
    er.last_heard_epoch = node->last_heard;
    er.next_hop = node->via_mqtt ? 0 : static_cast<uint32_t>(node->next_hop); // Expand uint8_t to uint32_t, 0 if via MQTT
    er.rssi_avg = 0;                                                          // TODO: Track RSSI average separately
    er.snr = static_cast<int8_t>(node->snr);                                  // Convert float to int8_t (range -128..+127)
    er.role = node->user.role; // Role is ephemeral (can change frequently via admin packets)
    er.hop_limit = node->hops_away;
    er.channel = node->channel;
    er.battery_level = 0;   // TODO: Extract from device metrics if available
    er.route_cost = 0xFFFF; // TODO: Calculate from hop_limit and signal quality

    return true;
}

bool NodeDBAdapter::recordsToNodeInfo(const DurableRecord &dr, const EphemeralRecord &er, meshtastic_NodeInfoLite *node)
{
    if (!node) {
        return false;
    }

    memset(node, 0, sizeof(meshtastic_NodeInfoLite));

    // Node identity
    node->num = dr.node_id;

    // Ephemeral fields (hot path)
    node->last_heard = er.last_heard_epoch;
    node->next_hop = static_cast<uint8_t>(er.next_hop & 0xFF); // Extract last byte (protobuf stores only last byte)
    node->snr = static_cast<float>(er.snr);                    // Convert int8_t to float
    node->hops_away = er.hop_limit;
    node->channel = er.channel;
    node->via_mqtt = (er.next_hop == 0 && er.last_heard_epoch > 0); // Infer MQTT if next_hop is 0 but node was heard

    // User info (durable)
    strncpy(node->user.long_name, dr.long_name, sizeof(node->user.long_name) - 1);
    strncpy(node->user.short_name, dr.short_name, sizeof(node->user.short_name) - 1);
    memcpy(node->user.public_key.bytes, dr.public_key, std::min(sizeof(dr.public_key), sizeof(node->user.public_key.bytes)));
    node->user.hw_model = static_cast<meshtastic_HardwareModel>(dr.hw_model);
    node->user.role = static_cast<meshtastic_Config_DeviceConfig_Role>(er.role); // Role from ephemeral (can change)

    return true;
}

StoreConfig NodeDBAdapter::detectPlatformConfig()
{
    StoreConfig config;

#if defined(ARCH_ESP32)
// Detect PSRAM
#if defined(BOARD_HAS_PSRAM)
    // Use the memGet API for PSRAM detection (consistent with Meshtastic)
    size_t psram_size = memGet.getFreePsram() + memGet.getPsramSize();
    if (psram_size >= 2 * 1024 * 1024) {
        LOG_INFO("Detected PSRAM: %u bytes, using ESP32 PSRAM config", psram_size);
        config = StoreConfig::esp32_psram();
    } else {
        LOG_INFO("PSRAM too small or not available, using ESP32 no-PSRAM config");
        config = StoreConfig::esp32_no_psram();
    }
#else
    LOG_INFO("No PSRAM detected, using ESP32 no-PSRAM config");
    config = StoreConfig::esp32_no_psram();
#endif
#elif defined(ARCH_NRF52)
    LOG_INFO("Using nRF52 config");
    config = StoreConfig::nrf52();
#elif defined(ARCH_RP2040)
    LOG_INFO("Using RP2040 config (similar to nRF52)");
    config = StoreConfig::nrf52(); // RP2040 has similar constraints
#else
    LOG_INFO("Unknown platform, using conservative config");
    config = StoreConfig::nrf52();
#endif

    return config;
}

// ============================================================================
// Global Functions
// ============================================================================

bool initNodeDBLSM()
{
    if (g_nodedb_adapter) {
        return true;
    }

    g_nodedb_adapter = new NodeDBAdapter();
    if (!g_nodedb_adapter->init()) {
        delete g_nodedb_adapter;
        g_nodedb_adapter = nullptr;
        return false;
    }

    return true;
}

void shutdownNodeDBLSM()
{
    if (g_nodedb_adapter) {
        delete g_nodedb_adapter;
        g_nodedb_adapter = nullptr;
    }
}

} // namespace tinylsm
} // namespace meshtastic
