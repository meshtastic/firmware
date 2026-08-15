#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/mesh-pb-constants.h"
#include <cstdint>

/**
 * NodeDBScalingModule: ratchets the NodeDB satellite caps down under megamesh pressure.
 *
 * The problem: on a constrained part the satellite stores (position / telemetry /
 * environment / status) cost ~452 B RAM and 336 B flash per node at their worst, against
 * 100 B / 112 B for the NodeInfoLite header itself. In a mesh large enough to churn the
 * hot store, that budget is better spent on identities than on stale payloads for nodes
 * we will not see again.
 *
 * The policy: a step ladder, one step per hourly evaluation, driven by HopScalingModule's
 * population estimate and gated on evidence that the pressure is actually on *us* (a full
 * hot store, or evictions since the last evaluation). Descending is fast (one step per
 * hour while the population warrants it); ascending needs QUIET_HOURS_PER_STEP consecutive
 * quiet hours per step, so a mesh that briefly quietens does not thrash the caps.
 *
 * Ladder (percentages of MAX_SATELLITE_NODES, i.e. of 40 on a constrained part):
 *
 *   step  population  satellites (pos/tel)  bulk (env/status)
 *   ----  ----------  --------------------  -----------------
 *     0            -              40 (100%)          40 (100%)
 *     1         >=200              24  (60%)          24  (60%)
 *     2         >=400              16  (40%)          16  (40%)
 *     3         >=800               8  (20%)           8  (20%)
 *     4        >=1500               8  (20%)           4  (10%)
 *
 * Position and telemetry share a ladder because the on-device UI reads both (distance,
 * bearing, battery column). Environment and status get the extra step because
 * EnvironmentMetrics is the elephant - 196 B RAM / 170 B flash per entry, 4-5x anything
 * else - and is the least useful thing to persist in a megamesh.
 *
 * UI floor: satellite entries feed the local display, so builds that draw a map or a node
 * list keep an absolute minimum of them regardless of how far the ladder descends
 * (UI_SATELLITE_FLOOR below). A headless router has no such reader and ratchets freely.
 *
 * No persistence: the step is re-derived on the first evaluation after boot from
 * HopScalingModule's own warm-started estimate, so a reboot costs at most one startup
 * delay of over-capacity, not a state file.
 */

#if HAS_NODEDB_SCALING

/// Absolute minimum satellite entries kept for builds whose UI reads them. InkHUD's map
/// applet draws every node with a position, so it needs more than a node list that only
/// resolves distance for the row in view; a headless build reads none of it.
#ifndef NODEDB_SCALING_UI_SATELLITE_FLOOR
#if defined(MESHTASTIC_INCLUDE_INKHUD)
#define NODEDB_SCALING_UI_SATELLITE_FLOOR 12
#elif HAS_SCREEN
#define NODEDB_SCALING_UI_SATELLITE_FLOOR 8
#else
#define NODEDB_SCALING_UI_SATELLITE_FLOOR 4
#endif
#endif

class NodeDBScalingModule : private concurrency::OSThread
{
  public:
    // -----------------------------------------------------------------------
    // Ladder
    // -----------------------------------------------------------------------
    struct Step {
        uint16_t enterPopulation; ///< estimated mesh population at or above which this step applies
        uint8_t satellitePct;     ///< percent of MAX_SATELLITE_NODES kept for position + telemetry
        uint8_t bulkPct;          ///< percent of MAX_SATELLITE_NODES kept for environment + status
    };

    static constexpr Step STEPS[] = {
        {0, 100, 100}, {200, 60, 60}, {400, 40, 40}, {800, 20, 20}, {1500, 20, 10},
    };
    static constexpr uint8_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);
    static constexpr uint8_t MAX_STEP = STEP_COUNT - 1;

    /// A step is released (ratcheted back up) only once the population drops below this
    /// percentage of the step's own entry threshold - the hysteresis band that stops a
    /// mesh hovering at ~200 nodes from stepping down and up every hour.
    static constexpr uint8_t RELEASE_PCT = 75;

    /// Consecutive quiet evaluations required to release one step. Descent is one step per
    /// evaluation; ascent is deliberately six times slower.
    static constexpr uint8_t QUIET_HOURS_PER_STEP = 6;

    // -----------------------------------------------------------------------
    // Floors
    // -----------------------------------------------------------------------
    /// Absolute minimum satellite entries kept for builds whose UI reads them; see the
    /// NODEDB_SCALING_UI_SATELLITE_FLOOR block above this class.
    static constexpr uint16_t UI_SATELLITE_FLOOR = NODEDB_SCALING_UI_SATELLITE_FLOOR;

    /// Environment and status have no map/list reader to starve, so they floor low
    /// everywhere - just enough to keep the nearest few sensor nodes.
    static constexpr uint16_t BULK_FLOOR = 4;

    NodeDBScalingModule();
    ~NodeDBScalingModule() = default;

    // -----------------------------------------------------------------------
    // Published caps - read by NodeDB's satellite eviction paths
    // -----------------------------------------------------------------------
    uint16_t getSatelliteCap() const { return satelliteCap; }
    uint16_t getEnvironmentCap() const { return environmentCap; }
    uint8_t getStep() const { return step; }

    /// Apply a step directly. Recomputes the caps and, when the step moved, trims the
    /// satellite maps to match. Public for tests and for a future admin override.
    void setStep(uint8_t newStep);

    /// One evaluation of the ladder against the supplied signals. Separated from runOnce()
    /// so tests can drive it without a clock or a live HopScalingModule.
    /// @param population estimated mesh population (HopScalingModule's scaled total)
    /// @param underPressure true when the hot store is full or has evicted since the last call
    void evaluate(uint16_t population, bool underPressure);

    /// Ladder lookup: the step this population alone warrants, before hysteresis.
    static uint8_t stepForPopulation(uint16_t population);

    /// Percentage of MAX_SATELLITE_NODES, floored. Exposed for tests.
    static uint16_t capForPct(uint8_t pct, uint16_t floor);

  protected:
    int32_t runOnce() override;

  private:
    /// Recompute satelliteCap / environmentCap from the current step.
    void refreshCaps();

    /// Read the current population estimate from HopScalingModule, or 0 when it has none yet.
    static uint16_t currentPopulationEstimate();

    uint8_t step = 0;
    uint8_t quietEvaluations = 0; ///< consecutive evaluations that warranted a release
    uint16_t satelliteCap = MAX_SATELLITE_NODES;
    uint16_t environmentCap = MAX_SATELLITE_NODES;
    uint32_t lastEvictionCount = 0; ///< NodeDB::hotEvictions at the previous evaluation
};

extern NodeDBScalingModule *nodeDBScalingModule;

#endif // HAS_NODEDB_SCALING

/// Effective per-map satellite caps. Both fall back to the compile-time
/// MAX_SATELLITE_NODES when the module is compiled out or not yet constructed, so NodeDB
/// can call them at any point in boot.
uint16_t nodeDBSatelliteCap();
uint16_t nodeDBEnvironmentCap();
