#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/mesh-pb-constants.h"
#include <cstdint>

/**
 * NodeDBScalingModule: ratchets the NodeDB satellite caps down under megamesh pressure.
 *
 * Satellite payloads (position / telemetry / environment / status) cost ~452 B RAM and 336 B
 * flash per node against 100 B / 112 B for the NodeInfoLite header, so once the mesh is large
 * enough to churn the hot store that budget buys more identities than stale payloads. The freed
 * flash is spent on hot-store slots (NodeDB::applyHotStoreCapacity), filled passively from
 * observed traffic so a larger store never buys extra handshake airtime.
 *
 *   step  population  sat (pos/tel)  bulk (env/status)
 *   ----  ----------  -------------  -----------------
 *      0           -      40 (100%)          40 (100%)
 *      1       >=200      24  (60%)          24  (60%)
 *      2       >=400      16  (40%)          16  (40%)
 *      3       >=800       8  (20%)           8  (20%)
 *      4      >=1500       8  (20%)           4  (10%)
 *
 * Percentages are of MAX_SATELLITE_NODES (40 on a constrained part). Env/status ratchet one step
 * deeper: EnvironmentMetrics is 4-5x any other entry and the least useful thing to persist, while
 * position and telemetry both feed the on-device UI and keep a floor for it. Descent is one step
 * per hourly evaluation and needs evidence the squeeze is on us (full hot store, or an eviction
 * since the last evaluation); ascent needs QUIET_HOURS_PER_STEP consecutive quiet evaluations.
 * The step is not persisted - it re-derives from HopScalingModule's warm-started estimate.
 *
 * Accepted trade: PACKETHISTORY_MAX stays sized from the baseline hot store, so dedup coverage
 * per node thins while the ratchet is engaged. See its comment in mesh-pb-constants.h.
 */

#if HAS_NODEDB_SCALING

/// Minimum satellite entries kept for builds whose UI reads them: InkHUD's map applet draws every
/// positioned node, a node list resolves only the row in view, a headless build reads none of it.
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

    /// Release band: a step is given back only below this percentage of its own entry threshold,
    /// so a mesh hovering at ~200 nodes does not step down and up every hour.
    static constexpr uint8_t RELEASE_PCT = 75;

    /// Consecutive quiet evaluations required to release one step; descent takes one.
    static constexpr uint8_t QUIET_HOURS_PER_STEP = 6;

    // -----------------------------------------------------------------------
    // Floors
    // -----------------------------------------------------------------------
    /// Minimum kept for a UI that reads them; see NODEDB_SCALING_UI_SATELLITE_FLOOR above.
    static constexpr uint16_t UI_SATELLITE_FLOOR = NODEDB_SCALING_UI_SATELLITE_FLOOR;

    /// Environment and status have no map/list reader to starve, so they floor low everywhere.
    static constexpr uint16_t BULK_FLOOR = 4;

    NodeDBScalingModule();
    ~NodeDBScalingModule() = default;

    // -----------------------------------------------------------------------
    // Published caps - read by NodeDB's satellite eviction paths
    // -----------------------------------------------------------------------
    uint16_t getSatelliteCap() const { return satelliteCap; }
    uint16_t getEnvironmentCap() const { return environmentCap; }
    uint8_t getStep() const { return step; }

    /// Hot-store slots this step's savings fund, priced in the worst-case on-disk bytes that size
    /// MAX_NUM_NODES. Funding only - NodeDB clamps to its decode ceiling and to the live heap.
    uint16_t getBonusNodes() const { return bonusNodes; }

    /// Worst-case bytes reclaimed from nodes.proto at the given caps, and the slots they buy.
    static uint32_t freedFlashBytes(uint16_t satCap, uint16_t envCap);
    static uint16_t bonusForCaps(uint16_t satCap, uint16_t envCap);

    /// One-shot startup report: the ladder, and what each step would cost and fund.
    void logLadder() const;

    /// Apply a step directly, trimming the satellite maps when it moved. Public for tests
    /// and for a future admin override.
    void setStep(uint8_t newStep);

    /// One ladder evaluation; split from runOnce() so tests need no clock or HopScalingModule.
    /// @param population HopScalingModule's scaled total
    /// @param underPressure hot store full, or evicted since the last call
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
    uint16_t bonusNodes = 0;
    uint32_t lastEvictionCount = 0; ///< NodeDB::hotEvictions at the previous evaluation
};

extern NodeDBScalingModule *nodeDBScalingModule;

#endif // HAS_NODEDB_SCALING

/// Effective per-map satellite caps. Both fall back to MAX_SATELLITE_NODES when the module is
/// compiled out or not yet constructed, so NodeDB can call them at any point in boot.
uint16_t nodeDBSatelliteCap();
uint16_t nodeDBEnvironmentCap();

/// Extra hot-store slots funded by the satellite savings; 0 when unpressured or compiled out.
uint16_t nodeDBBonusNodes();
