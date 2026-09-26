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
// 4. A repeated OPEN from the peer that owns the running session is answered by resending OPEN_OK,
//    not by opening a second session over the first. A lost OPEN_OK could not otherwise be recovered:
//    OPEN_OK is always seq 1, and a replay request for seq 1 encodes as last_rx_seq 0, which both
//    ends read as "no request". The client repeats its OPEN instead, and this makes that repeat safe.
//
// 5. The sender's window-shut retransmission waits for the frame, not the poll, and for a measured
//    acknowledgement latency rather than a derived one. Either half alone left a lossless bulk
//    transfer with a third of its frames arriving twice.
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

void test_dmshell_retransmit_run_allows_exactly_the_bound()
{
    DMShellRetransmitRun r;
    r.reset(3);

    for (uint32_t attempt = 1; attempt <= 3; attempt++) {
        TEST_ASSERT_TRUE(r.allowRetransmit(7));
        TEST_ASSERT_EQUAL_UINT32(attempt, r.repeatCount());
    }

    // The fourth ask is refused and stays refused, so the caller tears the session down once rather
    // than alternating between giving up and trying again.
    TEST_ASSERT_FALSE(r.allowRetransmit(7));
    TEST_ASSERT_FALSE(r.allowRetransmit(7));
    TEST_ASSERT_EQUAL_UINT32(3, r.repeatCount());
}

void test_dmshell_retransmit_run_restarts_on_a_new_sequence_number()
{
    DMShellRetransmitRun r;
    r.reset(2);

    TEST_ASSERT_TRUE(r.allowRetransmit(7));
    TEST_ASSERT_TRUE(r.allowRetransmit(7));
    TEST_ASSERT_FALSE(r.allowRetransmit(7));

    // A different gap is a different problem. Healthy recovery walks through many sequence numbers,
    // so the allowance has to be per sequence number or a long session would eventually trip it.
    TEST_ASSERT_TRUE(r.allowRetransmit(8));
    TEST_ASSERT_EQUAL_UINT32(1, r.repeatCount());
}

void test_dmshell_retransmit_run_clear_forgives_a_peer_that_caught_up()
{
    DMShellRetransmitRun r;
    r.reset(2);
    TEST_ASSERT_TRUE(r.allowRetransmit(7));
    TEST_ASSERT_TRUE(r.allowRetransmit(7));

    // The peer's cursor advanced, which proves it is alive; the frames it needed next must not
    // inherit a nearly spent allowance.
    r.clear();
    TEST_ASSERT_EQUAL_UINT32(0, r.repeatCount());
    TEST_ASSERT_TRUE(r.allowRetransmit(7));
    TEST_ASSERT_TRUE(r.allowRetransmit(7));
    TEST_ASSERT_FALSE(r.allowRetransmit(7));
}

void test_dmshell_retransmit_run_unbounded_when_limit_zero()
{
    DMShellRetransmitRun r;
    r.reset(0);

    // The measured pre-fix behaviour, kept reachable so one build can be A/B tested: 88 repeats of a
    // single sequence number and still going.
    for (uint32_t attempt = 0; attempt < 200; attempt++) {
        TEST_ASSERT_TRUE(r.allowRetransmit(7));
    }
    TEST_ASSERT_EQUAL_UINT32(200, r.repeatCount());
}

void test_dmshell_rx_duplicate_is_flagged_for_a_bare_ack()
{
    DMShellRxWindow w;
    w.reset(1);

    // Nothing missing, so there is no replay request to carry our cursor. The peer is repeating a
    // frame we already have, which is what a sender whose window has shut looks like, and the caller
    // answers with a bare ACK.
    DMShellRxDecision d = w.classify(2, 1000, 500);
    TEST_ASSERT_TRUE(d.process);
    TEST_ASSERT_FALSE(d.duplicate);

    d = w.classify(2, 1100, 500);
    TEST_ASSERT_FALSE(d.process);
    TEST_ASSERT_TRUE(d.duplicate);
    TEST_ASSERT_FALSE(d.requestReplay);
}

void test_dmshell_rx_damped_gap_is_not_a_duplicate()
{
    DMShellRxWindow w;
    w.reset(1);

    // A frame above the gap, then another while the request is damped. The second is silent on
    // purpose - answering every frame behind a hole is exactly the load the damping exists to avoid -
    // so it must not be mistaken for a duplicate.
    DMShellRxDecision d = w.classify(4, 1000, 500);
    TEST_ASSERT_TRUE(d.requestReplay);
    TEST_ASSERT_FALSE(d.duplicate);

    d = w.classify(5, 1100, 500);
    TEST_ASSERT_FALSE(d.process);
    TEST_ASSERT_FALSE(d.requestReplay);
    TEST_ASSERT_FALSE(d.duplicate);
}

void test_dmshell_rx_duplicate_behind_a_gap_asks_instead()
{
    DMShellRxWindow w;
    w.reset(1);

    w.classify(4, 1000, 500); // opens a gap at 2 and damps the request
    DMShellRxDecision d = w.classify(2, 2000, 500);
    TEST_ASSERT_TRUE(d.process);

    // Still waiting on 3, and a replay request carries the same cursor a bare ACK would, so the
    // caller has nothing extra to send.
    d = w.classify(2, 3000, 500);
    TEST_ASSERT_TRUE(d.duplicate);
    TEST_ASSERT_TRUE(d.requestReplay);
    TEST_ASSERT_EQUAL_UINT32(3, d.replaySeq);
}

void test_dmshell_rx_reorder_holds_and_returns_slots()
{
    DMShellRxReorder r;
    r.reset();

    const int slot = r.claimSlotFor(5);
    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_TRUE(r.holds(5));
    TEST_ASSERT_EQUAL_INT(slot, r.find(5));
    TEST_ASSERT_EQUAL_UINT32(1, r.count());

    // The same sequence number arriving again must not take a second slot, or a retransmitting peer
    // would fill the buffer with copies of one frame.
    TEST_ASSERT_EQUAL_INT(-1, r.claimSlotFor(5));
    TEST_ASSERT_EQUAL_UINT32(1, r.count());

    r.release(slot);
    TEST_ASSERT_FALSE(r.holds(5));
    TEST_ASSERT_EQUAL_UINT32(0, r.count());
}

void test_dmshell_rx_reorder_never_holds_seq_zero()
{
    DMShellRxReorder r;
    r.reset();

    // Unsequenced control frames are always processed in order, so they have nothing to reorder, and
    // 0 is the empty marker.
    TEST_ASSERT_EQUAL_INT(-1, r.claimSlotFor(0));
    TEST_ASSERT_FALSE(r.holds(0));
    TEST_ASSERT_EQUAL_INT(-1, r.find(0));
}

void test_dmshell_rx_reorder_full_buffer_keeps_the_lowest()
{
    DMShellRxReorder r;
    r.reset();
    for (uint32_t seq = 20; seq < 20 + (uint32_t)DMShellRxReorder::SLOTS; seq++) {
        TEST_ASSERT_TRUE(r.claimSlotFor(seq) >= 0);
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)DMShellRxReorder::SLOTS, r.count());

    // Full. A frame further out than everything held is the one to drop, because the gap fills from
    // below and the lowest sequence numbers are needed soonest.
    TEST_ASSERT_EQUAL_INT(-1, r.claimSlotFor(99));
    TEST_ASSERT_FALSE(r.holds(99));

    // A frame nearer the gap displaces the furthest one.
    TEST_ASSERT_TRUE(r.claimSlotFor(10) >= 0);
    TEST_ASSERT_TRUE(r.holds(10));
    TEST_ASSERT_FALSE(r.holds(20 + (uint32_t)DMShellRxReorder::SLOTS - 1));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)DMShellRxReorder::SLOTS, r.count());
}

void test_dmshell_rx_reorder_drains_in_order_after_the_gap_fills()
{
    // The measured failure: frames above a gap arrived intact and were discarded, so the peer had to
    // resend each one after the gap filled - a round trip per frame it already had. Here they are held
    // and the whole run is delivered as soon as the missing frame lands.
    DMShellRxWindow w;
    DMShellRxReorder r;
    w.reset(10);
    r.reset();

    for (uint32_t seq = 12; seq <= 15; seq++) {
        DMShellRxDecision d = w.classify(seq, 1000 + seq, 500);
        TEST_ASSERT_FALSE(d.process);
        TEST_ASSERT_FALSE(d.duplicate);
        TEST_ASSERT_TRUE(r.claimSlotFor(seq) >= 0);
    }
    TEST_ASSERT_EQUAL_UINT32(4, r.count());

    DMShellRxDecision d = w.classify(11, 3000, 500);
    TEST_ASSERT_TRUE(d.process);

    // The window wants 12 next and asks for it, but we are holding it - which is what the caller's
    // suppression check is for, so no replay request goes out for a frame already in hand.
    TEST_ASSERT_TRUE(d.requestReplay);
    TEST_ASSERT_EQUAL_UINT32(12, d.replaySeq);
    TEST_ASSERT_TRUE(r.holds(d.replaySeq));

    uint32_t delivered = 0;
    while (r.count() > 0) {
        const int slot = r.find(w.nextExpected());
        if (slot < 0) {
            break;
        }
        const uint32_t seq = w.nextExpected();
        r.release(slot);
        TEST_ASSERT_TRUE(w.classify(seq, 3100 + seq, 500).process);
        delivered++;
    }
    TEST_ASSERT_EQUAL_UINT32(4, delivered);
    TEST_ASSERT_EQUAL_UINT32(0, r.count());
    TEST_ASSERT_EQUAL_UINT32(15, w.lastInOrder());

    // Fully caught up, so nothing is outstanding and the next frame in sequence just flows.
    TEST_ASSERT_TRUE(w.classify(16, 4000, 500).process);
}

void test_dmshell_rx_reorder_a_hole_inside_the_buffer_still_asks()
{
    DMShellRxWindow w;
    DMShellRxReorder r;
    w.reset(10);
    r.reset();

    // 13 is lost as well, so draining stops at it and the window has to ask for that one.
    const uint32_t arrived[] = {12, 14, 15};
    for (uint32_t seq : arrived) {
        w.classify(seq, 1000 + seq, 500);
        r.claimSlotFor(seq);
    }
    TEST_ASSERT_TRUE(w.classify(11, 3000, 500).process);

    const int slot = r.find(w.nextExpected());
    TEST_ASSERT_TRUE(slot >= 0);
    r.release(slot);
    DMShellRxDecision d = w.classify(12, 3100, 500);
    TEST_ASSERT_TRUE(d.process);
    TEST_ASSERT_TRUE(d.requestReplay);
    TEST_ASSERT_EQUAL_UINT32(13, d.replaySeq);

    // Not held, so this request is the one that must actually go out.
    TEST_ASSERT_FALSE(r.holds(13));
    TEST_ASSERT_EQUAL_INT(-1, r.find(w.nextExpected()));
    TEST_ASSERT_EQUAL_UINT32(2, r.count());
}

// An OPEN whose OPEN_OK was lost. Round 10 on hardware: the client waited out its full 180 s
// --open-timeout while the server, which had accepted the session, streamed output to nobody. The
// client now repeats its OPEN under the same session id, and this is the server's side of that.
namespace
{
constexpr uint32_t kSession = 0x3b091724;
constexpr uint32_t kPeer = 0x999312f2;
} // namespace

void test_dmshell_open_with_no_session_opens()
{
    TEST_ASSERT_EQUAL(DMShellOpenAction::Open, classifyOpen(false, 0, 0, kSession, kPeer, 0));
}

void test_dmshell_open_repeated_before_any_ack_resends_open_ok()
{
    // Preempting here would fork a second shell and discard the first one's output, which is what a
    // naive client-side retry would have done.
    TEST_ASSERT_EQUAL(DMShellOpenAction::ResendOpenOk, classifyOpen(true, kSession, kPeer, kSession, kPeer, 0));
}

void test_dmshell_open_repeated_after_the_peer_acked_is_ignored()
{
    // The peer has OPEN_OK. Answering would replay seq 1, and on a session long enough for seq 1 to
    // have left the history, the replay path closes the session as evicted.
    TEST_ASSERT_EQUAL(DMShellOpenAction::Ignore, classifyOpen(true, kSession, kPeer, kSession, kPeer, 1));
    TEST_ASSERT_EQUAL(DMShellOpenAction::Ignore, classifyOpen(true, kSession, kPeer, kSession, kPeer, 500));
}

void test_dmshell_open_with_a_new_session_id_still_preempts()
{
    // A restarted client picks a fresh random id, and must still be able to take the shell over.
    TEST_ASSERT_EQUAL(DMShellOpenAction::Open, classifyOpen(true, kSession, kPeer, kSession + 1, kPeer, 0));
}

void test_dmshell_open_from_another_peer_still_preempts()
{
    TEST_ASSERT_EQUAL(DMShellOpenAction::Open, classifyOpen(true, kSession, kPeer, kSession, kPeer + 1, 0));
}

void test_dmshell_open_with_session_id_zero_never_matches()
{
    // The module replaces an id of 0 with a random one, so a running session can never carry 0 - but a
    // match on it must not be possible even if one somehow did.
    TEST_ASSERT_EQUAL(DMShellOpenAction::Open, classifyOpen(true, 0, kPeer, 0, kPeer, 0));
}

// The server's window-shut retransmission, timed the way the client's has been since 0246db2. Round 10
// on hardware: a bulk transfer with zero loss had 34% of its arriving frames be copies, because the
// retransmission fired on the poll after every cursor advance, and its interval was one derived round
// trip where the acknowledgement it waits for queues behind a window of our own frames.

void test_dmshell_ack_latency_first_sample_is_the_estimate()
{
    DMShellAckLatency l;
    TEST_ASSERT_EQUAL_UINT32(0, l.estimateMs());
    l.noteSample(800);
    TEST_ASSERT_EQUAL_UINT32(800, l.estimateMs());
}

void test_dmshell_ack_latency_smooths_by_a_quarter()
{
    DMShellAckLatency l;
    l.noteSample(800);
    l.noteSample(1600); // 800 + (1600 - 800) / 4
    TEST_ASSERT_EQUAL_UINT32(1000, l.estimateMs());
    l.noteSample(200); // 1000 + (200 - 1000) / 4
    TEST_ASSERT_EQUAL_UINT32(800, l.estimateMs());
}

void test_dmshell_ack_latency_ignores_a_zero_sample()
{
    // A zero sample means the send stamp and the acknowledgement landed on the same millisecond, which
    // says nothing about the link. Taken as the first sample it would read as "no estimate".
    DMShellAckLatency l;
    l.noteSample(0);
    TEST_ASSERT_EQUAL_UINT32(0, l.estimateMs());
    l.noteSample(500);
    l.noteSample(0);
    TEST_ASSERT_EQUAL_UINT32(500, l.estimateMs());
}

void test_dmshell_ack_latency_interval_is_floored_before_any_sample()
{
    // No acknowledgement yet, so the derived round trip is all there is: the old behaviour.
    DMShellAckLatency l;
    TEST_ASSERT_EQUAL_UINT32(658, l.intervalMs(658, 10000));
}

void test_dmshell_ack_latency_interval_is_twice_the_estimate_within_bounds()
{
    DMShellAckLatency l;
    l.noteSample(1200);
    TEST_ASSERT_EQUAL_UINT32(2400, l.intervalMs(658, 10000));

    DMShellAckLatency fast;
    fast.noteSample(100); // 200 is below the derived round trip, which still wins
    TEST_ASSERT_EQUAL_UINT32(658, fast.intervalMs(658, 10000));

    DMShellAckLatency slow;
    slow.noteSample(9000); // 18 s would sit out most of a LongFast session
    TEST_ASSERT_EQUAL_UINT32(10000, slow.intervalMs(658, 10000));
}

void test_dmshell_retransmit_waits_for_the_frame_not_the_poll()
{
    // The frame went to the radio at 5000. A poll at 5010 - which is where a cursor advance used to
    // send it again - must not; only a full interval after the frame's own send may.
    TEST_ASSERT_FALSE(retransmitDue(5010, 5000, 1400));
    TEST_ASSERT_FALSE(retransmitDue(6399, 5000, 1400));
    TEST_ASSERT_TRUE(retransmitDue(6400, 5000, 1400)); // inclusive, as Throttle::hasElapsed()
    TEST_ASSERT_TRUE(retransmitDue(9000, 5000, 1400));
}

void test_dmshell_retransmit_due_survives_millis_wrap()
{
    const uint32_t sent = 0xFFFFFF00u;                         // 256 ms before the wrap
    TEST_ASSERT_FALSE(retransmitDue(0x00000100u, sent, 1400)); // 512 ms later, across the wrap
    TEST_ASSERT_TRUE(retransmitDue(sent + 1400, sent, 1400));
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
    RUN_TEST(test_dmshell_retransmit_run_allows_exactly_the_bound);
    RUN_TEST(test_dmshell_retransmit_run_restarts_on_a_new_sequence_number);
    RUN_TEST(test_dmshell_retransmit_run_clear_forgives_a_peer_that_caught_up);
    RUN_TEST(test_dmshell_retransmit_run_unbounded_when_limit_zero);
    RUN_TEST(test_dmshell_rx_duplicate_is_flagged_for_a_bare_ack);
    RUN_TEST(test_dmshell_rx_damped_gap_is_not_a_duplicate);
    RUN_TEST(test_dmshell_rx_duplicate_behind_a_gap_asks_instead);
    RUN_TEST(test_dmshell_rx_reorder_holds_and_returns_slots);
    RUN_TEST(test_dmshell_rx_reorder_never_holds_seq_zero);
    RUN_TEST(test_dmshell_rx_reorder_full_buffer_keeps_the_lowest);
    RUN_TEST(test_dmshell_rx_reorder_drains_in_order_after_the_gap_fills);
    RUN_TEST(test_dmshell_rx_reorder_a_hole_inside_the_buffer_still_asks);
    RUN_TEST(test_dmshell_open_with_no_session_opens);
    RUN_TEST(test_dmshell_open_repeated_before_any_ack_resends_open_ok);
    RUN_TEST(test_dmshell_open_repeated_after_the_peer_acked_is_ignored);
    RUN_TEST(test_dmshell_open_with_a_new_session_id_still_preempts);
    RUN_TEST(test_dmshell_open_from_another_peer_still_preempts);
    RUN_TEST(test_dmshell_open_with_session_id_zero_never_matches);
    RUN_TEST(test_dmshell_ack_latency_first_sample_is_the_estimate);
    RUN_TEST(test_dmshell_ack_latency_smooths_by_a_quarter);
    RUN_TEST(test_dmshell_ack_latency_ignores_a_zero_sample);
    RUN_TEST(test_dmshell_ack_latency_interval_is_floored_before_any_sample);
    RUN_TEST(test_dmshell_ack_latency_interval_is_twice_the_estimate_within_bounds);
    RUN_TEST(test_dmshell_retransmit_waits_for_the_frame_not_the_poll);
    RUN_TEST(test_dmshell_retransmit_due_survives_millis_wrap);
    exit(UNITY_END());
}

void loop() {}
