// Tests for the megamesh satellite ratchet - src/modules/NodeDBScalingModule.cpp - and the
// effective-cap plumbing it drives in NodeDB::enforceSatelliteCaps().
#include "MeshTypes.h" // BEFORE TestUtil.h - provides MAX_SATELLITE_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NDB_TEST_ENTRY extern "C"
#else
#define NDB_TEST_ENTRY
#endif

#if HAS_NODEDB_SCALING

#include "mesh/NodeDB.h"
#include "modules/NodeDBScalingModule.h"
#include <cstring>

// Subclass shim: lets a test own the hot store directly so the satellite trim has
// last_heard values to rank against. Global scope to match `friend class NodeDBTestShim`.
class NodeDBTestShim : public NodeDB
{
  public:
    void clearHot()
    {
        meshNodes->clear();
        numMeshNodes = 0;
    }

    /// Push a hot-store row and give it a position + environment satellite entry.
    void pushWithSatellites(NodeNum num, uint32_t lastHeard)
    {
        meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
        n.num = num;
        n.last_heard = lastHeard;
        meshNodes->push_back(n);
        numMeshNodes = meshNodes->size();

        meshtastic_PositionLite pos = meshtastic_PositionLite_init_zero;
        pos.time = lastHeard;
        nodePositions[num] = pos;
        nodeEnvironment[num] = meshtastic_EnvironmentMetrics_init_zero;
    }

    /// Bare hot-store rows, no satellites - for occupancy/gate tests.
    void fillHot(pb_size_t count)
    {
        clearHot();
        for (pb_size_t i = 0; i < count; i++) {
            meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
            n.num = 0x2000 + i;
            n.last_heard = 1000 + i;
            meshNodes->push_back(n);
        }
        numMeshNodes = count;
        // Keep the backing store at the live cap, as production does after applyHotStoreCapacity().
        if (meshNodes->size() < effectiveMaxNodes())
            meshNodes->resize(effectiveMaxNodes());
    }
};

namespace
{

NodeDBTestShim *db = nullptr;
NodeDBScalingModule *mod = nullptr;

constexpr uint16_t pctOfBase(uint8_t pct)
{
    return (uint16_t)(((uint32_t)MAX_SATELLITE_NODES * pct) / 100u);
}

// ---------------------------------------------------------------------------
// Ladder lookup
// ---------------------------------------------------------------------------

void test_stepForPopulation_walksTheLadder()
{
    TEST_ASSERT_EQUAL_UINT8(0, NodeDBScalingModule::stepForPopulation(0));
    TEST_ASSERT_EQUAL_UINT8(0, NodeDBScalingModule::stepForPopulation(199));
    TEST_ASSERT_EQUAL_UINT8(1, NodeDBScalingModule::stepForPopulation(200));
    TEST_ASSERT_EQUAL_UINT8(1, NodeDBScalingModule::stepForPopulation(399));
    TEST_ASSERT_EQUAL_UINT8(2, NodeDBScalingModule::stepForPopulation(400));
    TEST_ASSERT_EQUAL_UINT8(3, NodeDBScalingModule::stepForPopulation(800));
    TEST_ASSERT_EQUAL_UINT8(4, NodeDBScalingModule::stepForPopulation(1500));
    TEST_ASSERT_EQUAL_UINT8(4, NodeDBScalingModule::stepForPopulation(65535));
}

// The published ladder is 100/60/40/20 for satellites and 100/60/40/20/10 for the bulk
// stores - i.e. 40-24-16-8 and 40-24-16-8-4 on a part whose base cap is 40.
void test_ladderPercentagesMatchThePublishedSteps()
{
    static const uint8_t expectedSat[] = {100, 60, 40, 20, 20};
    static const uint8_t expectedBulk[] = {100, 60, 40, 20, 10};
    TEST_ASSERT_EQUAL_UINT8(5, NodeDBScalingModule::STEP_COUNT);
    for (uint8_t i = 0; i < NodeDBScalingModule::STEP_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT8(expectedSat[i], NodeDBScalingModule::STEPS[i].satellitePct);
        TEST_ASSERT_EQUAL_UINT8(expectedBulk[i], NodeDBScalingModule::STEPS[i].bulkPct);
    }
}

void test_capForPct_appliesFloorAndCeiling()
{
    // A percentage above the floor passes through unchanged.
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, NodeDBScalingModule::capForPct(100, 1));
    TEST_ASSERT_EQUAL_UINT16(pctOfBase(60), NodeDBScalingModule::capForPct(60, 1));
    // A floor raises a cap that fell below it...
    TEST_ASSERT_EQUAL_UINT16(pctOfBase(20) + 5, NodeDBScalingModule::capForPct(20, pctOfBase(20) + 5));
    // ...but never above the unpressured cap.
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, NodeDBScalingModule::capForPct(10, MAX_SATELLITE_NODES + 100));
}

// ---------------------------------------------------------------------------
// Ratchet
// ---------------------------------------------------------------------------

void test_descendsOneStepPerEvaluation()
{
    // A population that warrants step 4 still only moves one step at a time.
    mod->evaluate(2000, /*underPressure=*/true);
    TEST_ASSERT_EQUAL_UINT8(1, mod->getStep());
    mod->evaluate(2000, true);
    TEST_ASSERT_EQUAL_UINT8(2, mod->getStep());
    mod->evaluate(2000, true);
    TEST_ASSERT_EQUAL_UINT8(3, mod->getStep());
    mod->evaluate(2000, true);
    TEST_ASSERT_EQUAL_UINT8(4, mod->getStep());
    // ...and stops at the bottom of the ladder.
    mod->evaluate(2000, true);
    TEST_ASSERT_EQUAL_UINT8(NodeDBScalingModule::MAX_STEP, mod->getStep());
}

// Hearing a big mesh is not enough: the squeeze has to be on our own store.
void test_populationWithoutPressureDoesNotDescend()
{
    for (int i = 0; i < 10; i++)
        mod->evaluate(2000, /*underPressure=*/false);
    TEST_ASSERT_EQUAL_UINT8(0, mod->getStep());
}

void test_capsFollowTheStep()
{
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, mod->getSatelliteCap());
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, mod->getEnvironmentCap());

    mod->setStep(1);
    TEST_ASSERT_EQUAL_UINT16(pctOfBase(60), mod->getSatelliteCap());
    TEST_ASSERT_EQUAL_UINT16(pctOfBase(60), mod->getEnvironmentCap());

    // At the bottom the bulk stores are cut one step further than the UI-facing ones.
    mod->setStep(NodeDBScalingModule::MAX_STEP);
    TEST_ASSERT_GREATER_THAN_UINT16(mod->getEnvironmentCap(), mod->getSatelliteCap());

    // Whatever the ladder says, the floors hold.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(NodeDBScalingModule::UI_SATELLITE_FLOOR, mod->getSatelliteCap());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(NodeDBScalingModule::BULK_FLOOR, mod->getEnvironmentCap());
}

// A step is only released after a sustained quiet spell, and only once the population has
// fallen clear of the hysteresis band around the step's own entry threshold.
void test_releaseNeedsSustainedQuiet()
{
    mod->setStep(1); // entry threshold 200, release below 150
    const uint16_t quiet = 100;

    for (uint8_t i = 0; i < NodeDBScalingModule::QUIET_HOURS_PER_STEP - 1; i++) {
        mod->evaluate(quiet, /*underPressure=*/false);
        TEST_ASSERT_EQUAL_UINT8(1, mod->getStep());
    }
    mod->evaluate(quiet, false);
    TEST_ASSERT_EQUAL_UINT8(0, mod->getStep());
}

void test_populationInsideHysteresisBandHolds()
{
    mod->setStep(1); // entry 200, release below 150: 175 is inside the band
    for (int i = 0; i < 20; i++)
        mod->evaluate(175, /*underPressure=*/false);
    TEST_ASSERT_EQUAL_UINT8(1, mod->getStep());
}

// One busy hour part-way through a quiet spell resets the count - no half-release.
void test_quietRunIsResetByABusyEvaluation()
{
    mod->setStep(1);
    for (uint8_t i = 0; i < NodeDBScalingModule::QUIET_HOURS_PER_STEP - 1; i++)
        mod->evaluate(100, false);
    mod->evaluate(250, /*underPressure=*/false); // warrants step 1: not a release, resets the run
    TEST_ASSERT_EQUAL_UINT8(1, mod->getStep());

    for (uint8_t i = 0; i < NodeDBScalingModule::QUIET_HOURS_PER_STEP - 1; i++) {
        mod->evaluate(100, false);
        TEST_ASSERT_EQUAL_UINT8(1, mod->getStep());
    }
    mod->evaluate(100, false);
    TEST_ASSERT_EQUAL_UINT8(0, mod->getStep());
}

// A population warranting a deeper step voids the release run even with no pressure signal -
// otherwise quiet hours from either side of a busy spell add up to an undeserved release.
void test_highPopulationWithoutPressureVoidsTheQuietRun()
{
    mod->setStep(1); // entry 200, release below 150
    for (uint8_t i = 0; i < NodeDBScalingModule::QUIET_HOURS_PER_STEP - 1; i++)
        mod->evaluate(100, false);

    mod->evaluate(500, /*underPressure=*/false); // warrants step 2, but nothing is squeezing us
    TEST_ASSERT_EQUAL_UINT8(1, mod->getStep());

    mod->evaluate(100, false); // would have been the sixth quiet hour had the run survived
    TEST_ASSERT_EQUAL_UINT8(1, mod->getStep());

    for (uint8_t i = 1; i < NodeDBScalingModule::QUIET_HOURS_PER_STEP; i++)
        mod->evaluate(100, false);
    TEST_ASSERT_EQUAL_UINT8(0, mod->getStep());
}

void test_ratchetIsSymmetricAcrossAFullCycle()
{
    for (int i = 0; i < NodeDBScalingModule::MAX_STEP; i++)
        mod->evaluate(2000, true);
    TEST_ASSERT_EQUAL_UINT8(NodeDBScalingModule::MAX_STEP, mod->getStep());

    // Mesh goes quiet: every step comes back, one quiet run each.
    for (uint8_t s = NodeDBScalingModule::MAX_STEP; s > 0; s--) {
        for (uint8_t i = 0; i < NodeDBScalingModule::QUIET_HOURS_PER_STEP; i++)
            mod->evaluate(0, false);
    }
    TEST_ASSERT_EQUAL_UINT8(0, mod->getStep());
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, mod->getSatelliteCap());
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, mod->getEnvironmentCap());
}

// ---------------------------------------------------------------------------
// Cap plumbing into NodeDB
// ---------------------------------------------------------------------------

void test_freeAccessorsTrackTheModule()
{
    mod->setStep(0);
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, nodeDBSatelliteCap());
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, nodeDBEnvironmentCap());

    mod->setStep(NodeDBScalingModule::MAX_STEP);
    TEST_ASSERT_EQUAL_UINT16(mod->getSatelliteCap(), nodeDBSatelliteCap());
    TEST_ASSERT_EQUAL_UINT16(mod->getEnvironmentCap(), nodeDBEnvironmentCap());
}

// Stepping down has to reclaim immediately, and has to reclaim the *stalest* entries -
// the freshest nodes are the ones the UI still wants to draw.
void test_stepDownTrimsSatellitesOldestFirst()
{
    db->clearHot();
    // last_heard ascending with node number, so 0x1000 is the stalest.
    for (uint16_t i = 0; i < MAX_SATELLITE_NODES; i++)
        db->pushWithSatellites(0x1000 + i, 1000 + i);

    mod->setStep(0);
    db->enforceSatelliteCaps();
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, db->nodePositions.size());

    mod->setStep(NodeDBScalingModule::MAX_STEP); // calls enforceSatelliteCaps() itself
    const uint16_t satCap = mod->getSatelliteCap();
    const uint16_t envCap = mod->getEnvironmentCap();
    TEST_ASSERT_EQUAL_UINT16(satCap, db->nodePositions.size());
    TEST_ASSERT_EQUAL_UINT16(envCap, db->nodeEnvironment.size());
    TEST_ASSERT_LESS_THAN_UINT16(satCap, envCap); // bulk is cut harder than UI-facing

    // The survivors are the freshest ones.
    for (uint16_t i = 0; i < MAX_SATELLITE_NODES; i++) {
        const bool shouldSurvive = i >= (MAX_SATELLITE_NODES - satCap);
        TEST_ASSERT_EQUAL(shouldSurvive ? 1u : 0u, db->nodePositions.count(0x1000 + i));
    }

    // Ratcheting back up must not resurrect anything - the maps refill from live traffic only.
    mod->setStep(0);
    TEST_ASSERT_EQUAL_UINT16(MAX_SATELLITE_NODES, mod->getSatelliteCap());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)satCap, (uint32_t)db->nodePositions.size());
}

// ---------------------------------------------------------------------------
// Converting freed satellite budget into hot-store slots
// ---------------------------------------------------------------------------

// The funding is priced in worst-case on-disk bytes, so it must rise monotonically as the
// ladder descends and be zero when unpressured.
void test_bonusRisesWithEachStep()
{
    TEST_ASSERT_EQUAL_UINT32(0, NodeDBScalingModule::freedFlashBytes(MAX_SATELLITE_NODES, MAX_SATELLITE_NODES));
    TEST_ASSERT_EQUAL_UINT16(0, NodeDBScalingModule::bonusForCaps(MAX_SATELLITE_NODES, MAX_SATELLITE_NODES));

    uint16_t previous = 0;
    for (uint8_t i = 1; i < NodeDBScalingModule::STEP_COUNT; i++) {
        const uint16_t sat =
            NodeDBScalingModule::capForPct(NodeDBScalingModule::STEPS[i].satellitePct, NodeDBScalingModule::UI_SATELLITE_FLOOR);
        const uint16_t env =
            NodeDBScalingModule::capForPct(NodeDBScalingModule::STEPS[i].bulkPct, NodeDBScalingModule::BULK_FLOOR);
        const uint16_t bonus = NodeDBScalingModule::bonusForCaps(sat, env);
        TEST_ASSERT_GREATER_THAN_UINT16(previous, bonus);
        previous = bonus;
    }
}

// A file we write must stay inside the decode allowance every build already grants, or a peer
// or a downgrade could not read it back.
void test_effectiveMaxNodesNeverExceedsDecodeCeiling()
{
    for (uint8_t i = 0; i < NodeDBScalingModule::STEP_COUNT; i++) {
        mod->setStep(i);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(NODEDB_MIGRATION_LOAD_CEILING, (uint32_t)db->effectiveMaxNodes());
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32((uint32_t)MAX_NUM_NODES, (uint32_t)db->effectiveMaxNodes());
    }
}

// THE invariant: extra capacity is filled passively. The introduce-yourself handshake gate must
// stay pinned to the baseline even while the store is allowed to grow past it.
void test_passiveFillGateDoesNotFollowTheBonus()
{
    mod->setStep(NodeDBScalingModule::MAX_STEP);
    db->applyHotStoreCapacity();

    // Below baseline: still actively introducing ourselves.
    db->fillHot(NODEDB_BASELINE_NODES - 1);
    TEST_ASSERT_FALSE(db->isPassiveFillOnly());

    // At baseline: gate closes, even though the store has room to keep growing.
    db->fillHot(NODEDB_BASELINE_NODES);
    TEST_ASSERT_TRUE(db->isPassiveFillOnly());
    if (db->effectiveMaxNodes() > NODEDB_BASELINE_NODES)
        TEST_ASSERT_FALSE(db->isFull()); // room remains, but only for passively-learned nodes
}

void test_capacityGrowsAndIsHandedBack()
{
    mod->setStep(0);
    db->applyHotStoreCapacity();
    const size_t base = db->meshNodes->size();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)db->effectiveMaxNodes(), (uint32_t)base);

    mod->setStep(NodeDBScalingModule::MAX_STEP);
    db->applyHotStoreCapacity();
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32((uint32_t)base, (uint32_t)db->meshNodes->size());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)db->effectiveMaxNodes(), (uint32_t)db->meshNodes->size());

    // Mesh quietens: the slots go back, and the store is resized to match.
    mod->setStep(0);
    db->applyHotStoreCapacity();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)base, (uint32_t)db->meshNodes->size());
    TEST_ASSERT_LESS_OR_EQUAL_UINT32((uint32_t)base, (uint32_t)db->numMeshNodes);
}

// Protected-node admission is pinned to the BASELINE cap, never the ratcheted one: capacity is
// handed back, and a round trip through the warm tier does not restore favorite/ignore/verified.
void test_protectedCapIgnoresTheRatchetedCapacity()
{
    mod->setStep(NodeDBScalingModule::MAX_STEP);
    db->applyHotStoreCapacity();

    db->clearHot();
    const pb_size_t grown = db->effectiveMaxNodes();
    for (pb_size_t i = 0; i < grown; i++)
        db->pushWithSatellites(0x3000 + i, 1000 + i);

    int admitted = 0;
    for (pb_size_t i = 0; i < grown; i++)
        if (db->setProtectedFlag(&db->meshNodes->at(i), NODEINFO_BITFIELD_IS_FAVORITE_MASK, true))
            admitted++;
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES - 2, admitted);
    TEST_ASSERT_EQUAL_INT(admitted, db->numProtectedNodes());

    // Hand the capacity back: the overflow demotes, but no favorite is among it.
    mod->setStep(0);
    db->applyHotStoreCapacity();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)MAX_NUM_NODES, (uint32_t)db->effectiveMaxNodes());
    TEST_ASSERT_EQUAL_INT(admitted, db->numProtectedNodes());
}

// Accepted trade, guarded so it cannot drift: dedup history is sized from the BASELINE hot
// store, never from the ratcheted cap. Wiring it to the effective cap would spend the heap the
// growth guard exists to protect, and would silently blow MESHTASTIC_BOOT_CACHE_BUDGET.
void test_dedupHistoryStaysPinnedToBaseline()
{
    const uint32_t fromBaseline = ((uint32_t)MAX_NUM_NODES * 2 > 100) ? (uint32_t)MAX_NUM_NODES * 2 : 100u;
    TEST_ASSERT_EQUAL_UINT32(fromBaseline, (uint32_t)PACKETHISTORY_MAX);

    // With the ratchet fully engaged the cap grows, but the history does not follow it - so the
    // records-per-slot ratio is expected to fall below the nominal 2x.
    mod->setStep(NodeDBScalingModule::MAX_STEP);
    db->applyHotStoreCapacity();
    TEST_ASSERT_EQUAL_UINT32(fromBaseline, (uint32_t)PACKETHISTORY_MAX);
    if (db->effectiveMaxNodes() > MAX_NUM_NODES)
        TEST_ASSERT_LESS_THAN_UINT32((uint32_t)db->effectiveMaxNodes() * 2u, (uint32_t)PACKETHISTORY_MAX);
}

} // namespace

// Every test starts from an unpressured ladder; setStep() also clears the hysteresis run.
void setUp(void)
{
    if (mod)
        mod->setStep(0);
}
void tearDown(void) {}

NDB_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    db = new NodeDBTestShim();
    nodeDB = db;
    mod = new NodeDBScalingModule();
    nodeDBScalingModule = mod;

    UNITY_BEGIN();

    RUN_TEST(test_stepForPopulation_walksTheLadder);
    RUN_TEST(test_ladderPercentagesMatchThePublishedSteps);
    RUN_TEST(test_capForPct_appliesFloorAndCeiling);

    RUN_TEST(test_descendsOneStepPerEvaluation);
    RUN_TEST(test_populationWithoutPressureDoesNotDescend);
    RUN_TEST(test_capsFollowTheStep);
    RUN_TEST(test_releaseNeedsSustainedQuiet);
    RUN_TEST(test_populationInsideHysteresisBandHolds);
    RUN_TEST(test_quietRunIsResetByABusyEvaluation);
    RUN_TEST(test_highPopulationWithoutPressureVoidsTheQuietRun);
    RUN_TEST(test_ratchetIsSymmetricAcrossAFullCycle);

    RUN_TEST(test_freeAccessorsTrackTheModule);
    RUN_TEST(test_stepDownTrimsSatellitesOldestFirst);

    RUN_TEST(test_bonusRisesWithEachStep);
    RUN_TEST(test_effectiveMaxNodesNeverExceedsDecodeCeiling);
    RUN_TEST(test_passiveFillGateDoesNotFollowTheBonus);
    RUN_TEST(test_capacityGrowsAndIsHandedBack);
    RUN_TEST(test_protectedCapIgnoresTheRatchetedCapacity);
    RUN_TEST(test_dedupHistoryStaysPinnedToBaseline);

    exit(UNITY_END());
}

NDB_TEST_ENTRY void loop() {}

#else // !HAS_NODEDB_SCALING

void setUp(void) {}
void tearDown(void) {}
NDB_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
NDB_TEST_ENTRY void loop() {}

#endif // HAS_NODEDB_SCALING
