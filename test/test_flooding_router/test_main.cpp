// FloodingRouter::perhapsCancelDupe in src/mesh/FloodingRouter.cpp: role gating, the TX-queue check,
// and its call into RepeatScalingModule; a stub radio says whether our copy is queued and counts cancels.
//
// Why: only a LoRa duplicate of a packet we still hold queued may cancel it, ROUTER never cancels,
// and RepeatScalingModule alone decides when. Regression guarded: the router ignoring the module's
// answer (cancelling on every duplicate, or never), or spending module tracker slots on duplicates
// that have nothing left to cancel.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "MeshRadio.h"
#include "configuration.h"
#include "mesh/FloodingRouter.h"
#include "mesh/RadioInterface.h"
#include "modules/RepeatScalingModule.h"

static constexpr NodeNum kSender1 = 0x000005AB;
static constexpr PacketId kId1 = 0x1001;

// Re-exposes perhapsCancelDupe; nulls cryptLock so the Router base can be rebuilt.
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

// Reports whether our copy of the duplicate's packet is still in the TX queue.
class QueueStubRadio : public RadioInterface
{
  public:
    bool queued = true;
    int cancelCalls = 0;
    bool cancelSending(NodeNum, PacketId) override
    {
        cancelCalls++;
        return true;
    }
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    uint32_t getPacketTime(uint32_t, bool) override { return 0; }
    bool findInTxQueue(NodeNum, PacketId) override { return queued; }
};

static FloodingRouterTestShim *shim = nullptr;
static QueueStubRadio *radio = nullptr;
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
    radio->queued = true;
    radio->cancelCalls = 0;
}

void tearDown(void) {}

// perhapsCancelDupe: role-gating and delegation to RepeatScalingModule

void test_perhapsCancelDupe_consults_repeatScalingModule_for_client_role(void)
{
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    shim->perhapsCancelDupe(&p);
    TEST_ASSERT_EQUAL_INT(1, counting->callCount);
    TEST_ASSERT_EQUAL_INT(0, radio->cancelCalls); // the module said keep it, so nothing is cancelled
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
    // The module's yes must reach the radio as exactly one cancel of our queued copy.
    counting->decision = true;
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->perhapsCancelDupe(&p);

    TEST_ASSERT_EQUAL_INT(1, counting->callCount);
    TEST_ASSERT_EQUAL_INT(1, radio->cancelCalls);
}

// A duplicate of a packet we have not queued (or already sent) has nothing to cancel, so it must not
// reach the module and take one of its tracker slots.
void test_perhapsCancelDupe_skips_repeatScalingModule_when_nothing_is_queued(void)
{
    radio->queued = false;
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->perhapsCancelDupe(&p);

    TEST_ASSERT_EQUAL_INT(0, counting->callCount);
}

// Router::findInTxQueue must be safe with no radio attached, as cancelSending already is.
void test_findInTxQueue_without_a_radio_is_false(void)
{
    shim->addInterface(nullptr); // frees the stub radio
    const bool found = shim->findInTxQueue(kSender1, kId1);

    // Rebuild before asserting: a failed assert longjmps out and setUp() would write through the freed radio
    radio = new QueueStubRadio();
    shim->addInterface(std::unique_ptr<RadioInterface>(radio));
    TEST_ASSERT_FALSE(found);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    initRegion(); // a RadioInterface reads myRegion when it is constructed
    shim = new FloodingRouterTestShim();
    radio = new QueueStubRadio();
    shim->addInterface(std::unique_ptr<RadioInterface>(radio));
    counting = new CountingRepeatScalingModule();

    printf("\n=== perhapsCancelDupe (role-gating + delegation) ===\n");
    RUN_TEST(test_perhapsCancelDupe_consults_repeatScalingModule_for_client_role);
    RUN_TEST(test_perhapsCancelDupe_router_role_never_consults_repeatScalingModule);
    RUN_TEST(test_perhapsCancelDupe_non_lora_transport_never_consults_repeatScalingModule);
    RUN_TEST(test_perhapsCancelDupe_does_not_crash_when_repeatScalingModule_says_cancel);
    RUN_TEST(test_perhapsCancelDupe_skips_repeatScalingModule_when_nothing_is_queued);
    RUN_TEST(test_findInTxQueue_without_a_radio_is_false);

    exit(UNITY_END());
}

void loop() {}
