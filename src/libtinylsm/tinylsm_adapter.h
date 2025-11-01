#pragma once

#include "mesh/NodeDB.h"
#include "tinylsm_store.h"
#include <memory>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// NodeDB Adapter (bridges tiny-LSM to Meshtastic's NodeDB API)
// ============================================================================

class NodeDBAdapter
{
  private:
    std::unique_ptr<NodeDBStore> store;
    bool initialized;

  public:
    NodeDBAdapter();
    ~NodeDBAdapter();

    // Initialize with platform-specific config
    bool init();

    // Convert NodeInfoLite to/from LSM records
    bool saveNode(const meshtastic_NodeInfoLite *node);
    bool loadNode(uint32_t node_id, meshtastic_NodeInfoLite *node);

    // Delete node
    bool deleteNode(uint32_t node_id);

    // Enumerate all nodes (callback-based)
    typedef void (*node_callback_t)(const meshtastic_NodeInfoLite *node, void *user_data);
    bool forEachNode(node_callback_t callback, void *user_data);

    // Maintenance
    void tick();
    void flush();
    void compact();

    // Statistics
    void logStats();

  private:
    bool nodeInfoToRecords(const meshtastic_NodeInfoLite *node, DurableRecord &dr, EphemeralRecord &er);
    bool recordsToNodeInfo(const DurableRecord &dr, const EphemeralRecord &er, meshtastic_NodeInfoLite *node);
    StoreConfig detectPlatformConfig();
};

// ============================================================================
// Global Instance
// ============================================================================

extern NodeDBAdapter *g_nodedb_adapter;

// Initialization helper
bool initNodeDBLSM();
void shutdownNodeDBLSM();

} // namespace tinylsm
} // namespace meshtastic
