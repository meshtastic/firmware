// Unit tests for FloodingRouter::perhapsCancelDupe's role-gating and its delegation to
// RepeatScalingModule (the "how many duplicates should we tolerate before giving up" decision
// and its bookkeeping/logging now lives there - see test/test_repeat_scaling_module).
//
// perhapsCancelDupe() is safe to call directly here even with no RadioInterface: Router::
// cancelSending() is iface-null-safe (just returns false), so the queue-cancellation side effect
// is a no-op - what we can and do observe is whether/how many times RepeatScalingModule was
// consulted at all.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "configuration.h"
#include "mesh/FloodingRouter.h"
#include "modules/RepeatScalingModule.h"

static constexpr NodeNum kSender1 = 0x000005AB;
static constexpr PacketId kId1 = 0x1001;

// ---------------------------------------------------------------------------
// Test shim - re-exposes the protected perhapsCancelDupe. Nulls cryptLock before it's rebuilt so
// the Router base can be (re)constructed (same pattern as test_nexthop_routing's
// NextHopRouterTestShim / test_mqtt's MockRouter).
// ---------------------------------------------------------------------------
class FloodingRouterTestShim : public FloodingRouter
{
  public:
    FloodingRouterTestShim() : FloodingRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }

    using FloodingRouter::perhapsCancelDupe;

  protected:
    // FloodingRouter::perhapsRebroadcast is pure virtual; not exercised by these tests.
    bool perhapsRebroadcast(const meshtastic_MeshPacket *p) override { return false; }
};

// Test double standing in for the real RepeatScalingModule: records how many times
// FloodingRouter consulted it, and returns a fixed, test-controlled decision.
class CountingRepeatScalingModule : public RepeatScalingModule
{
  public:
    int callCount = 0;
    bool decision = false;
    bool shouldCancelDupe(const meshtastic_MeshPacket *p) override
    {
        callCount++;
        return decision;
    }
};

static FloodingRouterTestShim *shim = nullptr;
static CountingRepeatScalingModule *counting = nullptr;

static meshtastic_MeshPacket makeDupePacket(NodeNum from, PacketId id)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.id = id;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    return p;
}

void setUp(void)
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    counting->callCount = 0;
    counting->decision = false;
    repeatScalingModule = counting;
}

void tearDown(void) {}

// ===========================================================================
// perhapsCancelDupe: role-gating and delegation to RepeatScalingModule
// ===========================================================================

void test_perhapsCancelDupe_consults_repeatScalingModule_for_client_role(void)
{
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    shim->perhapsCancelDupe(&p);
    TEST_ASSERT_EQUAL_INT(1, counting->callCount);
}

void test_perhapsCancelDupe_router_role_never_consults_repeatScalingModule(void)
{
    // ROUTER never cancels its own rebroadcast (roleAllowsCancelingDupe() gates it out entirely),
    // so it should also never bother asking RepeatScalingModule about it.
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->perhapsCancelDupe(&p);
    shim->perhapsCancelDupe(&p);

    TEST_ASSERT_EQUAL_INT(0, counting->callCount);
}

void test_perhapsCancelDupe_non_lora_transport_never_consults_repeatScalingModule(void)
{
    // Only LoRa-transport packets can trigger a cancel; other transports should never be routed
    // through RepeatScalingModule.
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL;

    shim->perhapsCancelDupe(&p);

    TEST_ASSERT_EQUAL_INT(0, counting->callCount);
}

void test_perhapsCancelDupe_does_not_crash_when_repeatScalingModule_says_cancel(void)
{
    // With no RadioInterface, Router::cancelSending() is a safe no-op - this just confirms the
    // true-branch (attempting the cancel) doesn't crash and still only consults the module once.
    counting->decision = true;
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->perhapsCancelDupe(&p);

    TEST_ASSERT_EQUAL_INT(1, counting->callCount);
}

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    shim = new FloodingRouterTestShim();
    counting = new CountingRepeatScalingModule();

    printf("\n=== perhapsCancelDupe (role-gating + delegation) ===\n");
    RUN_TEST(test_perhapsCancelDupe_consults_repeatScalingModule_for_client_role);
    RUN_TEST(test_perhapsCancelDupe_router_role_never_consults_repeatScalingModule);
    RUN_TEST(test_perhapsCancelDupe_non_lora_transport_never_consults_repeatScalingModule);
    RUN_TEST(test_perhapsCancelDupe_does_not_crash_when_repeatScalingModule_says_cancel);

    exit(UNITY_END());
}

void loop() {}
