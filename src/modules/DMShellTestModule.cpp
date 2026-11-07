#include "DMShellTestModule.h"

#ifdef DMSHELL_TEST_PEER

#include "MeshService.h"
#include "Throttle.h"
#include "configuration.h"
#include "mesh/mesh-pb-constants.h"
#include <string.h>

// When the first session opens, after boot, so the node has its config and the peer's key
#ifndef DMSHELL_TEST_START_DELAY_MS
#define DMSHELL_TEST_START_DELAY_MS 60000
#endif
// The workload, as the bench harness types it: wait, type the command, then a keystroke per interval
#ifndef DMSHELL_TEST_SETTLE_MS
#define DMSHELL_TEST_SETTLE_MS 12000
#endif
#ifndef DMSHELL_TEST_COMMAND
#define DMSHELL_TEST_COMMAND "head -c 120000 /etc/services\n"
#endif
// Type the command a second time at this time from OPEN_OK, so a load burst lands mid-run as well as at the start; 0 = once
#ifndef DMSHELL_TEST_COMMAND_AGAIN_MS
#define DMSHELL_TEST_COMMAND_AGAIN_MS 0
#endif
#ifndef DMSHELL_TEST_KEY_INTERVAL_MS
#define DMSHELL_TEST_KEY_INTERVAL_MS 200
#endif
// Every Nth keystroke is a CR, the rest spaces
#ifndef DMSHELL_TEST_CR_EVERY
#define DMSHELL_TEST_CR_EVERY 10
#endif
// How long each session types for, from OPEN_OK; then it drains its input and closes
#ifndef DMSHELL_TEST_SESSION_MS
#define DMSHELL_TEST_SESSION_MS 300000
#endif
#ifndef DMSHELL_TEST_SESSIONS
#define DMSHELL_TEST_SESSIONS 1
#endif
#ifndef DMSHELL_TEST_GAP_MS
#define DMSHELL_TEST_GAP_MS 30000
#endif
// How keystrokes are grouped into INPUT frames, as the client's stdin delivers them: 0 = a terminal (a frame ends at
// CR or tab, 0.5 s after the last byte, 2 s after the first, or at 64 bytes), 1 = a pipe (64-byte frames only)
#ifndef DMSHELL_TEST_BATCH_PIPE
#define DMSHELL_TEST_BATCH_PIPE 0
#endif
// The client's DMSHELL_INPUT_WINDOW and DMSHELL_ACK_EVERY
#ifndef DMSHELL_TEST_INPUT_WINDOW
#define DMSHELL_TEST_INPUT_WINDOW 4
#endif
#ifndef DMSHELL_TEST_ACK_EVERY
#define DMSHELL_TEST_ACK_EVERY 2
#endif
// The slot parity stamped on every packet the client sends: unset = none (the ordinary backoff), 0 = even, 1 = odd
#ifdef DMSHELL_TEST_SLOT_PARITY
#if DMSHELL_TEST_SLOT_PARITY == 0
static constexpr meshtastic_MeshPacket_SlotParity SLOT_PARITY = meshtastic_MeshPacket_SlotParity_SLOT_PARITY_EVEN;
#elif DMSHELL_TEST_SLOT_PARITY == 1
static constexpr meshtastic_MeshPacket_SlotParity SLOT_PARITY = meshtastic_MeshPacket_SlotParity_SLOT_PARITY_ODD;
#else
#error "DMSHELL_TEST_SLOT_PARITY is 0 (even) or 1 (odd)"
#endif
#else
static constexpr meshtastic_MeshPacket_SlotParity SLOT_PARITY = meshtastic_MeshPacket_SlotParity_SLOT_PARITY_UNSET;
#endif

// The client's timing constants (bin/dmshell_client.py)
static constexpr uint32_t BATCH_IDLE_MS = 500;
static constexpr uint32_t BATCH_MAX_HOLD_MS = 2000;
static constexpr uint32_t RETRY_FLOOR_MS = 1000;
static constexpr uint32_t RETRY_MAX_MS = 8000;
static constexpr uint32_t RETRY_FLAT_ATTEMPTS = 4;
static constexpr uint32_t MAX_INPUT_RETRANSMITS = 25;
static constexpr uint32_t OPEN_RETRY_FIRST_MS = 5000;
static constexpr uint32_t OPEN_RETRY_MAX_MS = 30000;
static constexpr uint32_t OPEN_TIMEOUT_MS = 180000;
static constexpr uint32_t HEARTBEAT_IDLE_MS = 5000;
static constexpr uint32_t HEARTBEAT_REPEAT_MS = 15000;
static constexpr uint32_t DRAIN_TIMEOUT_MS = 10000;
static constexpr uint32_t CLOSE_WAIT_MS = 7000;
static constexpr uint32_t ACTIVE_POLL_MS = 20;

DMShellTestModule *dmShellTestModule;

static const char *opName(meshtastic_RemoteShell_OpCode op)
{
    switch (op) {
    case meshtastic_RemoteShell_OpCode_OPEN:
        return "OPEN";
    case meshtastic_RemoteShell_OpCode_INPUT:
        return "INPUT";
    case meshtastic_RemoteShell_OpCode_RESIZE:
        return "RESIZE";
    case meshtastic_RemoteShell_OpCode_CLOSE:
        return "CLOSE";
    case meshtastic_RemoteShell_OpCode_PING:
        return "PING";
    case meshtastic_RemoteShell_OpCode_ACK:
        return "ACK";
    case meshtastic_RemoteShell_OpCode_OPEN_OK:
        return "OPEN_OK";
    case meshtastic_RemoteShell_OpCode_OUTPUT:
        return "OUTPUT";
    case meshtastic_RemoteShell_OpCode_CLOSED:
        return "CLOSED";
    case meshtastic_RemoteShell_OpCode_ERROR:
        return "ERROR";
    case meshtastic_RemoteShell_OpCode_PONG:
        return "PONG";
    default:
        return "op?";
    }
}

DMShellTestModule::DMShellTestModule()
    : SinglePortModule("DMShellTest", meshtastic_PortNum_REMOTE_SHELL_APP), concurrency::OSThread("DMShellTest")
{
    nextSessionAtMs = DMSHELL_TEST_START_DELAY_MS;
    LOG_INFO("DMShellTest: peer 0x%08x, %u session(s) of %u ms from %u ms after boot, %s batching, window %u, "
             "command again at %u ms",
             (unsigned)DMSHELL_TEST_PEER, (unsigned)DMSHELL_TEST_SESSIONS, (unsigned)DMSHELL_TEST_SESSION_MS,
             (unsigned)DMSHELL_TEST_START_DELAY_MS, DMSHELL_TEST_BATCH_PIPE ? "pipe" : "terminal",
             (unsigned)DMSHELL_TEST_INPUT_WINDOW, (unsigned)DMSHELL_TEST_COMMAND_AGAIN_MS);
}

int32_t DMShellTestModule::runOnce()
{
    const uint32_t now = millis();
    switch (phase) {
    case Phase::Waiting:
        if (!Throttle::deadlinePassedAt(now, nextSessionAtMs))
            return (int32_t)(nextSessionAtMs - now);
        startSession(now);
        return ACTIVE_POLL_MS;
    case Phase::Opening:
        if (Throttle::deadlinePassedAt(now, sessionStartMs + OPEN_TIMEOUT_MS)) {
            sendFrame(meshtastic_RemoteShell_OpCode_CLOSE, now, nullptr, 0, false);
            endSession(now, "open_timeout");
        } else if (Throttle::deadlinePassedAt(now, nextOpenRetryMs)) {
            // Same session id and seq, so the server answers it from the session the first OPEN made
            meshtastic_RemoteShell frame = meshtastic_RemoteShell_init_zero;
            frame.op = meshtastic_RemoteShell_OpCode_OPEN;
            frame.session_id = sessionId;
            frame.seq = openSeq;
            frame.cols = 80;
            frame.rows = 24;
            transmit(frame, now);
            stats.openRetries++;
            LOG_INFO("DMShellTest t=%u no OPEN_OK after %u ms, repeating OPEN", (unsigned)now, (unsigned)openRetryIntervalMs);
            openRetryIntervalMs = min(openRetryIntervalMs * 2, OPEN_RETRY_MAX_MS);
            nextOpenRetryMs = now + openRetryIntervalMs;
        }
        return ACTIVE_POLL_MS;
    case Phase::Active:
        serviceActive(now);
        return ACTIVE_POLL_MS;
    case Phase::Closing:
        if (Throttle::deadlinePassedAt(now, closeDeadlineMs))
            endSession(now, "no_closed");
        return ACTIVE_POLL_MS;
    case Phase::Done:
    default:
        return disable();
    }
}

void DMShellTestModule::startSession(uint32_t now)
{
    sessionId = (uint32_t)random(1, 0x7FFFFFFF);
    nextTxSeq = 1;
    stats = {};
    rxWindow.reset(0);
    rxReorder.reset();
    peerHighestSeq = 0;
    framesSinceOutbound = 0;
    lastInboundMs = now;
    lastHeartbeatMs = 0;
    lastPongReplaySeq = 0;
    txWindow.reset(DMSHELL_TEST_INPUT_WINDOW);
    txWindowBlocked = false;
    for (auto &h : history)
        h.valid = false;
    historyWindow.reset();
    ackLatency.reset();
    retransmitSeq = 0;
    retransmitAttempts = 0;
    nextRetransmitMs = 0;
    pendingInputLen = 0;
    commandTyped = false;
    commandRepeated = false;
    keystrokesTyped = 0;
    batchLen = 0;
    batchFirstByteMs = batchLastByteMs = 0;
    batchEndsHere = false;
    closeSent = false;
    endReason = nullptr;

    sessionStartMs = now;
    phase = Phase::Opening;
    LOG_INFO("DMShellTest t=%u session %u of %u: OPEN session=0x%08x to 0x%08x", (unsigned)now, (unsigned)sessionsDone + 1,
             (unsigned)DMSHELL_TEST_SESSIONS, (unsigned)sessionId, (unsigned)DMSHELL_TEST_PEER);
    openSeq = sendFrame(meshtastic_RemoteShell_OpCode_OPEN, now);
    openRetryIntervalMs = OPEN_RETRY_FIRST_MS;
    nextOpenRetryMs = now + openRetryIntervalMs;
}

void DMShellTestModule::endSession(uint32_t now, const char *reason)
{
    LOG_INFO("DMShellTest t=%u session=0x%08x ended: %s", (unsigned)now, (unsigned)sessionId, reason);
    logStats(now);
    sessionsDone++;
    if (sessionsDone >= DMSHELL_TEST_SESSIONS) {
        phase = Phase::Done;
        LOG_INFO("DMShellTest: all %u session(s) done", (unsigned)sessionsDone);
    } else {
        phase = Phase::Waiting;
        nextSessionAtMs = now + DMSHELL_TEST_GAP_MS;
    }
}

void DMShellTestModule::serviceActive(uint32_t now)
{
    const bool typing = !Throttle::deadlinePassedAt(now, openedAtMs + DMSHELL_TEST_SESSION_MS);
    if (typing)
        typeWorkload(now);
    else
        closeBatch(); // time is up: whatever is still being batched goes out now, as EOF ends the client's read
    flushInput(now);
    serviceRetransmit(now);
    if (phase != Phase::Active)
        return; // the peer stopped answering and the session ended
    serviceHeartbeat(now);
    if (typing)
        return;

    const bool drained = pendingInputLen == 0 && batchLen == 0 && txWindow.outstanding() == 0;
    if (drained || Throttle::deadlinePassedAt(now, openedAtMs + DMSHELL_TEST_SESSION_MS + DRAIN_TIMEOUT_MS)) {
        if (!drained)
            LOG_WARN("DMShellTest t=%u closing with input unacknowledged: %u queued, %u outstanding", (unsigned)now,
                     (unsigned)pendingInputLen, (unsigned)txWindow.outstanding());
        sendFrame(meshtastic_RemoteShell_OpCode_CLOSE, now);
        closeSent = true;
        phase = Phase::Closing;
        closeDeadlineMs = now + CLOSE_WAIT_MS;
    }
}

void DMShellTestModule::addByte(uint8_t b, uint32_t now)
{
    if (batchLen == 0)
        batchFirstByteMs = now;
    batchLastByteMs = now;
    batch[batchLen++] = b;
    if (!DMSHELL_TEST_BATCH_PIPE && (b == '\r' || b == '\t'))
        batchEndsHere = true;
}

// When the client's stdin read would return what has been typed so far
bool DMShellTestModule::batchDue(uint32_t now) const
{
    if (batchLen == 0)
        return false;
    if (batchLen >= BATCH_MAX_BYTES || batchEndsHere)
        return true;
    if (DMSHELL_TEST_BATCH_PIPE)
        return false;
    return Throttle::deadlinePassedAt(now, batchLastByteMs + BATCH_IDLE_MS) ||
           Throttle::deadlinePassedAt(now, batchFirstByteMs + BATCH_MAX_HOLD_MS);
}

// Queue the batch as one INPUT's worth of bytes
void DMShellTestModule::closeBatch()
{
    batchEndsHere = false;
    if (batchLen == 0)
        return;
    const uint32_t room = PENDING_INPUT_MAX - pendingInputLen;
    const uint32_t kept = batchLen < room ? batchLen : room;
    memcpy(pendingInput + pendingInputLen, batch, kept);
    pendingInputLen += kept;
    stats.pendingInputDropped += batchLen - kept;
    batchLen = 0;
    batchFirstByteMs = 0;
}

void DMShellTestModule::typeCommand(uint32_t now)
{
    static const char command[] = DMSHELL_TEST_COMMAND;
    for (size_t i = 0; i + 1 < sizeof(command); i++) {
        addByte((uint8_t)command[i], now);
        if (batchDue(now))
            closeBatch();
    }
    LOG_INFO("DMShellTest t=%u typed the command (%u bytes)", (unsigned)now, (unsigned)(sizeof(command) - 1));
}

// The bench workload: after the settle time the command once, then one keystroke per interval
void DMShellTestModule::typeWorkload(uint32_t now)
{
    if (!commandTyped) {
        if (!Throttle::deadlinePassedAt(now, openedAtMs + DMSHELL_TEST_SETTLE_MS))
            return;
        typeCommand(now);
        commandTyped = true;
        nextKeystrokeMs = now + DMSHELL_TEST_KEY_INTERVAL_MS;
    }
    if (DMSHELL_TEST_COMMAND_AGAIN_MS && !commandRepeated &&
        Throttle::deadlinePassedAt(now, openedAtMs + DMSHELL_TEST_COMMAND_AGAIN_MS)) {
        closeBatch(); // the command starts its own frame, as it would after a pause at a terminal
        typeCommand(now);
        commandRepeated = true;
    }

    if (batchDue(now))
        closeBatch();
    while (Throttle::deadlinePassedAt(now, nextKeystrokeMs)) {
        keystrokesTyped++;
        addByte(keystrokesTyped % DMSHELL_TEST_CR_EVERY == 0 ? '\r' : ' ', now);
        nextKeystrokeMs += DMSHELL_TEST_KEY_INTERVAL_MS;
        if (batchDue(now))
            closeBatch();
    }
}

void DMShellTestModule::flushInput(uint32_t now)
{
    while (pendingInputLen > 0) {
        const bool blocked = !txWindow.canSend();
        if (blocked != txWindowBlocked) {
            txWindowBlocked = blocked;
            if (blocked)
                stats.inputWindowClosed++;
            LOG_INFO("DMShellTest t=%u input window %s at cursor %u, outstanding %u", (unsigned)now,
                     blocked ? "closed" : "reopened", (unsigned)txWindow.peerAcked(), (unsigned)txWindow.outstanding());
        }
        if (blocked)
            return;
        const uint32_t chunk = pendingInputLen < BATCH_MAX_BYTES ? pendingInputLen : BATCH_MAX_BYTES;
        sendFrame(meshtastic_RemoteShell_OpCode_INPUT, now, pendingInput, chunk);
        memmove(pendingInput, pendingInput + chunk, pendingInputLen - chunk);
        pendingInputLen -= chunk;
    }
    if (txWindowBlocked && txWindow.canSend()) {
        txWindowBlocked = false;
        LOG_INFO("DMShellTest t=%u input window reopened at cursor %u, outstanding %u", (unsigned)now,
                 (unsigned)txWindow.peerAcked(), (unsigned)txWindow.outstanding());
    }
}

uint32_t DMShellTestModule::baseIntervalMs() const
{
    return ackLatency.intervalMs(RETRY_FLOOR_MS, RETRY_MAX_MS);
}

// With the window shut the server sees nothing above the gap, so it never asks for the frame it is missing
void DMShellTestModule::serviceRetransmit(uint32_t now)
{
    if (txWindow.canSend())
        return;
    const uint32_t missing = txWindow.peerAcked() + 1;
    if (missing >= nextTxSeq)
        return;
    if (missing != retransmitSeq) {
        retransmitSeq = missing;
        retransmitAttempts = 0;
        retransmitIntervalMs = baseIntervalMs();
        const SentFrame *sent = findSent(missing);
        nextRetransmitMs = (sent ? sent->lastSentMs : now) + retransmitIntervalMs;
    }
    if (!Throttle::deadlinePassedAt(now, nextRetransmitMs))
        return;
    if (retransmitAttempts >= MAX_INPUT_RETRANSMITS) {
        LOG_WARN("DMShellTest t=%u peer_unresponsive seq=%u after %u retransmits", (unsigned)now, (unsigned)missing,
                 (unsigned)retransmitAttempts);
        sendFrame(meshtastic_RemoteShell_OpCode_CLOSE, now, nullptr, 0, false);
        endSession(now, "peer_unresponsive");
        return;
    }
    retransmitAttempts++;
    stats.inputRetransmits++;
    if (retransmitAttempts > RETRY_FLAT_ATTEMPTS)
        retransmitIntervalMs = min(retransmitIntervalMs * 2, RETRY_MAX_MS);
    nextRetransmitMs = now + retransmitIntervalMs;
    LOG_INFO("DMShellTest t=%u input_retransmit seq=%u attempt=%u interval=%u latency=%u", (unsigned)now, (unsigned)missing,
             (unsigned)retransmitAttempts, (unsigned)retransmitIntervalMs, (unsigned)ackLatency.estimateMs());
    replayFrom(missing, now);
}

// Keyed on the peer's silence alone, as the client is: the server's PING handler is what repeats a frame we lack
void DMShellTestModule::serviceHeartbeat(uint32_t now)
{
    if (Throttle::isWithinTimespanMs(lastInboundMs, HEARTBEAT_IDLE_MS))
        return;
    const bool sinceInbound = lastHeartbeatMs == 0 || (int32_t)(lastHeartbeatMs - lastInboundMs) <= 0;
    if (!sinceInbound && Throttle::isWithinTimespanMs(lastHeartbeatMs, HEARTBEAT_REPEAT_MS))
        return;
    lastHeartbeatMs = now;
    sendFrame(meshtastic_RemoteShell_OpCode_PING, now, nullptr, 0, true, nextTxSeq - 1, rxWindow.lastInOrder());
}

ProcessMessage DMShellTestModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // PKI only, as the server requires of us: a channel-encrypted packet can claim any `from`
    if (phase == Phase::Waiting || phase == Phase::Done || getFrom(&mp) != (NodeNum)DMSHELL_TEST_PEER ||
        mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag || !mp.pki_encrypted)
        return ProcessMessage::CONTINUE;
    meshtastic_RemoteShell frame = meshtastic_RemoteShell_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_RemoteShell_fields, &frame)) {
        LOG_WARN("DMShellTest: malformed frame from the peer");
        return ProcessMessage::STOP;
    }
    handleFrame(frame, millis());
    return ProcessMessage::STOP;
}

void DMShellTestModule::handleFrame(const meshtastic_RemoteShell &frame, uint32_t now)
{
    if (frame.session_id != sessionId) {
        // Another session's frame, such as the server finishing an earlier run: its seq belongs to that session
        stats.rxOtherSession++;
        return;
    }
    lastInboundMs = now;
    notePeerReceiveCursor(frame, now);

    if (frame.op == meshtastic_RemoteShell_OpCode_CLOSED) {
        LOG_INFO("DMShellTest t=%u rx CLOSED seq=%u", (unsigned)now, (unsigned)frame.seq);
        endSession(now, closeSent ? "closed" : "closed_by_peer");
        return;
    }
    if (frame.op == meshtastic_RemoteShell_OpCode_ACK) {
        if (frame.last_rx_seq > 0) {
            LOG_INFO("DMShellTest t=%u rx ACK replay request seq=%u", (unsigned)now, (unsigned)frame.last_rx_seq + 1);
            replayFrom(frame.last_rx_seq + 1, now);
        }
        return;
    }

    // Judged against the cursor before this frame moves it, as the client counts them
    const uint32_t expected = rxWindow.nextExpected();
    const char *kind = "new";
    if (frame.seq != 0) {
        stats.rxFramesTotal++;
        if (frame.seq < expected) {
            stats.rxDuplicate++;
            kind = "dup";
        } else if (frame.seq > expected) {
            stats.rxAheadOfCursor++;
            kind = "ahead";
        } else {
            stats.rxNewInOrder++;
        }
        if (frame.seq > peerHighestSeq)
            peerHighestSeq = frame.seq;
    }
    LOG_INFO("DMShellTest t=%u rx %s seq=%u %s len=%u ack=%u", (unsigned)now, opName(frame.op), (unsigned)frame.seq, kind,
             (unsigned)frame.payload.size, (unsigned)frame.ack_seq);

    DMShellRxDecision decision = rxWindow.classify(frame.seq, now, baseIntervalMs());
    if (decision.duplicate) {
        // We have it, so the peer has not seen our cursor
        sendAck(now, decision.requestReplay ? decision.replaySeq : 0);
        return;
    }
    if (!decision.process) {
        const int slot = rxReorder.claimSlotFor(frame.seq);
        if (slot >= 0) {
            reorderFrames[slot] = frame;
            stats.rxHeldForGap++;
        } else if (!rxReorder.holds(frame.seq)) {
            stats.rxDroppedReorderFull++;
        }
        if (decision.requestReplay)
            sendAck(now, decision.replaySeq);
        return;
    }

    handleInOrder(frame, now);
    if (phase != Phase::Active && phase != Phase::Opening && phase != Phase::Closing)
        return;
    uint32_t replaySeq = decision.requestReplay ? decision.replaySeq : 0;
    for (int slot = rxReorder.find(rxWindow.nextExpected()); slot >= 0; slot = rxReorder.find(rxWindow.nextExpected())) {
        const meshtastic_RemoteShell held = reorderFrames[slot];
        rxReorder.release(slot);
        const DMShellRxDecision next = rxWindow.classify(held.seq, now, baseIntervalMs());
        if (!next.process)
            break;
        handleInOrder(held, now);
        replaySeq = next.requestReplay ? next.replaySeq : 0;
    }
    if (replaySeq)
        sendAck(now, replaySeq);
    else if (framesSinceOutbound >= DMSHELL_TEST_ACK_EVERY)
        sendAck(now); // bare: ack_seq alone reopens the server's window
}

void DMShellTestModule::handleInOrder(const meshtastic_RemoteShell &frame, uint32_t now)
{
    if (frame.seq != 0)
        framesSinceOutbound++;
    switch (frame.op) {
    case meshtastic_RemoteShell_OpCode_OPEN_OK:
        if (phase == Phase::Opening) {
            phase = Phase::Active;
            openedAtMs = now;
            LOG_INFO("DMShellTest t=%u opened session=0x%08x after %u ms", (unsigned)now, (unsigned)sessionId,
                     (unsigned)(now - sessionStartMs));
        }
        break;
    case meshtastic_RemoteShell_OpCode_OUTPUT:
        stats.rxOutputBytes += frame.payload.size;
        break;
    case meshtastic_RemoteShell_OpCode_ERROR:
        LOG_WARN("DMShellTest t=%u remote error seq=%u: %.*s", (unsigned)now, (unsigned)frame.seq, (int)frame.payload.size,
                 (const char *)frame.payload.bytes);
        break;
    case meshtastic_RemoteShell_OpCode_PONG:
        if (frame.last_rx_seq != 0 && frame.last_rx_seq < nextTxSeq - 1)
            replayFrom(frame.last_rx_seq + 1, now);
        if (frame.last_tx_seq > rxWindow.lastInOrder()) {
            if (frame.last_tx_seq > peerHighestSeq)
                peerHighestSeq = frame.last_tx_seq;
            const uint32_t want = rxWindow.nextExpected();
            if (want != lastPongReplaySeq || !Throttle::isWithinTimespanMs(lastPongReplayMs, baseIntervalMs())) {
                lastPongReplaySeq = want;
                lastPongReplayMs = now;
                sendAck(now, want);
            }
        }
        break;
    default:
        break;
    }
}

// The server's cursor over our frames is the larger of ack_seq and, on a replay request, last_rx_seq
void DMShellTestModule::notePeerReceiveCursor(const meshtastic_RemoteShell &frame, uint32_t now)
{
    const uint32_t before = txWindow.peerAcked();
    txWindow.notePeerAcked(frame.ack_seq > frame.last_rx_seq ? frame.ack_seq : frame.last_rx_seq);
    const uint32_t cursor = txWindow.peerAcked();
    if (cursor <= before)
        return;
    const SentFrame *sent = findSent(cursor);
    if (sent)
        ackLatency.noteSample(now - sent->lastSentMs);
    retransmitSeq = 0; // progress: the next stall starts a fresh run
}

void DMShellTestModule::replayFrom(uint32_t seq, uint32_t now)
{
    SentFrame *sent = findSent(seq);
    if (!sent) {
        if (historyWindow.classify(seq, nextTxSeq) == DMShellReplayLookup::Evicted) {
            stats.replayEvicted++;
            LOG_WARN("DMShellTest t=%u replay_evicted seq=%u", (unsigned)now, (unsigned)seq);
            sendFrame(meshtastic_RemoteShell_OpCode_CLOSE, now, nullptr, 0, false);
            endSession(now, "replay_evicted");
        } else {
            stats.replayUnavailable++;
            LOG_INFO("DMShellTest t=%u replay_unavailable seq=%u", (unsigned)now, (unsigned)seq);
        }
        return;
    }
    stats.replaysSentToPeer++;
    stats.framesResent++;
    LOG_INFO("DMShellTest t=%u replay_sent %s seq=%u", (unsigned)now, opName(sent->frame.op), (unsigned)seq);
    transmit(sent->frame, now);
    sent->lastSentMs = now;
}

uint32_t DMShellTestModule::sendFrame(meshtastic_RemoteShell_OpCode op, uint32_t now, const uint8_t *payload, size_t len,
                                      bool remember, uint32_t lastTxSeq, uint32_t lastRxSeq)
{
    meshtastic_RemoteShell frame = meshtastic_RemoteShell_init_zero;
    frame.op = op;
    frame.session_id = sessionId;
    frame.seq = op == meshtastic_RemoteShell_OpCode_ACK ? 0 : nextTxSeq++;
    frame.ack_seq = rxWindow.lastInOrder();
    frame.last_tx_seq = lastTxSeq;
    frame.last_rx_seq = lastRxSeq;
    if (op == meshtastic_RemoteShell_OpCode_OPEN) {
        frame.cols = 80;
        frame.rows = 24;
    }
    if (payload && len) {
        if (len > sizeof(frame.payload.bytes))
            len = sizeof(frame.payload.bytes);
        memcpy(frame.payload.bytes, payload, len);
        frame.payload.size = len;
    }
    if (frame.seq != 0) {
        txWindow.noteSent(frame.seq);
        if (remember) {
            SentFrame &slot = history[frame.seq % HISTORY_LEN];
            slot.valid = true;
            slot.frame = frame;
            slot.lastSentMs = now;
            historyWindow.noteStored(frame.seq);
        }
    }
    LOG_INFO("DMShellTest t=%u tx %s seq=%u len=%u ack=%u", (unsigned)now, opName(op), (unsigned)frame.seq, (unsigned)len,
             (unsigned)frame.ack_seq);
    transmit(frame, now);
    return frame.seq;
}

void DMShellTestModule::sendAck(uint32_t now, uint32_t replayFrom)
{
    if (replayFrom) {
        stats.replayRequestsSent++;
        LOG_INFO("DMShellTest t=%u missing_requested seq=%u", (unsigned)now, (unsigned)replayFrom);
    }
    sendFrame(meshtastic_RemoteShell_OpCode_ACK, now, nullptr, 0, false, 0, replayFrom ? replayFrom - 1 : 0);
}

void DMShellTestModule::transmit(const meshtastic_RemoteShell &frame, uint32_t now)
{
    (void)now;
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        LOG_WARN("DMShellTest: no packet for %s seq=%u", opName(frame.op), (unsigned)frame.seq);
        return;
    }
    const size_t encoded =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), meshtastic_RemoteShell_fields, &frame);
    if (encoded == 0) {
        LOG_WARN("DMShellTest: failed to encode %s seq=%u", opName(frame.op), (unsigned)frame.seq);
        packetPool.release(p);
        return;
    }
    p->decoded.payload.size = encoded;
    // As the client's packets go out: direct, no routing ACK, encrypted to the peer's key so the server authorises us
    p->to = DMSHELL_TEST_PEER;
    p->hop_limit = 0;
    p->hop_start = 0;
    p->channel = 0;
    p->want_ack = false;
    p->pki_encrypted = true;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    p->slot_parity = SLOT_PARITY;
    // Anything we send carries our receive cursor in ack_seq
    if (frame.ack_seq == rxWindow.lastInOrder())
        framesSinceOutbound = 0;
    stats.txFramesTotal++;
    stats.txPayloadBytes += frame.payload.size;
    switch (frame.op) {
    case meshtastic_RemoteShell_OpCode_INPUT:
        stats.txInput++;
        break;
    case meshtastic_RemoteShell_OpCode_ACK:
        stats.txAck++;
        break;
    case meshtastic_RemoteShell_OpCode_PING:
        stats.txPing++;
        break;
    case meshtastic_RemoteShell_OpCode_OPEN:
        stats.txOpen++;
        break;
    case meshtastic_RemoteShell_OpCode_CLOSE:
        stats.txClose++;
        break;
    default:
        break;
    }
    service->sendToMesh(p);
}

DMShellTestModule::SentFrame *DMShellTestModule::findSent(uint32_t seq)
{
    if (seq == 0)
        return nullptr;
    SentFrame &slot = history[seq % HISTORY_LEN];
    return slot.valid && slot.frame.seq == seq ? &slot : nullptr;
}

void DMShellTestModule::logStats(uint32_t now)
{
    // Short lines: the board's log buffer is 160 bytes and truncates a longer one
    const unsigned id = (unsigned)sessionId;
    const uint32_t elapsedMs = now - (openedAtMs ? openedAtMs : sessionStartMs);
    // Everything below the window's cursor was delivered, whether it arrived in order or was drained from the reorder
    // buffer; rxNewInOrder counts only the former
    const uint32_t delivered = rxWindow.lastInOrder();
    const uint32_t neverDelivered = peerHighestSeq > delivered ? peerHighestSeq - delivered : 0;
    LOG_INFO("DMShellTest stats session=0x%08x elapsed_ms=%u peer_highest_seq=%u keystrokes=%u ack_latency_ms=%u", id,
             (unsigned)elapsedMs, (unsigned)peerHighestSeq, (unsigned)keystrokesTyped, (unsigned)ackLatency.estimateMs());
    LOG_INFO("DMShellTest stats session=0x%08x inbound frames_arrived=%u delivered_in_order=%u duplicates=%u ahead_of_cursor=%u",
             id, (unsigned)stats.rxFramesTotal, (unsigned)stats.rxNewInOrder, (unsigned)stats.rxDuplicate,
             (unsigned)stats.rxAheadOfCursor);
    LOG_INFO("DMShellTest stats session=0x%08x inbound held_for_gap=%u dropped_reorder_full=%u never_delivered=%u", id,
             (unsigned)stats.rxHeldForGap, (unsigned)stats.rxDroppedReorderFull, (unsigned)neverDelivered);
    LOG_INFO("DMShellTest stats session=0x%08x inbound output_bytes=%u other_session=%u", id, (unsigned)stats.rxOutputBytes,
             (unsigned)stats.rxOtherSession);
    LOG_INFO("DMShellTest stats session=0x%08x outbound frames_transmitted=%u frames_originated=%u resends=%u", id,
             (unsigned)stats.txFramesTotal, (unsigned)(nextTxSeq - 1), (unsigned)stats.framesResent);
    LOG_INFO("DMShellTest stats session=0x%08x outbound input_retransmits=%u payload_bytes=%u", id,
             (unsigned)stats.inputRetransmits, (unsigned)stats.txPayloadBytes);
    LOG_INFO("DMShellTest stats session=0x%08x outbound INPUT=%u ACK=%u PING=%u OPEN=%u CLOSE=%u", id, (unsigned)stats.txInput,
             (unsigned)stats.txAck, (unsigned)stats.txPing, (unsigned)stats.txOpen, (unsigned)stats.txClose);
    LOG_INFO("DMShellTest stats session=0x%08x recovery replay_requests_sent=%u replays_sent_to_peer=%u", id,
             (unsigned)stats.replayRequestsSent, (unsigned)stats.replaysSentToPeer);
    LOG_INFO("DMShellTest stats session=0x%08x recovery replay_unavailable=%u replay_evicted=%u open_retries=%u", id,
             (unsigned)stats.replayUnavailable, (unsigned)stats.replayEvicted, (unsigned)stats.openRetries);
    LOG_INFO("DMShellTest stats session=0x%08x recovery input_window_closed=%u input_dropped=%u", id,
             (unsigned)stats.inputWindowClosed, (unsigned)stats.pendingInputDropped);
}

#endif
