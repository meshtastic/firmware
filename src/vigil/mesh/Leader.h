#pragma once

#include <cstdint>

namespace vigil {

/*
 * Leader — Distributed broadcast ownership based on confidence.
 *
 * Architecture decision #2 (CEO): Highest-confidence node
 * owns the CH2 broadcast for a given track. Others suppress.
 *
 *   Node A (conf 0.85) ──┐
 *   Node B (conf 0.92) ──┼──► B wins, broadcasts on CH2
 *   Node C (conf 0.78) ──┘    A and C suppress their broadcasts
 *
 * No election protocol needed. Nodes compare their own
 * confidence against received CH2 broadcasts. If a
 * matching track was already broadcast with higher
 * confidence, suppress.
 */

class Leader {
  public:
    // Should this node broadcast a drone alert for the given cluster?
    // Returns false if a higher-confidence broadcast was already heard.
    bool shouldBroadcast(uint8_t cluster_id, float my_confidence) const;

    // Record that we heard a CH2 broadcast from another node
    void recordBroadcast(uint8_t cluster_id, float their_confidence, uint32_t timestamp);

    // Purge stale records
    void purgeStale(uint32_t max_age_ms);

  private:
    static constexpr uint8_t MAX_TRACKS = 8;

    struct TrackRecord {
        uint8_t cluster_id;
        float best_confidence;
        uint32_t timestamp;
        bool active;
    };

    TrackRecord records[MAX_TRACKS] = {};
};

} // namespace vigil
