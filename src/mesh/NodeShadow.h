#pragma once

#include <cstdint>

/**
 * Lightweight shadow index entry for NodeDB
 *
 * This is a minimal 16-byte structure that allows fast iteration
 * and sorting without keeping full node data in RAM. Full node data
 * is stored in LSM and loaded on-demand.
 *
 * Memory comparison:
 * - Old: 500 nodes × 200 bytes = 100 KB
 * - New: 3000 nodes × 16 bytes = 48 KB (52 KB saved, 6x capacity!)
 */
struct NodeShadow {
    uint32_t node_id;    // Node identifier (4 bytes)
    uint32_t last_heard; // Last heard time for sorting (4 bytes)

    // Packed flags (4 bytes) - frequently accessed metadata
    uint32_t is_favorite : 1;
    uint32_t is_ignored : 1;
    uint32_t has_user : 1;
    uint32_t has_position : 1;
    uint32_t via_mqtt : 1;
    uint32_t has_hops_away : 1;
    uint32_t reserved_flags : 10; // Future use
    uint32_t hops_away : 8;       // 0-255
    uint32_t channel : 8;         // 0-255

    uint32_t sort_key; // Precomputed for fast sorting (4 bytes)

    NodeShadow()
        : node_id(0), last_heard(0), is_favorite(0), is_ignored(0), has_user(0), has_position(0), via_mqtt(0), has_hops_away(0),
          reserved_flags(0), hops_away(0), channel(0), sort_key(0)
    {
    }

    NodeShadow(uint32_t id, uint32_t heard)
        : node_id(id), last_heard(heard), is_favorite(0), is_ignored(0), has_user(0), has_position(0), via_mqtt(0),
          has_hops_away(0), reserved_flags(0), hops_away(0), channel(0), sort_key(0)
    {
        update_sort_key(0); // Assume not our node initially
    }

    // Update sort key for fast sorting
    // Priority: Our node (0) > Favorites (1) > Last heard (2+)
    void update_sort_key(uint32_t our_node_id)
    {
        if (node_id == our_node_id) {
            sort_key = 0; // Always first
        } else if (is_favorite) {
            sort_key = 1; // Favorites second
        } else {
            // Invert last_heard so recent = smaller sort_key
            sort_key = 0xFFFFFFFF - last_heard;
        }
    }

    // For std::sort
    bool operator<(const NodeShadow &other) const { return sort_key < other.sort_key; }
};

// Verify size is exactly 16 bytes
static_assert(sizeof(NodeShadow) == 16, "NodeShadow must be exactly 16 bytes for memory efficiency");
