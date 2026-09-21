// Unit tests for DMShellRxWindow and DMShellTxHistoryWindow in src/modules/DMShellRecovery.h - the
// sequence tracking and replay bookkeeping behind DMShell's retransmission scheme.
//
// Two contracts are pinned here, and both exist because of an observed failure: a DMShell session on
// a fast modem preset (ShortTurbo) degenerated into a retransmission storm that never terminated.
//
// 1. A replay request is damped per sequence number. Without damping, every frame arriving above a
//    gap produces its own request, so one lost frame costs as many requests as the sender has frames
//    in flight, and each of those costs the sender a duplicate replay - a positive feedback loop on
//    an already contended channel. The damping is keyed on the sequence number, not on time alone,
//    so a *different* gap is always reported immediately; delaying that would add a round trip to
//    every recovery.
//
// 2. A request for a frame that has aged out of the send ring is distinguishable from one that can
//    still be answered. The ring is bounded in frames (50), so the wall-clock span it covers scales
//    with the preset: about two minutes on LongFast but under seven seconds on ShortTurbo. Once a
//    requested frame is gone the peer can never receive it, and neither side advances past the hole,
//    so the request repeats until the session idles out five minutes later. DMShellModule relies on
//    the Evicted verdict to close the session instead.
//
// 3. The sender will not transmit more than a fixed number of frames past the peer's acknowledgements.
//    This is what makes contracts 1 and 2 stop mattering: measured on hardware, whichever end has the
//    higher frame rate outruns its own replay ring first, so a bigger ring on one side just moves the
//    failure to the other. A sender that cannot get more than a window ahead of a gap can always
//    answer a replay request. The window also bounds the send path in wall-clock terms, which is what
//    lets an idle timeout detect a peer that has vanished - a blocked sender goes quiet and times out,
//    where an unbounded one transmitted to nobody for as long as its command kept producing output.
//
// The regression guarded: if these assertions are deleted or relaxed, the storm comes back, and it
// comes back invisibly - the failing session looks like a flaky radio link rather than a protocol
// bug, which is exactly how it was originally misdiagnosed.
//
// Time is passed in rather than read from millis() so the damping boundaries can be asserted
// exactly, including across the millis() wrap.
#include "TestUtil.h"
#include "modules/DMShellRecovery.h"
#include <cstdint>
#include <unity.h>

namespace
{
// The OPEN sequence number every case seeds from, so the first data frame expected is 2.
constexpr uint32_t kOpenSeq = 1;
constexpr uint32_t kInterval = 1000;

/// Unity's equality assertions take integers, and DMShellReplayLookup is a scoped enum, so the
/// comparison has to be made explicitly. A macro rather than a function so a failure still reports
/// the caller's line.
#define ASSERT_LOOKUP(expected, actual) TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

DMShellRxWindow makeWindow()
{
    DMShellRxWindow w;
    w.reset(kOpenSeq);
    return w;
}
} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_dmshell_rx_seq_zero_always_processed()
{
    DMShellRxWindow w = makeWindow();

    const DMShellRxDecision d = w.classify(0, 0, kInterval);

    TEST_ASSERT_TRUE(d.process);
    TEST_ASSERT_FALSE(d.requestReplay);
}

void test_dmshell_rx_in_order_run_requests_nothing()
{
    DMShellRxWindow w = makeWindow();

    for (uint32_t seq = 2; seq <= 6; seq++) {
        const DMShellRxDecision d = w.classify(seq, seq * 10, kInterval);
        TEST_ASSERT_TRUE(d.process);
        TEST_ASSERT_FALSE(d.requestReplay);
    }

    TEST_ASSERT_EQUAL_UINT32(6, w.lastInOrder());
    TEST_ASSERT_EQUAL_UINT32(7, w.nextExpected());
}

void test_dmshell_rx_gap_requests_once_then_damps()
{
    DMShellRxWindow w = makeWindow();

    // Frame 2 is lost; 3 arrives and opens the gap.
    const DMShellRxDecision first = w.classify(3, 0, kInterval);
    TEST_ASSERT_FALSE(first.process);
    TEST_ASSERT_TRUE(first.requestReplay);
    TEST_ASSERT_EQUAL_UINT32(2, first.replaySeq);

    // Everything the sender had in flight arrives next. Before the damping each of these produced
    // its own request for seq 2, and each one cost the sender a duplicate replay.
    for (uint32_t seq = 4; seq <= 8; seq++) {
        const DMShellRxDecision d = w.classify(seq, 100, kInterval);
        TEST_ASSERT_FALSE(d.process);
        TEST_ASSERT_FALSE(d.requestReplay);
    }
}

void test_dmshell_rx_gap_reasks_once_the_interval_elapses()
{
    DMShellRxWindow w = makeWindow();
    TEST_ASSERT_TRUE(w.classify(3, 0, kInterval).requestReplay);

    // One millisecond short of the interval is still damped; the boundary itself is not.
    TEST_ASSERT_FALSE(w.classify(4, kInterval - 1, kInterval).requestReplay);

    const DMShellRxDecision again = w.classify(5, kInterval, kInterval);
    TEST_ASSERT_TRUE(again.requestReplay);
    TEST_ASSERT_EQUAL_UINT32(2, again.replaySeq);
}

void test_dmshell_rx_a_different_gap_is_not_damped()
{
    DMShellRxWindow w = makeWindow();
    TEST_ASSERT_TRUE(w.classify(3, 0, kInterval).requestReplay); // asks for 2

    // The replay of 2 arrives well inside the damping interval and fills that hole, but 3 was
    // dropped on arrival so it is now the missing one. That must be reported at once: making the
    // caller wait out an interval keyed to a sequence number it has already recovered would add a
    // round trip to every single recovery.
    const DMShellRxDecision filled = w.classify(2, 10, kInterval);
    TEST_ASSERT_TRUE(filled.process);
    TEST_ASSERT_TRUE(filled.requestReplay);
    TEST_ASSERT_EQUAL_UINT32(3, filled.replaySeq);
}

void test_dmshell_rx_duplicate_with_no_gap_requests_nothing()
{
    DMShellRxWindow w = makeWindow();
    TEST_ASSERT_TRUE(w.classify(2, 0, kInterval).process);

    // A stale duplicate when we are fully caught up. The predecessor of this code answered it with
    // an unconditional acknowledgement whose replay field underflowed to 0xffffffff, which the peer
    // read as a request to replay a sequence number it had never sent.
    const DMShellRxDecision dup = w.classify(2, 10, kInterval);
    TEST_ASSERT_FALSE(dup.process);
    TEST_ASSERT_FALSE(dup.requestReplay);
}

void test_dmshell_rx_zero_interval_disables_damping()
{
    DMShellRxWindow w = makeWindow();

    // DMSHELL_LEGACY_RECOVERY passes 0 to reproduce the pre-fix behaviour on the same build, so the
    // A/B comparison does not depend on reflashing. Every frame above the gap must ask again.
    TEST_ASSERT_TRUE(w.classify(3, 0, 0).requestReplay);
    for (uint32_t seq = 4; seq <= 8; seq++) {
        TEST_ASSERT_TRUE(w.classify(seq, 0, 0).requestReplay);
    }
}

void test_dmshell_rx_damping_survives_millis_wrap()
{
    DMShellRxWindow w = makeWindow();

    // Ask just before the wrap, so the deadline lands just after it.
    const uint32_t nearWrap = 0xffffff00u;
    TEST_ASSERT_TRUE(w.classify(3, nearWrap, kInterval).requestReplay);

    // Still inside the interval, but now() has not wrapped yet: a naive comparison reads the wrapped
    // deadline as long past and un-damps every request from here to the wrap.
    TEST_ASSERT_FALSE(w.classify(4, 0xfffffff0u, kInterval).requestReplay);

    // Past the wrapped deadline (0xffffff00 + 1000 == 0x2e8).
    TEST_ASSERT_TRUE(w.classify(5, 0x300u, kInterval).requestReplay);
}

void test_dmshell_tx_history_finds_retained_sequence()
{
    DMShellTxHistoryWindow h(4);
    for (uint32_t seq = 1; seq <= 4; seq++) {
        h.noteStored(seq);
    }

    ASSERT_LOOKUP(DMShellReplayLookup::Found, h.classify(1, 5));
    ASSERT_LOOKUP(DMShellReplayLookup::Found, h.classify(4, 5));
}

void test_dmshell_tx_history_reports_evicted_sequence()
{
    DMShellTxHistoryWindow h(4);
    for (uint32_t seq = 1; seq <= 5; seq++) {
        h.noteStored(seq);
    }

    // Storing 5 in a ring of 4 displaced 1. Reporting that as merely "not found" is what let the
    // peer retry an unanswerable request until the session idled out.
    ASSERT_LOOKUP(DMShellReplayLookup::Evicted, h.classify(1, 6));
    ASSERT_LOOKUP(DMShellReplayLookup::Found, h.classify(2, 6));
    ASSERT_LOOKUP(DMShellReplayLookup::Found, h.classify(5, 6));
}

void test_dmshell_tx_history_evicts_with_capacity_one()
{
    DMShellTxHistoryWindow h(1);
    h.noteStored(1);
    ASSERT_LOOKUP(DMShellReplayLookup::Found, h.classify(1, 2));

    h.noteStored(2);
    ASSERT_LOOKUP(DMShellReplayLookup::Evicted, h.classify(1, 3));
    ASSERT_LOOKUP(DMShellReplayLookup::Found, h.classify(2, 3));
}

void test_dmshell_tx_history_ignores_unsent_sequence()
{
    DMShellTxHistoryWindow h(4);
    h.noteStored(1);
    h.noteStored(2);

    // A peer asking for something at or past our next sequence number is confused, not owed data.
    // Closing the session on that would hand any malformed request a way to kill the shell.
    ASSERT_LOOKUP(DMShellReplayLookup::NotSentYet, h.classify(3, 3));
    ASSERT_LOOKUP(DMShellReplayLookup::NotSentYet, h.classify(99, 3));
    ASSERT_LOOKUP(DMShellReplayLookup::NotSentYet, h.classify(0, 3));
}

void test_dmshell_tx_history_empty_ring_is_not_eviction()
{
    DMShellTxHistoryWindow h(4);

    // Nothing stored: frames went out without being remembered (a replay passes remember=false).
    // The safe answer is to ignore the request, not to tear the session down.
    ASSERT_LOOKUP(DMShellReplayLookup::NotSentYet, h.classify(1, 5));
}

void test_dmshell_tx_history_reset_forgets_everything()
{
    DMShellTxHistoryWindow h(4);
    for (uint32_t seq = 1; seq <= 6; seq++) {
        h.noteStored(seq);
    }
    ASSERT_LOOKUP(DMShellReplayLookup::Evicted, h.classify(1, 7));

    // A new session restarts sequence numbering at 1, so stale retention bounds would report the
    // new session's first frames as evicted.
    h.reset();
    h.noteStored(1);
    ASSERT_LOOKUP(DMShellReplayLookup::Found, h.classify(1, 2));
}

void test_dmshell_tx_window_unbounded_when_capacity_zero()
{
    DMShellTxWindow w;
    w.reset(0);

    // DMSHELL_TX_WINDOW=0 and legacy mode both reproduce the pre-window behaviour on one build, so
    // the bound can be measured with and without it without reflashing.
    for (uint32_t seq = 1; seq <= 500; seq++) {
        w.noteSent(seq);
        TEST_ASSERT_TRUE(w.canSend());
    }
    TEST_ASSERT_EQUAL_UINT32(500, w.outstanding());
}

void test_dmshell_tx_window_closes_at_capacity()
{
    DMShellTxWindow w;
    w.reset(2);

    w.noteSent(1);
    TEST_ASSERT_TRUE(w.canSend());
    w.noteSent(2);
    TEST_ASSERT_FALSE(w.canSend()); // two outstanding, nothing acknowledged
    TEST_ASSERT_EQUAL_UINT32(2, w.outstanding());
}

void test_dmshell_tx_window_reopens_on_ack()
{
    DMShellTxWindow w;
    w.reset(2);
    w.noteSent(1);
    w.noteSent(2);
    TEST_ASSERT_FALSE(w.canSend());

    // A cumulative cursor, so one acknowledgement can free several slots.
    w.notePeerAcked(1);
    TEST_ASSERT_TRUE(w.canSend());
    TEST_ASSERT_EQUAL_UINT32(1, w.outstanding());

    w.notePeerAcked(2);
    TEST_ASSERT_EQUAL_UINT32(0, w.outstanding());
}

void test_dmshell_tx_window_ignores_stale_and_overreaching_cursors()
{
    DMShellTxWindow w;
    w.reset(2);
    w.noteSent(1);
    w.noteSent(2);
    w.notePeerAcked(2);

    // A duplicate or reordered frame carrying an older cursor must not close the window again.
    w.notePeerAcked(1);
    TEST_ASSERT_EQUAL_UINT32(0, w.outstanding());

    // A peer claiming to have received more than we ever sent cannot buy itself extra credit; the
    // cursor is clamped, so the window reflects our own progress once we do send more.
    w.notePeerAcked(999);
    w.noteSent(3);
    w.noteSent(4);
    TEST_ASSERT_EQUAL_UINT32(2, w.outstanding());
    TEST_ASSERT_FALSE(w.canSend());
}

void test_dmshell_tx_window_of_one_is_strict_request_response()
{
    DMShellTxWindow w;
    w.reset(1);

    w.noteSent(1);
    TEST_ASSERT_FALSE(w.canSend());
    w.notePeerAcked(1);
    TEST_ASSERT_TRUE(w.canSend());
}

void test_dmshell_tx_window_reset_clears_peer_state()
{
    DMShellTxWindow w;
    w.reset(2);
    w.noteSent(1);
    w.noteSent(2);
    w.notePeerAcked(2);

    // A new session restarts sequence numbering at 1, so a stale cursor would grant credit for
    // frames the new session has not sent.
    w.reset(2);
    TEST_ASSERT_EQUAL_UINT32(0, w.outstanding());
    w.noteSent(1);
    w.noteSent(2);
    TEST_ASSERT_FALSE(w.canSend());
}

void test_dmshell_tx_window_reports_peer_cursor_for_retransmission()
{
    DMShellTxWindow w;
    w.reset(4);
    for (uint32_t seq = 1; seq <= 4; seq++) {
        w.noteSent(seq);
    }
    w.notePeerAcked(2);

    // A blocked sender works out what to retransmit from this, so it has to be the clamped, monotone
    // value rather than whatever the last frame happened to claim.
    TEST_ASSERT_EQUAL_UINT32(2, w.peerAcked());
    w.notePeerAcked(1);
    TEST_ASSERT_EQUAL_UINT32(2, w.peerAcked());
    w.notePeerAcked(99);
    TEST_ASSERT_EQUAL_UINT32(4, w.peerAcked());
}

void test_dmshell_tx_window_latches_when_peer_cursor_freezes()
{
    DMShellTxWindow w;
    w.reset(4);

    // The failure measured on hardware. The peer loses frame 3, so its cumulative cursor freezes at
    // 2 however many frames arrive afterwards, and the sender fills the window against a cursor that
    // will never move on its own.
    w.noteSent(1);
    w.noteSent(2);
    w.notePeerAcked(2);
    for (uint32_t seq = 3; seq <= 6; seq++) {
        w.noteSent(seq);
    }

    TEST_ASSERT_FALSE(w.canSend());
    TEST_ASSERT_EQUAL_UINT32(4, w.outstanding());

    // Nothing here reopens it, which is why the sender has to retransmit peerAcked() + 1 rather than
    // wait to be asked: with the window shut the peer never sees a higher sequence number, so it
    // never re-asks, and the session latches silently.
    TEST_ASSERT_EQUAL_UINT32(3, w.peerAcked() + 1);

    // The retransmission lands and the cursor jumps past the whole run the peer had buffered.
    w.notePeerAcked(6);
    TEST_ASSERT_TRUE(w.canSend());
    TEST_ASSERT_EQUAL_UINT32(0, w.outstanding());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_dmshell_rx_seq_zero_always_processed);
    RUN_TEST(test_dmshell_rx_in_order_run_requests_nothing);
    RUN_TEST(test_dmshell_rx_gap_requests_once_then_damps);
    RUN_TEST(test_dmshell_rx_gap_reasks_once_the_interval_elapses);
    RUN_TEST(test_dmshell_rx_a_different_gap_is_not_damped);
    RUN_TEST(test_dmshell_rx_duplicate_with_no_gap_requests_nothing);
    RUN_TEST(test_dmshell_rx_zero_interval_disables_damping);
    RUN_TEST(test_dmshell_rx_damping_survives_millis_wrap);
    RUN_TEST(test_dmshell_tx_history_finds_retained_sequence);
    RUN_TEST(test_dmshell_tx_history_reports_evicted_sequence);
    RUN_TEST(test_dmshell_tx_history_evicts_with_capacity_one);
    RUN_TEST(test_dmshell_tx_history_ignores_unsent_sequence);
    RUN_TEST(test_dmshell_tx_history_empty_ring_is_not_eviction);
    RUN_TEST(test_dmshell_tx_history_reset_forgets_everything);
    RUN_TEST(test_dmshell_tx_window_unbounded_when_capacity_zero);
    RUN_TEST(test_dmshell_tx_window_closes_at_capacity);
    RUN_TEST(test_dmshell_tx_window_reopens_on_ack);
    RUN_TEST(test_dmshell_tx_window_ignores_stale_and_overreaching_cursors);
    RUN_TEST(test_dmshell_tx_window_of_one_is_strict_request_response);
    RUN_TEST(test_dmshell_tx_window_reset_clears_peer_state);
    RUN_TEST(test_dmshell_tx_window_reports_peer_cursor_for_retransmission);
    RUN_TEST(test_dmshell_tx_window_latches_when_peer_cursor_freezes);
    exit(UNITY_END());
}

void loop() {}
