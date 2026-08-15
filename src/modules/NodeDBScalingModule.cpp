#include "NodeDBScalingModule.h"

#if HAS_NODEDB_SCALING

#include "HopScalingModule.h"
#include "NodeDB.h"

namespace
{
// First evaluation is deferred past HopScalingModule's warm-start load and its first hourly
// rollover, which is what publishes the population estimate we read.
constexpr uint32_t INITIAL_DELAY_MS = 15 * 60 * 1000UL;
constexpr uint32_t RUN_INTERVAL_MS = 60 * 60 * 1000UL;
// Retry sooner while the population estimate is still unavailable (no rollover yet).
constexpr uint32_t RETRY_INTERVAL_MS = 5 * 60 * 1000UL;
} // namespace

NodeDBScalingModule *nodeDBScalingModule = nullptr;

NodeDBScalingModule::NodeDBScalingModule() : concurrency::OSThread("NodeDBScaling")
{
    refreshCaps();
    logLadder();
    setIntervalFromNow(INITIAL_DELAY_MS);
}

uint16_t NodeDBScalingModule::capForPct(uint8_t pct, uint16_t floor)
{
    uint32_t cap = ((uint32_t)MAX_SATELLITE_NODES * pct) / 100u;
    if (cap < floor)
        cap = floor;
    if (cap > (uint32_t)MAX_SATELLITE_NODES)
        cap = MAX_SATELLITE_NODES; // a floor must never raise the cap above the unpressured one
    return (uint16_t)cap;
}

uint8_t NodeDBScalingModule::stepForPopulation(uint16_t population)
{
    uint8_t warranted = 0;
    for (uint8_t i = 1; i < STEP_COUNT; i++) {
        if (population >= STEPS[i].enterPopulation)
            warranted = i;
    }
    return warranted;
}

uint32_t NodeDBScalingModule::freedFlashBytes(uint16_t satCap, uint16_t envCap)
{
    // Priced in the currency that sizes MAX_NUM_NODES: worst-case encoded bytes in nodes.proto,
    // split across the two caps exactly as enforceSatelliteCaps() trims them.
    const uint16_t satDropped = (satCap < MAX_SATELLITE_NODES) ? (uint16_t)(MAX_SATELLITE_NODES - satCap) : 0;
    const uint16_t envDropped = (envCap < MAX_SATELLITE_NODES) ? (uint16_t)(MAX_SATELLITE_NODES - envCap) : 0;
    return (uint32_t)satDropped * (meshtastic_NodePositionEntry_size + meshtastic_NodeTelemetryEntry_size) +
           (uint32_t)envDropped * (meshtastic_NodeEnvironmentEntry_size + meshtastic_NodeStatusEntry_size);
}

uint16_t NodeDBScalingModule::bonusForCaps(uint16_t satCap, uint16_t envCap)
{
    return (uint16_t)(freedFlashBytes(satCap, envCap) / meshtastic_NodeInfoLite_size);
}

void NodeDBScalingModule::refreshCaps()
{
    satelliteCap = capForPct(STEPS[step].satellitePct, UI_SATELLITE_FLOOR);
    environmentCap = capForPct(STEPS[step].bulkPct, BULK_FLOOR);
    bonusNodes = bonusForCaps(satelliteCap, environmentCap);
}

void NodeDBScalingModule::logLadder() const
{
    LOG_INFO("NodeDB scaling: base sats %d, hot store %d (passive-fill above %d)", MAX_SATELLITE_NODES, (int)MAX_NUM_NODES,
             (int)NODEDB_BASELINE_NODES);
    for (uint8_t i = 0; i < STEP_COUNT; i++) {
        const uint16_t sat = capForPct(STEPS[i].satellitePct, UI_SATELLITE_FLOOR);
        const uint16_t env = capForPct(STEPS[i].bulkPct, BULK_FLOOR);
        LOG_DEBUG("  step %u: pop>=%u sats %u env %u frees %uB -> +%u nodes", i, STEPS[i].enterPopulation, sat, env,
                  (unsigned)freedFlashBytes(sat, env), bonusForCaps(sat, env));
    }
}

void NodeDBScalingModule::setStep(uint8_t newStep)
{
    if (newStep > MAX_STEP)
        newStep = MAX_STEP;
    // An explicit step change restarts the hysteresis run either way: a release run counted
    // against the step we just left says nothing about the one we just entered.
    quietEvaluations = 0;
    if (newStep == step)
        return;

    step = newStep;
    refreshCaps();
    LOG_INFO("NodeDB scaling: step %u - sats %u, env/status %u (of %d), +%u nodes", step, satelliteCap, environmentCap,
             MAX_SATELLITE_NODES, bonusNodes);
    // Descending must reclaim immediately; ascending is a no-op here and refills from traffic.
    if (nodeDB) {
        nodeDB->enforceSatelliteCaps();
        // Spend (or hand back) the freed budget. Growth is heap-guarded and may decline.
        nodeDB->applyHotStoreCapacity();
    }
}

void NodeDBScalingModule::evaluate(uint16_t population, bool underPressure)
{
    const uint8_t warranted = stepForPopulation(population);

    if (warranted > step) {
        // A population that warrants *more* ratchet voids any release run in progress, whether
        // or not we act on it - otherwise quiet hours accumulate across a busy spell between them.
        quietEvaluations = 0;
        // Population alone is not enough to descend: a node parked beside a busy repeater hears a
        // big mesh without its own store ever being squeezed. Require evidence of the squeeze.
        if (underPressure)
            setStep(step + 1);
        return;
    }

    if (step == 0)
        return;

    // Release only once the population has fallen clear of the band around this step's own
    // entry threshold, and stayed there.
    const uint32_t releaseBelow = ((uint32_t)STEPS[step].enterPopulation * RELEASE_PCT) / 100u;
    if (population >= releaseBelow) {
        quietEvaluations = 0;
        return;
    }

    if (++quietEvaluations >= QUIET_HOURS_PER_STEP) {
        quietEvaluations = 0;
        setStep(step - 1);
    }
}

uint16_t NodeDBScalingModule::currentPopulationEstimate()
{
    if (!hopScalingModule)
        return 0;
    // Already scaled by the filtering denominator, and only counts nodes seen in the last
    // 13 h - the hysteresis we would otherwise have to build ourselves.
    return hopScalingModule->getLastPerHopCounts().total;
}

int32_t NodeDBScalingModule::runOnce()
{
    const uint16_t population = currentPopulationEstimate();
    if (population == 0)
        return RETRY_INTERVAL_MS; // no rollover yet; hold the current step rather than release it

    const uint32_t evictions = nodeDB ? nodeDB->hotEvictions : 0;
    const bool underPressure = (nodeDB && nodeDB->isFull()) || evictions != lastEvictionCount;
    lastEvictionCount = evictions;

    evaluate(population, underPressure);
    return RUN_INTERVAL_MS;
}

uint16_t nodeDBSatelliteCap()
{
    return nodeDBScalingModule ? nodeDBScalingModule->getSatelliteCap() : (uint16_t)MAX_SATELLITE_NODES;
}

uint16_t nodeDBEnvironmentCap()
{
    return nodeDBScalingModule ? nodeDBScalingModule->getEnvironmentCap() : (uint16_t)MAX_SATELLITE_NODES;
}

uint16_t nodeDBBonusNodes()
{
    return nodeDBScalingModule ? nodeDBScalingModule->getBonusNodes() : 0;
}

#else // HAS_NODEDB_SCALING

uint16_t nodeDBSatelliteCap()
{
    return MAX_SATELLITE_NODES;
}

uint16_t nodeDBEnvironmentCap()
{
    return MAX_SATELLITE_NODES;
}

uint16_t nodeDBBonusNodes()
{
    return 0;
}

#endif // HAS_NODEDB_SCALING
