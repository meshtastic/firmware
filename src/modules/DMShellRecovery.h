#pragma once

// Sequence and replay bookkeeping for DMShell, split out of DMShellModule so it pulls in no
// platform headers and the native test suite can drive it directly. Time is always passed in
// rather than read from millis(), so the tests are deterministic.

#include "mesh/Throttle.h"
#include <stdint.h>

/// What the receive side wants done with one inbound sequenced frame.
struct DMShellRxDecision {
    bool process = false;       ///< In order: hand it to the session.
    bool requestReplay = false; ///< Ask the peer to replay replaySeq.
    bool duplicate = false;     ///< Already had it in order: the peer has not seen our receive cursor.
    uint32_t replaySeq = 0;
};

/// Whether a sequence number the peer asked us to replay is still available.
enum class DMShellReplayLookup : uint8_t {
    Found,      ///< Still in the ring: replay it.
    NotSentYet, ///< At or past our next sequence number, so we never sent it. Ignore.
    Evicted,    ///< Sent, but aged out of the ring. Unrecoverable.
};

/// Receive-side sequence state.
///
/// Damping matters: without it every frame arriving above the gap produces its own replay
/// request, so one loss costs as many requests as the sender has frames in flight, and each
/// request costs the sender a duplicate replay.
class DMShellRxWindow
{
  public:
    /// Seed from the sequence number of the OPEN that started the session.
    void reset(uint32_t openSeq)
    {
        lastInOrderSeq = openSeq;
        nextExpectedSeq = openSeq + 1;
        highestSeenSeq = openSeq;
        requestedSeq = 0;
        nextRequestAllowedMs = 0;
        everRequested = false;
    }

    uint32_t lastInOrder() const { return lastInOrderSeq; }
    uint32_t nextExpected() const { return nextExpectedSeq; }

    /// Classify one inbound frame. replayIntervalMs of 0 disables damping.
    DMShellRxDecision classify(uint32_t seq, uint32_t nowMs, uint32_t replayIntervalMs)
    {
        DMShellRxDecision decision;

        if (seq == 0) {
            decision.process = true; // unsequenced control frame (ACK, and anything else seq-less)
            return decision;
        }

        if (seq < nextExpectedSeq) {
            // A duplicate or a replay we no longer need. The peer would not be repeating a frame we
            // already have unless it has not seen our cursor - which is what a sender whose own window
            // has shut looks like - so the caller owes it one, and asking for a replay says the same
            // thing. Only worth a request if we are still waiting on something below the highest
            // sequence number we have seen.
            decision.duplicate = true;
            if (highestSeenSeq >= nextExpectedSeq) {
                askFor(nextExpectedSeq, nowMs, replayIntervalMs, decision);
            }
            return decision;
        }

        if (seq > nextExpectedSeq) {
            if (seq > highestSeenSeq) {
                highestSeenSeq = seq;
            }
            askFor(nextExpectedSeq, nowMs, replayIntervalMs, decision);
            return decision;
        }

        lastInOrderSeq = seq;
        nextExpectedSeq = seq + 1;
        if (seq > highestSeenSeq) {
            highestSeenSeq = seq;
        }
        decision.process = true;

        if (highestSeenSeq >= nextExpectedSeq) {
            // This frame filled one hole but a later one is still open.
            askFor(nextExpectedSeq, nowMs, replayIntervalMs, decision);
        } else {
            highestSeenSeq = 0; // fully caught up
        }
        return decision;
    }

  private:
    void askFor(uint32_t seq, uint32_t nowMs, uint32_t replayIntervalMs, DMShellRxDecision &decision)
    {
        // Re-asking for the same sequence number before a replay could plausibly have arrived is
        // pure channel load. A different sequence number is always allowed through immediately.
        if (everRequested && requestedSeq == seq && !Throttle::deadlinePassedAt(nowMs, nextRequestAllowedMs)) {
            return;
        }
        everRequested = true;
        requestedSeq = seq;
        nextRequestAllowedMs = nowMs + replayIntervalMs;
        decision.requestReplay = true;
        decision.replaySeq = seq;
    }

    uint32_t lastInOrderSeq = 0;
    uint32_t nextExpectedSeq = 1;
    uint32_t highestSeenSeq = 0;
    uint32_t requestedSeq = 0;
    uint32_t nextRequestAllowedMs = 0;
    bool everRequested = false;
};

/// Slot bookkeeping for a small out-of-order receive buffer.
///
/// Without one, a frame that arrives above a gap is discarded even though it arrived intact, and the
/// peer has to send it again once the gap fills - so one loss costs a round trip per frame that was
/// already in hand. Measured on hardware at LongFast: the sequence numbers the receiver kept asking
/// for had arrived a dozen times each, and the session advanced roughly one frame per round trip while
/// delivering almost nothing.
///
/// Only sequence numbers live here; the frames themselves stay with the module, because this header
/// deliberately pulls in no protobuf or platform headers.
class DMShellRxReorder
{
  public:
    /// Slots are indices into the caller's frame array. 8 is twice the peer's default send window, so a
    /// well-behaved peer can never fill it, and a peer running unbounded is bounded here instead.
    static constexpr int SLOTS = 8;

    void reset()
    {
        for (int i = 0; i < SLOTS; i++) {
            slotSeq[i] = 0;
        }
    }

    /// The slot holding seq, or -1.
    int find(uint32_t seq) const
    {
        if (seq == 0) {
            return -1;
        }
        for (int i = 0; i < SLOTS; i++) {
            if (slotSeq[i] == seq) {
                return i;
            }
        }
        return -1;
    }

    bool holds(uint32_t seq) const { return find(seq) >= 0; }

    /// Where to store seq, or -1 if it should be dropped: already held, or the buffer is full of
    /// sequence numbers we need sooner. Claims the slot, so the caller must write the frame into it.
    int claimSlotFor(uint32_t seq)
    {
        if (seq == 0 || holds(seq)) {
            return -1;
        }
        for (int i = 0; i < SLOTS; i++) {
            if (slotSeq[i] == 0) {
                slotSeq[i] = seq;
                return i;
            }
        }
        // Full. The lowest sequence numbers are the ones needed soonest, so the highest is the one
        // worth losing - and only to something lower than it.
        int highest = 0;
        for (int i = 1; i < SLOTS; i++) {
            if (slotSeq[i] > slotSeq[highest]) {
                highest = i;
            }
        }
        if (seq >= slotSeq[highest]) {
            return -1;
        }
        slotSeq[highest] = seq;
        return highest;
    }

    void release(int slot)
    {
        if (slot >= 0 && slot < SLOTS) {
            slotSeq[slot] = 0;
        }
    }

    uint32_t count() const
    {
        uint32_t held = 0;
        for (int i = 0; i < SLOTS; i++) {
            if (slotSeq[i] != 0) {
                held++;
            }
        }
        return held;
    }

  private:
    uint32_t slotSeq[SLOTS] = {}; // 0 = empty
};

/// Sender-side bound on how far ahead of the peer's acknowledgements we are willing to transmit.
///
/// Without one, a lost frame is fatal rather than transient: the sender keeps streaming, the
/// receiver will not advance past the hole, and by the time a replay is asked for the frame has
/// aged out of the ring. Measured on hardware at both ends of the preset range - whichever side has
/// the higher frame rate outruns its own ring first, so enlarging a ring only moves the failure to
/// the other end. Bounding the outstanding data instead makes the ring size irrelevant, because the
/// sender can never get more than a window ahead of the gap.
///
/// It also bounds the send path in wall-clock terms, which is what makes an idle timeout mean
/// something: a sender blocked on a closed window stops transmitting, so a peer that vanishes no
/// longer leaves it talking to nobody indefinitely.
class DMShellTxWindow
{
  public:
    /// capacity is in frames; 0 disables the bound entirely (the pre-window behaviour).
    void reset(uint32_t capacity)
    {
        windowFrames = capacity;
        highestSentSeq = 0;
        peerAckedSeq = 0;
    }

    /// Call with the sequence number of each sequenced frame handed to the radio.
    void noteSent(uint32_t seq)
    {
        if (seq > highestSentSeq) {
            highestSentSeq = seq;
        }
    }

    /// Call with the peer's cumulative receive cursor, from any inbound frame that carries one.
    /// Clamped to what we have actually sent, so a confused or malicious peer cannot open the
    /// window past our own progress, and monotone, so a stale frame cannot close it again.
    void notePeerAcked(uint32_t seq)
    {
        if (seq > highestSentSeq) {
            seq = highestSentSeq;
        }
        if (seq > peerAckedSeq) {
            peerAckedSeq = seq;
        }
    }

    uint32_t outstanding() const { return highestSentSeq - peerAckedSeq; }

    /// The peer's cumulative cursor, so a blocked sender can work out which frame to retransmit.
    uint32_t peerAcked() const { return peerAckedSeq; }

    /// Whether another frame of new data may be generated. Control frames and replays deliberately
    /// ignore this: blocking a teardown behind a closed window is the same class of bug as buffering
    /// one behind a sequence gap.
    bool canSend() const { return windowFrames == 0 || outstanding() < windowFrames; }

    uint32_t capacity() const { return windowFrames; }

  private:
    uint32_t windowFrames = 0; // 0 = unbounded
    uint32_t highestSentSeq = 0;
    uint32_t peerAckedSeq = 0;
};

/// Counts consecutive retransmissions of one sequence number, so a sender that is talking to nobody
/// eventually stops.
///
/// Bounding the outstanding data left exactly one unbounded behaviour behind: a sender whose window
/// is shut repeats the frame the peer's cursor says is missing, forever. It never evicts anything and
/// the idle timeout keeps being refreshed while the peer is still sending, so nothing else ends the
/// session. Measured on hardware, a healthy link needed at most 11 repeats of a single sequence
/// number and a peer that had genuinely vanished reached 88 without stopping, so a bound in between
/// separates the two cleanly.
class DMShellRetransmitRun
{
  public:
    /// maxRepeats of 0 leaves the run unbounded, for measuring against the old behaviour.
    void reset(uint32_t maxRepeats)
    {
        limit = maxRepeats;
        clear();
    }

    /// Call whenever the peer's cursor advances: progress means the peer is alive, whatever happened
    /// before it.
    void clear()
    {
        currentSeq = 0;
        repeats = 0;
    }

    /// Call before each retransmission. Returns false once the allowance for this sequence number is
    /// spent, which is the caller's signal to give up on the peer rather than keep transmitting.
    bool allowRetransmit(uint32_t seq)
    {
        if (seq != currentSeq) {
            currentSeq = seq;
            repeats = 0;
        }
        if (limit != 0 && repeats >= limit) {
            return false;
        }
        repeats++;
        return true;
    }

    /// How many retransmissions of the current sequence number have been allowed.
    uint32_t repeatCount() const { return repeats; }

  private:
    uint32_t limit = 0; // 0 = unbounded
    uint32_t currentSeq = 0;
    uint32_t repeats = 0;
};

/// Tracks which sent sequence numbers the replay ring still holds.
///
/// The ring is bounded in frames, so the wall-clock span it covers shrinks with the modem preset -
/// 50 frames is about two minutes on LongFast but under seven seconds on ShortTurbo. Once a
/// requested frame has aged out no amount of retrying can recover it, so the caller has to be able
/// to tell that case apart from a request it can still answer.
class DMShellTxHistoryWindow
{
  public:
    explicit DMShellTxHistoryWindow(uint32_t capacity) : ringCapacity(capacity ? capacity : 1) {}

    void reset()
    {
        oldestRetainedSeq = 0;
        newestStoredSeq = 0;
    }

    /// Call once per frame added to the ring, in send order. Replays must not be passed here: they
    /// reuse an existing sequence number and do not displace anything.
    void noteStored(uint32_t seq)
    {
        if (seq == 0) {
            return;
        }
        if (oldestRetainedSeq == 0) {
            oldestRetainedSeq = seq;
        }
        newestStoredSeq = seq;
        if (newestStoredSeq - oldestRetainedSeq >= ringCapacity) {
            oldestRetainedSeq = newestStoredSeq - ringCapacity + 1;
        }
    }

    DMShellReplayLookup classify(uint32_t seq, uint32_t nextTxSeq) const
    {
        if (seq == 0 || seq >= nextTxSeq || oldestRetainedSeq == 0) {
            return DMShellReplayLookup::NotSentYet;
        }
        if (seq < oldestRetainedSeq) {
            return DMShellReplayLookup::Evicted;
        }
        return DMShellReplayLookup::Found;
    }

  private:
    uint32_t ringCapacity;
    uint32_t oldestRetainedSeq = 0; // 0 while nothing has been stored
    uint32_t newestStoredSeq = 0;
};

/// What to do with an OPEN, given whatever session is already running.
enum class DMShellOpenAction : uint8_t {
    Open,         ///< Start a session, preempting any other.
    ResendOpenOk, ///< The peer is retrying an OPEN we already accepted, so its OPEN_OK was lost.
    Ignore,       ///< A late copy of an OPEN whose OPEN_OK the peer has already acknowledged.
};

/// A client that hears no OPEN_OK sends its OPEN again under the same session id. Opening afresh on
/// that retry would preempt the session the first OPEN created, killing its shell along with any output
/// already sent, so a repeat is answered from the running session instead. A different session id or a
/// different peer is a new session and preempts, as it always has; so does a session id of 0, which
/// the module replaces with a random one and so can never match.
///
/// peerAcked is the peer's cumulative receive cursor. Anything above 0 means it already has the OPEN_OK,
/// which is always seq 1, so a repeat is a stale copy. Answering one would replay seq 1, and on a long
/// session that has aged out of the history - the replay path then closes the session as evicted.
inline DMShellOpenAction classifyOpen(bool sessionActive, uint32_t activeSessionId, uint32_t activePeer, uint32_t openSessionId,
                                      uint32_t openFrom, uint32_t peerAcked)
{
    if (!sessionActive || openSessionId == 0 || openSessionId != activeSessionId || openFrom != activePeer) {
        return DMShellOpenAction::Open;
    }
    return peerAcked == 0 ? DMShellOpenAction::ResendOpenOk : DMShellOpenAction::Ignore;
}

/// Smoothed acknowledgement latency, and the retransmission interval derived from it.
///
/// The window-shut retransmission waited one derived round trip - two maximum-length frames plus a
/// margin - but the acknowledgement for the oldest unacknowledged frame queues behind up to a window of
/// our own frames, each waiting out a CSMA backoff that grows with channel utilisation. So it fired
/// before the acknowledgement could arrive, and on a lossless link about a third of what the peer
/// received was copies. The client had the same defect and measures instead (0246db2); this is the same
/// estimator, so the two ends agree.
class DMShellAckLatency
{
  public:
    void reset() { estimate = 0; }

    /// Fold in the time from handing a frame to the radio to seeing the peer's cursor pass it. Smoothed
    /// by a quarter, as the client is: the quantity wanted belongs to the link, and one frame's queueing
    /// or collision should not move it far.
    void noteSample(uint32_t sampleMs)
    {
        if (sampleMs == 0) {
            return;
        }
        if (estimate == 0) {
            estimate = sampleMs;
            return;
        }
        const int64_t next = (int64_t)estimate + ((int64_t)sampleMs - (int64_t)estimate) / 4;
        estimate = next > 0 ? (uint32_t)next : 1;
    }

    /// 0 until the first sample.
    uint32_t estimateMs() const { return estimate; }

    /// Twice the estimate: one frame out and its acknowledgement back are already in it, and the rest is
    /// margin for the peer's queue. Never below floorMs, the derived round trip used before any sample
    /// exists, and never above capMs.
    uint32_t intervalMs(uint32_t floorMs, uint32_t capMs) const
    {
        uint64_t scaled = 2ull * estimate;
        if (scaled < floorMs) {
            scaled = floorMs;
        }
        if (scaled > capMs) {
            scaled = capMs;
        }
        return (uint32_t)scaled;
    }

  private:
    uint32_t estimate = 0;
};

/// Whether a frame last handed to the radio at lastSentMs may be repeated at nowMs.
///
/// The wait belongs to the frame, not to the poll that notices it. Measured from the poll, every cursor
/// advance cleared the deadline, and the next unacknowledged frame - queued moments earlier and possibly
/// still waiting for the radio - was repeated at once. Elapsed time is an unsigned difference, so this
/// holds across the millis() wrap.
inline bool retransmitDue(uint32_t nowMs, uint32_t lastSentMs, uint32_t intervalMs)
{
    return (uint32_t)(nowMs - lastSentMs) >= intervalMs;
}
