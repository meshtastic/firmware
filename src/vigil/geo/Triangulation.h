#pragma once

#include <cstdint>

namespace vigil {

/*
 * Triangulation — Least-squares vector intersection from ≥2 nodes.
 *
 * Architecture decision #15: GDOP confidence flag.
 * Architecture decision #6 (CEO): Angular clustering with cluster IDs.
 *
 *   Node A (DoA vec) ──┐
 *   Node B (DoA vec) ──┼──► Least-squares intersection ──► 3D coordinate
 *   Node C (DoA vec) ──┘         │
 *                                ▼
 *                          GDOP computation
 *                          (narrow baseline = low confidence)
 */

struct GeoCoord {
    double lat;
    double lon;
    float alt_m;
};

struct TriangulationInput {
    uint16_t node_id;
    double node_lat;
    double node_lon;
    float azimuth_deg;     // Global bearing (heading-corrected)
    float elevation_deg;
    float confidence;
    uint8_t cluster_id;
    uint32_t timestamp_gps;
};

enum class TriangError : uint8_t {
    OK = 0,
    INSUFFICIENT_VECTORS,  // Need ≥2
    VECTORS_DIVERGENT,     // Lines don't converge
    VECTORS_STALE,         // Timestamps too old (>5s)
};

struct TriangResult {
    TriangError error;
    GeoCoord position;
    float confidence;      // Combined confidence
    float gdop;            // Geometric Dilution of Precision
    uint8_t cluster_id;
    uint8_t num_vectors;   // How many vectors contributed
};

class Triangulation {
  public:
    // Add a DoA vector from a neighbor node (received via CH1)
    void addVector(const TriangulationInput &vec);

    // Attempt triangulation for a given cluster ID
    // Returns OK if ≥2 vectors converge, error otherwise
    TriangResult solve(uint8_t cluster_id) const;

    // Purge stale vectors (call periodically)
    void purgeStale(uint32_t max_age_ms);

    // Clear all vectors
    void reset();

  private:
    static constexpr uint8_t MAX_VECTORS = 16;
    static constexpr uint32_t STALE_THRESHOLD_MS = 5000;

    TriangulationInput vectors[MAX_VECTORS];
    uint8_t num_vectors = 0;

    float computeGdop(const TriangulationInput *vecs, uint8_t count) const;
};

} // namespace vigil
