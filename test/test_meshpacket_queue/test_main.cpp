// Unit tests for MeshPacketQueue::replaceLowerPriorityPacket()'s late-packet branch - the one that
// evicts an overdue packet from a full queue to make room for a new arrival.
//
// tx_after is an absolute millis() deadline, so every decision here has to subtract before comparing
// or it inverts across the 32-bit wrap. The subtlety the cases below pin is that an *elapsed* time
// only orders two deadlines that have both passed: a deadline still in the future subtracts to a
// near-2^32 elapsed, which reads as the most overdue packet in the queue rather than the least.
//
// maxLen is 1 throughout. That is enough to reach the branch (any enqueue into a full queue goes
// through it) and it keeps CompareMeshPacketFunc out of the picture - std::upper_bound over an
// empty range never invokes the comparator, so the suite needs no NodeDB.

#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "configuration.h"
#include "mesh/MeshPacketQueue.h"
#include "mesh/MeshTypes.h"
#include <cstdlib>
#include <unity.h>

namespace
{

// A packet that is only ever a queue occupant: id, tx_after, and priority are what the queue reads.
// Leaving `p->from = 0` makes `isFromUs(p)` short-circuit on `p->from == 0` without a NodeDB instance.
meshtastic_MeshPacket *makePacket(uint32_t id, uint32_t txAfter,
                                  meshtastic_MeshPacket_Priority pri = meshtastic_MeshPacket_Priority_DEFAULT)
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    TEST_ASSERT_NOT_NULL(p);
    p->id = id;
    p->tx_after = txAfter;
    p->priority = pri;
    return p;
}

uint32_t popId(MeshPacketQueue &q)
{
    meshtastic_MeshPacket *p = q.dequeue();
    TEST_ASSERT_NOT_NULL(p);
    uint32_t id = p->id;
    packetPool.release(p);
    return id;
}

// Drains whatever is still queued back to the pool, so a failing case cannot starve a later one.
void drain(MeshPacketQueue &q)
{
    while (meshtastic_MeshPacket *p = q.dequeue())
        packetPool.release(p);
}

} // namespace

void setUp(void)
{
    Time::setTestMillis(0);
}
void tearDown(void)
{
    Time::useRealClock();
}

// The regression: the incoming packet is not due yet, so it must not displace an overdue one.
// `now - p->tx_after` underflows to ~49.7 days of "elapsed", which an unguarded comparison reads as
// the more urgent packet.
static void test_future_incoming_deadline_does_not_evict_an_overdue_packet(void)
{
    Time::setTestMillis(1000);
    MeshPacketQueue q(1);

    meshtastic_MeshPacket *back = makePacket(0x1001, 900);   // 100ms overdue
    meshtastic_MeshPacket *fresh = makePacket(0x1002, 1100); // 100ms in the future
    TEST_ASSERT_TRUE(q.enqueue(back));

    TEST_ASSERT_FALSE(q.enqueue(fresh));
    TEST_ASSERT_EQUAL_HEX32(0x1001, q.getFront()->id);

    packetPool.release(fresh);
    drain(q);
}

// The ordering the branch does want: both deadlines have passed and the arrival is the more overdue
// of the two, so the queued packet gives up its slot.
static void test_more_overdue_incoming_packet_evicts_the_late_back_packet(void)
{
    Time::setTestMillis(1000);
    MeshPacketQueue q(1);

    meshtastic_MeshPacket *back = makePacket(0x2001, 900);  // 100ms overdue
    meshtastic_MeshPacket *fresh = makePacket(0x2002, 800); // 200ms overdue
    TEST_ASSERT_TRUE(q.enqueue(back));

    TEST_ASSERT_TRUE(q.enqueue(fresh)); // back is released by the queue
    TEST_ASSERT_EQUAL_HEX32(0x2002, q.getFront()->id);

    drain(q);
}

// The other half of that ordering: a less overdue arrival leaves the queue alone.
static void test_less_overdue_incoming_packet_is_rejected(void)
{
    Time::setTestMillis(1000);
    MeshPacketQueue q(1);

    meshtastic_MeshPacket *back = makePacket(0x3001, 800);  // 200ms overdue
    meshtastic_MeshPacket *fresh = makePacket(0x3002, 900); // 100ms overdue
    TEST_ASSERT_TRUE(q.enqueue(back));

    TEST_ASSERT_FALSE(q.enqueue(fresh));
    TEST_ASSERT_EQUAL_HEX32(0x3001, q.getFront()->id);

    packetPool.release(fresh);
    drain(q);
}

// An arrival with no TX delay at all always wins the slot from an overdue packet.
static void test_undelayed_incoming_packet_evicts_the_late_back_packet(void)
{
    Time::setTestMillis(1000);
    MeshPacketQueue q(1);

    meshtastic_MeshPacket *back = makePacket(0x4001, 900);
    meshtastic_MeshPacket *fresh = makePacket(0x4002, 0); // no tx_after
    TEST_ASSERT_TRUE(q.enqueue(back));

    TEST_ASSERT_TRUE(q.enqueue(fresh));
    TEST_ASSERT_EQUAL_HEX32(0x4002, q.getFront()->id);

    drain(q);
}

// Both deadlines were set before the wrap and `now` is after it, so every raw comparison in the
// branch inverts. The decisions must come out the same as they do away from the boundary.
static void test_decisions_survive_the_millis_wrap(void)
{
    // 0xFFFFFF00 and 0xFFFFFE00 are 256ms and 512ms before the wrap; now is 256ms after it.
    Time::setTestMillis(0x00000100);
    MeshPacketQueue q(1);

    meshtastic_MeshPacket *back = makePacket(0x5001, 0xFFFFFF00);  // 512ms overdue
    meshtastic_MeshPacket *older = makePacket(0x5002, 0xFFFFFE00); // 768ms overdue
    TEST_ASSERT_TRUE(q.enqueue(back));
    TEST_ASSERT_TRUE(q.enqueue(older));
    TEST_ASSERT_EQUAL_HEX32(0x5002, q.getFront()->id);
    drain(q);

    // ...and a not-yet-due arrival still loses, with the deadline on the far side of the wrap.
    MeshPacketQueue q2(1);
    meshtastic_MeshPacket *back2 = makePacket(0x5003, 0xFFFFFF00); // 512ms overdue
    meshtastic_MeshPacket *fresh = makePacket(0x5004, 0x00000300); // 512ms in the future
    TEST_ASSERT_TRUE(q2.enqueue(back2));

    TEST_ASSERT_FALSE(q2.enqueue(fresh));
    TEST_ASSERT_EQUAL_HEX32(0x5003, q2.getFront()->id);

    packetPool.release(fresh);
    drain(q2);
}

// When std::upper_bound inserts a freshly clamped DEFAULT late packet at queue.back() with a future
// tx_after deadline, older late packets ahead of it in the queue may already be heavily overdue.
// An arriving normal packet must evict the most-overdue late packet rather than being blocked by
// the future deadline sitting at queue.back().
static void test_evicts_most_overdue_late_packet_even_when_back_packet_is_future(void)
{
    Time::setTestMillis(10000);
    MeshPacketQueue q(3);

    meshtastic_MeshPacket *oldestLate = makePacket(0x6001, 2000);  // 8000ms overdue
    meshtastic_MeshPacket *midLate = makePacket(0x6002, 5000);     // 5000ms overdue
    meshtastic_MeshPacket *newestLate = makePacket(0x6003, 14000); // 4000ms in the future (at queue.back())
    TEST_ASSERT_TRUE(q.enqueue(oldestLate));
    TEST_ASSERT_TRUE(q.enqueue(midLate));
    TEST_ASSERT_TRUE(q.enqueue(newestLate));

    meshtastic_MeshPacket *freshNormal = makePacket(0x6004, 0);
    TEST_ASSERT_TRUE(q.enqueue(freshNormal));

    // freshNormal (tx_after == 0) sorts to the front; oldestLate (0x6001) was evicted.
    TEST_ASSERT_EQUAL_HEX32(0x6004, popId(q));
    TEST_ASSERT_EQUAL_HEX32(0x6002, popId(q));
    TEST_ASSERT_EQUAL_HEX32(0x6003, popId(q));
    drain(q);
}

// When all late packets in a full queue still have future tx_after deadlines, an arriving
// higher-priority packet (e.g. ACK or HIGH text message) must still evict the lowest-priority
// late packet at queue.back() rather than being dropped.
static void test_high_priority_packet_evicts_future_default_late_packet(void)
{
    Time::setTestMillis(1000);
    MeshPacketQueue q(2);

    TEST_ASSERT_TRUE(q.enqueue(makePacket(0x7001, 3000, meshtastic_MeshPacket_Priority_DEFAULT)));
    TEST_ASSERT_TRUE(q.enqueue(makePacket(0x7002, 4000, meshtastic_MeshPacket_Priority_DEFAULT)));

    meshtastic_MeshPacket *ack = makePacket(0x7003, 0, meshtastic_MeshPacket_Priority_ACK);
    TEST_ASSERT_TRUE(q.enqueue(ack));

    TEST_ASSERT_EQUAL_HEX32(0x7003, popId(q));
    TEST_ASSERT_EQUAL_HEX32(0x7001, popId(q));
    drain(q);
}

// An overdue late packet (already rebroadcast by another node on the mesh) must be evicted
// before an un-relayed normal packet (`tx_after == 0`) of lower priority than the new arrival.
static void test_overdue_late_packet_evicted_before_lower_priority_normal_packet(void)
{
    Time::setTestMillis(5000);
    MeshPacketQueue q(2);

    meshtastic_MeshPacket *unrelayedNormal = makePacket(0x8001, 0, meshtastic_MeshPacket_Priority_DEFAULT);
    meshtastic_MeshPacket *overdueLate = makePacket(0x8002, 2000, meshtastic_MeshPacket_Priority_DEFAULT);
    TEST_ASSERT_TRUE(q.enqueue(unrelayedNormal));
    TEST_ASSERT_TRUE(q.enqueue(overdueLate));

    meshtastic_MeshPacket *highPri = makePacket(0x8003, 0, meshtastic_MeshPacket_Priority_HIGH);
    TEST_ASSERT_TRUE(q.enqueue(highPri));

    // highPri (0x8003) and unrelayedNormal (0x8001) remain; overdueLate (0x8002) was evicted.
    TEST_ASSERT_EQUAL_HEX32(0x8003, popId(q));
    TEST_ASSERT_EQUAL_HEX32(0x8001, popId(q));
    drain(q);
}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_future_incoming_deadline_does_not_evict_an_overdue_packet);
    RUN_TEST(test_more_overdue_incoming_packet_evicts_the_late_back_packet);
    RUN_TEST(test_less_overdue_incoming_packet_is_rejected);
    RUN_TEST(test_undelayed_incoming_packet_evicts_the_late_back_packet);
    RUN_TEST(test_decisions_survive_the_millis_wrap);
    RUN_TEST(test_evicts_most_overdue_late_packet_even_when_back_packet_is_future);
    RUN_TEST(test_high_priority_packet_evicts_future_default_late_packet);
    RUN_TEST(test_overdue_late_packet_evicted_before_lower_priority_normal_packet);
    exit(UNITY_END());
}

void loop() {}
