// Example usage of Tiny-LSM for Meshtastic NodeDB
// This file demonstrates the basic API usage

#include "configuration.h"
#include "tinylsm_adapter.h"
#include "tinylsm_store.h"

using namespace meshtastic::tinylsm;

// Example 1: Direct Store API usage
void example_direct_usage()
{
    LOG_INFO("=== Example 1: Direct Store API ===");

    // Create store with platform-specific config
    NodeDBStore store;

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    StoreConfig config = StoreConfig::esp32_psram();
#elif defined(ARCH_ESP32)
    StoreConfig config = StoreConfig::esp32_no_psram();
#else
    StoreConfig config = StoreConfig::nrf52();
#endif

    // Initialize
    if (!store.init(config)) {
        LOG_ERROR("Failed to initialize store");
        return;
    }

    // Write some durable records (node identity)
    for (uint32_t i = 0; i < 10; i++) {
        DurableRecord dr;
        dr.node_id = 0x10000 + i;
        snprintf(dr.long_name, sizeof(dr.long_name), "Node-%u", i);
        snprintf(dr.short_name, sizeof(dr.short_name), "N%u", i);
        dr.hw_model = 1; // Example hardware model

        if (!store.putDurable(dr, false)) {
            LOG_ERROR("Failed to write durable record for node %u", i);
        }
    }

    // Write some ephemeral records (routing & metrics - hot path)
    for (uint32_t i = 0; i < 10; i++) {
        EphemeralRecord er;
        er.node_id = 0x10000 + i;
        er.last_heard_epoch = get_epoch_time();
        er.next_hop = (i > 0) ? (0x10000 + i - 1) : 0; // Route through previous node
        er.snr = 10 + (i % 5);
        er.rssi_avg = -80 + (i % 20);
        er.role = i % 3; // Mix of roles
        er.hop_limit = 1 + (i % 3);
        er.channel = i % 8; // Different channels
        er.battery_level = 85 + (i % 15);

        if (!store.putEphemeral(er)) {
            LOG_ERROR("Failed to write ephemeral record for node %u", i);
        }
    }

    // Read back a node
    uint32_t test_node = 0x10005;
    auto dr_result = store.getDurable(test_node);
    if (dr_result.found) {
        LOG_INFO("Found durable record: node_id=0x%08X, name=%s", dr_result.value.node_id, dr_result.value.long_name);
    }

    auto er_result = store.getEphemeral(test_node);
    if (er_result.found) {
        LOG_INFO("Found ephemeral record: last_heard=%u, next_hop=0x%08X, snr=%d, hop_limit=%u, channel=%u, role=%u",
                 er_result.value.last_heard_epoch, er_result.value.next_hop, er_result.value.snr, er_result.value.hop_limit,
                 er_result.value.channel, er_result.value.role);
    }

    // Print statistics
    StoreStats s1 = store.stats();
    LOG_INFO("Durable: %u entries in memtable, %u SortedTables", s1.durable_memtable_entries, s1.durable_sstables);
    LOG_INFO("Ephemeral: %u entries in memtable, %u SortedTables", s1.ephemeral_memtable_entries, s1.ephemeral_sstables);

    // Simulate background maintenance
    LOG_INFO("Running background maintenance...");
    for (int i = 0; i < 5; i++) {
        store.tick();
        delay(100);
    }

    // Force a checkpoint
    LOG_INFO("Forcing ephemeral checkpoint...");
    store.requestCheckpointEphemeral();

    // Shutdown
    store.shutdown();
    LOG_INFO("Store shut down successfully");
}

// Example 2: Using the Meshtastic adapter
void example_adapter_usage()
{
    LOG_INFO("=== Example 2: Meshtastic Adapter ===");

    // Initialize adapter (auto-detects platform)
    if (!initNodeDBLSM()) {
        LOG_ERROR("Failed to initialize NodeDB LSM adapter");
        return;
    }

    // Create a test node
    meshtastic_NodeInfoLite node;
    memset(&node, 0, sizeof(node));
    node.num = 0x12345678;
    node.last_heard = get_epoch_time();
    node.next_hop = 0x44; // Next hop for routing (last byte of node number, uint8_t)
    node.snr = 15.0f;
    node.hops_away = 2;
    node.channel = 3; // Channel number
    strcpy(node.user.long_name, "Test Node");
    strcpy(node.user.short_name, "TST");
    node.user.hw_model = meshtastic_HardwareModel_TBEAM;
    node.user.role = meshtastic_Config_DeviceConfig_Role_ROUTER;

    // Save node
    if (g_nodedb_adapter->saveNode(&node)) {
        LOG_INFO("Node saved successfully");
    } else {
        LOG_ERROR("Failed to save node");
    }

    // Load node back
    meshtastic_NodeInfoLite loaded_node;
    if (g_nodedb_adapter->loadNode(0x12345678, &loaded_node)) {
        LOG_INFO("Loaded node: %s (next_hop=0x%08X, SNR=%d, hop_limit=%u, channel=%u)", loaded_node.user.long_name,
                 static_cast<uint32_t>(loaded_node.next_hop), loaded_node.snr, loaded_node.hops_away, loaded_node.channel);
    } else {
        LOG_ERROR("Failed to load node");
    }

    // Background tick (call this periodically from main loop)
    g_nodedb_adapter->tick();

    // Log statistics
    g_nodedb_adapter->logStats();

    // Shutdown
    shutdownNodeDBLSM();
    LOG_INFO("Adapter shut down successfully");
}

// Example 3: Stress test (write many nodes)
void example_stress_test()
{
    LOG_INFO("=== Example 3: Stress Test ===");

    NodeDBStore store;
    StoreConfig config = StoreConfig::esp32_psram();
    config.memtable_durable_kb = 128; // Smaller to trigger more flushes
    config.memtable_ephemeral_kb = 64;

    if (!store.init(config)) {
        LOG_ERROR("Failed to initialize store");
        return;
    }

    const uint32_t num_nodes = 1000;
    LOG_INFO("Writing %u nodes...", num_nodes);

    uint32_t start_time = millis();

    for (uint32_t i = 0; i < num_nodes; i++) {
        DurableRecord dr;
        dr.node_id = 0x20000 + i;
        snprintf(dr.long_name, sizeof(dr.long_name), "StressNode-%u", i);
        snprintf(dr.short_name, sizeof(dr.short_name), "S%u", i % 100);

        EphemeralRecord er;
        er.node_id = 0x20000 + i;
        er.last_heard_epoch = get_epoch_time() - (i % 3600);
        er.next_hop = (i > 0) ? (0x20000 + (i - 1)) : 0;
        er.snr = -10 + (i % 30);
        er.hop_limit = 1 + (i % 5);
        er.channel = i % 8;
        er.role = i % 3;

        store.putDurable(dr, false);
        store.putEphemeral(er);

        // Periodic tick
        if (i % 100 == 0) {
            store.tick();
        }
    }

    uint32_t write_time = millis() - start_time;
    LOG_INFO("Wrote %u nodes in %u ms (%.2f nodes/sec)", num_nodes, write_time, 1000.0f * num_nodes / write_time);

    // Read back random samples
    LOG_INFO("Reading back random samples...");
    start_time = millis();
    uint32_t found_count = 0;

    for (uint32_t i = 0; i < 100; i++) {
        uint32_t node_id = 0x20000 + (rand() % num_nodes);
        auto result = store.getDurable(node_id);
        if (result.found) {
            found_count++;
        }
    }

    uint32_t read_time = millis() - start_time;
    LOG_INFO("Read 100 nodes in %u ms, found %u (%.2f reads/sec)", read_time, found_count, 100000.0f / read_time);

    // Statistics
    StoreStats s = store.stats();
    LOG_INFO("Final stats:");
    LOG_INFO("  Durable: %u SortedTables, %u bytes", s.durable_sstables, s.durable_total_bytes);
    LOG_INFO("  Ephemeral: %u SortedTables, %u bytes", s.ephemeral_sstables, s.ephemeral_total_bytes);
    LOG_INFO("  Compactions: %u", s.compactions_total);
    LOG_INFO("  SortedTables written: %u", s.sstables_written);

    store.shutdown();
}

// Call from setup() or main()
void tinylsm_examples()
{
    LOG_INFO("Starting Tiny-LSM examples...");

    // Run examples
    example_direct_usage();
    delay(1000);

    example_adapter_usage();
    delay(1000);

    // Stress test (may take a while)
    // example_stress_test();

    LOG_INFO("Examples completed");
}
