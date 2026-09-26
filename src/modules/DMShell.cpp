#include "DMShell.h"

#if defined(MESHTASTIC_HAS_DMSHELL)

#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Throttle.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/mesh-pb-constants.h"
#include "meshUtils.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

DMShellModule *dmShellModule;

namespace
{
constexpr uint16_t PTY_COLS_DEFAULT = 120;
constexpr uint16_t PTY_ROWS_DEFAULT = 40;
constexpr size_t MAX_MESSAGE_SIZE = 200;

// Bounds on the replay-request interval. The floor keeps a fast preset from degenerating into a
// request per inbound frame; the ceiling keeps a slow one from parking a recoverable gap for
// longer than a user will wait.
constexpr uint32_t REPLAY_REQUEST_MIN_MS = 250;
constexpr uint32_t REPLAY_REQUEST_MAX_MS = 10000;
// Covers our request reaching the peer and the replay coming back past contention on both legs.
constexpr uint32_t REPLAY_REQUEST_MARGIN_MS = 250;

/// Frames of unacknowledged output allowed in flight.
///
/// Four rather than one or two so the channel stays busy while an acknowledgement is in flight: the
/// peer acknowledges every second frame, so a smaller window makes the stream wait a round trip per
/// frame. It has to stay well under the replay ring, which is the whole point - a sender at most four
/// frames ahead of a gap can always still answer the replay request for it, at any preset, which is
/// what a bigger ring on one side of the link could never achieve.
///
/// The peer's acknowledgement interval must not exceed this, or the sender blocks with the window
/// full and the peer still waiting to accumulate frames, and the stream only advances when the peer's
/// idle heartbeat eventually fires.
constexpr uint32_t DEFAULT_TX_WINDOW_FRAMES = 4;
/// Above the replay ring there is nothing left to bound, so a larger value is a configuration error.
constexpr uint32_t MAX_TX_WINDOW_FRAMES = (uint32_t)DMShellSession::TX_HISTORY_LEN;

/// In-order frames the peer may send before we owe it a bare ACK carrying our receive cursor.
///
/// The peer bounds its own unacknowledged input the way we bound our output, so it needs our cursor
/// to make progress. Input that produces no output - a keystroke a program swallows, a password
/// prompt - would otherwise leave us silent until its window filled and latched. Anything we
/// originate carries the cursor already, so this only fires when we have nothing else to say.
///
/// Keeping it below the peer's window means a healthy stream never has to stall for one. It does not
/// have to be: a peer whose window shut early retransmits, and shouldProcessIncomingFrame answers a
/// duplicate with a bare ACK directly, so the pair recovers whatever the two numbers are.
constexpr uint32_t ACK_AFTER_RX_FRAMES = 2;

/// Consecutive retransmissions of one sequence number before the peer is declared gone.
///
/// Measured on hardware: in healthy operation the most any single sequence number needed was 11, and
/// a peer that had actually vanished reached 88 and was still climbing. 25 sits in the gap with wide
/// margin on both sides. It is a frame count rather than a duration on purpose - the retransmission
/// interval is already derived from the modem config, so this scales with the preset by itself,
/// giving about 11 s on ShortTurbo and 114 s on LongFast.
constexpr uint32_t DEFAULT_MAX_CONSECUTIVE_RETRANSMITS = 25;

/// DMSHELL_LEGACY_RECOVERY=1 restores the pre-damping behaviour on the same build, so a session
/// can be measured with and without the fix without reflashing.
bool legacyRecoveryRequested()
{
    const char *value = getenv("DMSHELL_LEGACY_RECOVERY");
    return value && *value && strcmp(value, "0") != 0;
}

/// How many times the server repeats one unacknowledged frame before giving up on the peer.
/// DMSHELL_MAX_RETRANSMITS=0 restores the unbounded behaviour.
uint32_t maxRetransmitsFromEnv()
{
    const char *value = getenv("DMSHELL_MAX_RETRANSMITS");
    if (!value || !*value) {
        return DEFAULT_MAX_CONSECUTIVE_RETRANSMITS;
    }
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end == value) {
        LOG_WARN("DMShell: ignoring unparseable DMSHELL_MAX_RETRANSMITS=%s", value);
        return DEFAULT_MAX_CONSECUTIVE_RETRANSMITS;
    }
    return (uint32_t)parsed; // 0 disables the bound
}

/// How many frames of unacknowledged data the server will keep in flight. DMSHELL_TX_WINDOW=0
/// restores the unbounded behaviour, so the bound can be measured with and without on one build.
uint32_t txWindowFromEnv()
{
    const char *value = getenv("DMSHELL_TX_WINDOW");
    if (!value || !*value) {
        return DEFAULT_TX_WINDOW_FRAMES;
    }
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || parsed > MAX_TX_WINDOW_FRAMES) {
        // A window larger than the replay ring bounds nothing, so it is a configuration error rather
        // than a preference worth honouring.
        LOG_WARN("DMShell: ignoring DMSHELL_TX_WINDOW=%s, expected 0-%u", value, (unsigned)MAX_TX_WINDOW_FRAMES);
        return DEFAULT_TX_WINDOW_FRAMES;
    }
    return (uint32_t)parsed;
}
} // namespace

DMShellModule::DMShellModule()
    : SinglePortModule("DMShellModule", meshtastic_PortNum_REMOTE_SHELL_APP), concurrency::OSThread("DMShell", 100)
{
    LOG_WARN("DMShell enabled on Portduino: remote shell access is dangerous and intended for trusted debugging only");
    legacyRecovery = legacyRecoveryRequested();
    if (legacyRecovery) {
        LOG_WARN("DMShell: DMSHELL_LEGACY_RECOVERY set, replay-request damping and eviction reporting are OFF");
    }

    // Legacy mode means "reproduce the pre-fix behaviour", and the pre-fix behaviour was unbounded.
    maxConsecutiveRetransmits = maxRetransmitsFromEnv();
    txWindowFrames = legacyRecovery ? 0 : txWindowFromEnv();
    if (txWindowFrames == 0) {
        LOG_WARN("DMShell: outstanding-data window disabled, the sender may run away from a gap");
    } else {
        LOG_INFO("DMShell: bounding unacknowledged output to %u frames", (unsigned)txWindowFrames);
    }
}

/// How long to wait before asking for the same missing sequence number again.
///
/// One full frame's airtime is about 100 msec on ShortTurbo and over 2 sec on LongFast, so a fixed
/// interval either floods a slow preset with requests the peer has not had time to answer, or
/// leaves a fast one idle. Derive it from the modem config instead.
uint32_t DMShellModule::replayRequestIntervalMs() const
{
    if (legacyRecovery) {
        return 0; // no damping: one request per inbound frame, as before
    }

    uint32_t frameMsec = 0;
    if (RadioLibInterface::instance != nullptr) {
        // Null on a node whose radio is not RadioLib-backed (--sim, SerialHal): fall back to the floor.
        frameMsec = RadioLibInterface::instance->getPacketTime((uint32_t)MAX_LORA_PAYLOAD_LEN);
    }

    const uint32_t interval = 2 * frameMsec + REPLAY_REQUEST_MARGIN_MS;
    return clamp(interval, REPLAY_REQUEST_MIN_MS, REPLAY_REQUEST_MAX_MS);
}

ProcessMessage DMShellModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    meshtastic_RemoteShell frame = meshtastic_RemoteShell_init_zero;
    if (!mp.pki_encrypted) {
        LOG_WARN("DMShell: ignoring packet without PKI from 0x%x", mp.from);
        return ProcessMessage::STOP;
    }

    if (!parseFrame(mp, frame)) {
        LOG_WARN("DMShell: ignoring malformed frame");
        return ProcessMessage::STOP;
    }

    if (frame.op == meshtastic_RemoteShell_OpCode_ACK) {
        if (session.active && frame.session_id == session.sessionId && getFrom(&mp) == session.peer) {
            // A bare ACK carries no sequence number of its own and never reaches the ordered path
            // below, so this is the only place its receive cursor can be read. It is also the only
            // signal a peer with nothing to say can give us, and therefore the one that keeps the
            // window open during a one-way stream.
            notePeerReceiveCursor(frame);
            if (frame.last_rx_seq > 0) {
                resendFramesFrom(frame.last_rx_seq + 1);
            }
        }
        return ProcessMessage::CONTINUE;
    }

    if (frame.op >= 64) {
        LOG_WARN("DMShell: ignoring frame with op code %d, seq %d", frame.op, frame.seq);
        return ProcessMessage::CONTINUE;
    }

    if (!isAuthorizedPacket(mp)) {
        LOG_WARN("DMShell: unauthorized sender 0x%x, %u", mp.from, frame.op);
        myReply = allocErrorResponse(meshtastic_Routing_Error_NOT_AUTHORIZED, &mp);
        return ProcessMessage::STOP;
    }

    if (frame.op == meshtastic_RemoteShell_OpCode_OPEN) {
        LOG_WARN("DMShell: received OPEN from 0x%x sessionId=0x%x", mp.from, frame.session_id);
        const DMShellOpenAction action = legacyRecovery
                                             ? DMShellOpenAction::Open
                                             : classifyOpen(session.active, session.sessionId, session.peer, frame.session_id,
                                                            getFrom(&mp), session.txWindow.peerAcked());
        if (action == DMShellOpenAction::ResendOpenOk) {
            // OPEN_OK is always seq 1. The cursor check in classifyOpen() is what keeps it in the history.
            LOG_INFO("DMShell: repeated OPEN for session=0x%x, resending OPEN_OK", session.sessionId);
            resendFramesFrom(1);
        } else if (action == DMShellOpenAction::Ignore) {
            LOG_INFO("DMShell: late copy of OPEN for session=0x%x, peer already has OPEN_OK", session.sessionId);
        } else if (!openSession(mp, frame)) {
            sendSessionlessError("open_failed", getFrom(&mp), frame.session_id);
        }
        return ProcessMessage::STOP;
    }

    if (!session.active || frame.session_id != session.sessionId || getFrom(&mp) != session.peer) {
        if (!session.active) {
            LOG_WARN("DMShell: no active session, rejecting op %d from 0x%x", frame.op, mp.from);
        } else {
            LOG_WARN("DMShell: session ID mismatch (got 0x%x expected 0x%x) or peer mismatch (got 0x%x expected 0x%x), rejecting "
                     "op %d",
                     frame.session_id, session.sessionId, mp.from, session.peer, frame.op);
        }
        sendSessionlessError("invalid_session", getFrom(&mp), frame.session_id);
        return ProcessMessage::STOP;
    }

    // Both of these read the peer's state, not ours, so they belong here rather than past the ordering
    // gate below. An out-of-order frame is still proof the peer is alive, and still carries a valid
    // receive cursor - during a gap it may be the only kind of frame arriving, and the cursor is what
    // reopens our send window.
    session.lastActivityMs = millis();
    notePeerReceiveCursor(frame);

    // A teardown must not sit behind a gap. The session checks above have already established that
    // this is our peer on our session, and sequence state is discarded by the close anyway, so act on
    // it before the ordering check rather than asking for a replay and holding the shell open until
    // the idle timeout. The client applies the same rule to CLOSED.
    if (frame.op == meshtastic_RemoteShell_OpCode_CLOSE) {
        closeSession("peer_close", true);
        return ProcessMessage::STOP;
    }

    const DMShellRxDecision decision = classifyIncomingFrame(frame);
    if (!decision.process) {
        if (!decision.duplicate) {
            // Above the gap: it arrived intact, so hold it rather than making the peer send it again
            // once the gap fills. A duplicate is already past us, and seq 0 is never out of order.
            rememberOutOfOrderFrame(frame);
        }
        return ProcessMessage::STOP;
    }

    applySessionFrame(frame);
    drainBufferedFrames();
    return ProcessMessage::STOP;
}

/// Act on one in-order frame. Reached both from the radio and from the reorder buffer, so everything
/// here must be safe to run against a frame that arrived some time ago.
void DMShellModule::applySessionFrame(const meshtastic_RemoteShell &frame)
{
    switch (frame.op) {
    case meshtastic_RemoteShell_OpCode_INPUT:
        if (!writeSessionInput(frame)) {
            sendError("input_write_failed");
        } else if (!session.txWindow.canSend()) {
            // Same bound as runOnce's path. Skipping the read rather than dropping the frame leaves
            // the bytes in the PTY for runOnce to pick up once the window reopens.
            LOG_DEBUG("DMShell: window closed, deferring output after INPUT");
        } else {
            uint8_t outBuf[MAX_MESSAGE_SIZE];
            const ssize_t bytesRead = read(session.masterFd, outBuf, sizeof(outBuf));
            if (bytesRead > 0) {
                LOG_TRACE("DMShell: read %zd bytes from PTY", bytesRead);
                meshtastic_RemoteShell frame = {
                    .op = meshtastic_RemoteShell_OpCode_OUTPUT,
                    .session_id = session.sessionId,
                    .seq = session.nextTxSeq++,
                    .ack_seq = session.lastAckedRxSeq,
                    .cols = 0,
                    .rows = 0,
                    .flags = 0,
                };
                assert(bytesRead <= sizeof(frame.payload.bytes));
                memcpy(frame.payload.bytes, outBuf, bytesRead);
                frame.payload.size = bytesRead;
                sendFrameToPeer(session.peer, frame, true);
                session.lastActivityMs = millis();
            }
        }
        break;
    case meshtastic_RemoteShell_OpCode_RESIZE:
        if (frame.rows > 0 && frame.cols > 0) {
            struct winsize ws = {};
            ws.ws_row = frame.rows;
            ws.ws_col = frame.cols;
            if (session.masterFd >= 0) {
                ioctl(session.masterFd, TIOCSWINSZ, &ws);
            }
        }
        break;
    case meshtastic_RemoteShell_OpCode_PING: {
        // Same rule as notePeerReceiveCursor(): a stale last_rx_seq must not walk the cursor back
        // behind what the peer has already acknowledged, or we replay from a frame history has
        // dropped and close a session the peer is happy with.
        const uint32_t peerLastRxSeq = frame.last_rx_seq > frame.ack_seq ? frame.last_rx_seq : frame.ack_seq;

        const uint32_t nextMissingForPeer = peerLastRxSeq + 1;
        if (nextMissingForPeer > 0 && nextMissingForPeer < session.nextTxSeq) {
            resendFramesFrom(nextMissingForPeer);
            if (!session.active) {
                break; // the replay was unrecoverable and the session is gone
            }
        }

        meshtastic_RemoteShell frame = {
            .op = meshtastic_RemoteShell_OpCode_PONG,
            .session_id = session.sessionId,
            .seq = session.nextTxSeq++,
            .ack_seq = session.lastAckedRxSeq,
            .cols = 0,
            .rows = 0,
            .flags = 0,
            .last_tx_seq = session.nextTxSeq > 0 ? session.nextTxSeq - 1 : 0,
            .last_rx_seq = session.lastAckedRxSeq,
        };
        frame.payload.size = 0;
        sendFrameToPeer(session.peer, frame, true);
        break;
    }
    default:
        sendError("unsupported_op");
        break;
    }
}

int32_t DMShellModule::runOnce()
{
    processPendingChildReap();

    if (!session.active) {
        return 100;
    }

    reapChildIfExited();
    if (!session.active) {
        return 100;
    }

    // Anything the PTY would not take last time goes ahead of new input and of the read below.
    flushPendingWrite();

    if (Throttle::isWithinTimespanMs(session.lastActivityMs, SESSION_IDLE_TIMEOUT_MS) == false) {
        closeSession("idle_timeout", true);
        return 100;
    }

    // Null on a node whose radio is not RadioLib-backed (--sim, SerialHal): nothing to throttle against.
    if (RadioLibInterface::instance != nullptr && RadioLibInterface::instance->packetsInTxQueue() > 1) {
        return 50;
    }

    // Ahead of the window gate on purpose: while our own window is shut we retransmit, and a
    // retransmission carries a stale cursor, so a bare ACK is the only thing that keeps the peer's
    // window open when both directions are blocked at once. Deliberately limited to that case. With
    // the window open an OUTPUT frame carries the same cursor for free, so paying the debt here would
    // spend a frame on what the next one does anyway; that case is settled after the PTY read below.
    if (session.framesSinceOutbound >= ACK_AFTER_RX_FRAMES && !session.txWindow.canSend()) {
        sendBareAck();
        return 50;
    }

    if (!session.txWindow.canSend()) {
        // Window closed: leave the bytes in the PTY buffer, which is the backpressure. Deliberately
        // returning before the read also stops lastActivityMs being refreshed below, which is what
        // lets SESSION_IDLE_TIMEOUT_MS finally mean something - a peer that has vanished no longer
        // keeps us transmitting, because we stop and then time out.
        if (!session.txWindowBlocked) {
            session.txWindowBlocked = true;
            LOG_INFO("DMShell: window closed at %u unacknowledged frames, waiting for the peer",
                     (unsigned)session.txWindow.outstanding());
        }
        retransmitOldestUnacked();
        return 100;
    }

    if (session.txWindowBlocked) {
        session.txWindowBlocked = false;
        LOG_INFO("DMShell: window reopened, %u unacknowledged frames", (unsigned)session.txWindow.outstanding());
    }

    uint8_t outBuf[MAX_MESSAGE_SIZE];
    while (session.masterFd >= 0) {
        const ssize_t bytesRead = read(session.masterFd, outBuf, sizeof(outBuf));
        if (bytesRead > 0) {
            LOG_TRACE("DMShell: read %zd bytes from PTY", bytesRead);

            meshtastic_RemoteShell frame = {
                .op = meshtastic_RemoteShell_OpCode_OUTPUT,
                .session_id = session.sessionId,
                .seq = session.nextTxSeq++,
                .ack_seq = session.lastAckedRxSeq,
                .cols = 0,
                .rows = 0,
                .flags = 0,
            };
            assert(bytesRead <= sizeof(frame.payload.bytes));
            memcpy(frame.payload.bytes, outBuf, bytesRead);
            frame.payload.size = bytesRead;
            sendFrameToPeer(session.peer, frame, true);

            session.lastActivityMs = millis();
            // continue;
            // do we want to ack every data message, and only send the next on ack?
            // would require some retry logic. Maybe re-use the wantAck bit
            return 50;
        }

        if (bytesRead == 0) {
            closeSession("pty_eof", true);
            break;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        LOG_WARN("DMShell: PTY read error errno=%d", errno);
        closeSession("pty_read_error", true);
        break;
    }

    // Only reached when the PTY had nothing to carry the cursor: the send above returns directly, and
    // sendFrameToPeer() zeroes framesSinceOutbound because every frame carries ack_seq. So the debt
    // is paid by a frame of its own exactly when no frame of ours was going out anyway. The session
    // check matters because the loop breaks here after closeSession() on EOF or a read error.
    if (session.active && session.framesSinceOutbound >= ACK_AFTER_RX_FRAMES) {
        sendBareAck();
        return 50;
    }

    return 100;
}

bool DMShellModule::parseFrame(const meshtastic_MeshPacket &mp, meshtastic_RemoteShell &outFrame)
{
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        return false;
    }

    if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_RemoteShell_fields, &outFrame)) {
        // op and seq, because without them the log cannot answer the question that matters when a gap
        // will not close: did the peer's replay arrive and get discarded, or did it never arrive at
        // all. Logged before the session and ordering checks, so a frame rejected by either still
        // shows up here.
        LOG_INFO("Received a DMShell message op=%u seq=%u", outFrame.op, outFrame.seq);
    } else {
        LOG_ERROR("Error decoding DMShell message!");
        return false;
    }

    return true;
}

bool DMShellModule::isAuthorizedPacket(const meshtastic_MeshPacket &mp) const
{
    if (mp.from == 0) {
        return !config.security.is_managed;
    }

    const meshtastic_Channel *ch = &channels.getByIndex(mp.channel);
    if (strcasecmp(ch->settings.name, Channels::adminChannel) == 0) {
        return config.security.admin_channel_enabled;
    }

    if (mp.pki_encrypted) {
        for (uint8_t i = 0; i < 3; ++i) {
            if (config.security.admin_key[i].size == 32 &&
                memcmp(mp.public_key.bytes, config.security.admin_key[i].bytes, 32) == 0) {
                return true;
            }
        }
    }

    return false;
}

bool DMShellModule::openSession(const meshtastic_MeshPacket &mp, const meshtastic_RemoteShell &frame)
{
    if (session.active) {
        closeSession("preempted", false);
    }

    int masterFd = -1;
    struct winsize ws = {};
    if (frame.rows > 0) {
        ws.ws_row = frame.rows;
    } else {
        ws.ws_row = PTY_ROWS_DEFAULT;
    }
    if (frame.cols > 0) {
        ws.ws_col = frame.cols;
    } else {
        ws.ws_col = PTY_COLS_DEFAULT;
    }
    const pid_t childPid = forkpty(&masterFd, nullptr, nullptr, &ws);
    if (childPid < 0) {
        LOG_ERROR("DMShell: forkpty failed errno=%d", errno);
        return false;
    }

    if (childPid == 0) {
        const char *shell = getenv("SHELL");
        if (!shell || !*shell) {
            shell = "/bin/sh";
        }
        execl(shell, shell, "-i", static_cast<char *>(nullptr));
        _exit(127);
    }

    const int flags = fcntl(masterFd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);
    }

    session.active = true;
    session.sessionId = (frame.session_id != 0) ? frame.session_id : static_cast<uint32_t>(random(1, 0x7fffffff));
    session.peer = getFrom(&mp);
    session.channel = mp.channel;
    session.masterFd = masterFd;
    session.childPid = childPid;
    session.nextTxSeq = 1;
    session.lastAckedRxSeq = frame.seq;
    session.rxWindow.reset(frame.seq);
    session.rxReorder.reset();
    // Entries are matched on seq alone, and every session numbers from 1, so anything left over from the
    // previous session would answer for this one - a repeated OPEN could be sent the old OPEN_OK, whose
    // session id the client would then adopt.
    for (auto &entry : session.txHistory) {
        entry.valid = false;
    }
    session.txHistoryNext = 0;
    session.txHistoryWindow.reset();
    session.ackLatency.reset();
    session.txWindow.reset(txWindowFrames);
    session.txWindowBlocked = false;
    session.nextRetransmitMs = 0;
    session.retransmitRun.reset(maxConsecutiveRetransmits);
    session.framesSinceOutbound = 0;
    session.lastActivityMs = millis();

    meshtastic_RemoteShell newFrame = {
        .op = meshtastic_RemoteShell_OpCode_OPEN_OK,
        .session_id = session.sessionId,
        .seq = session.nextTxSeq++,
        .ack_seq = frame.seq,
        .cols = ws.ws_col,
        .rows = ws.ws_row,
        .flags = 0,
    };
    newFrame.payload.size = 0;
    sendFrameToPeer(session.peer, newFrame, true);

    LOG_INFO("DMShell: opened session=0x%x peer=0x%x pid=%d", session.sessionId, session.peer, session.childPid);
    return true;
}

/// Discard output the peer has already given up on, when its input carries an interrupt.
///
/// A local terminal does this for free: the line discipline flushes the output queue when it raises
/// SIGINT, unless NOFLSH is set. Over LoRa the same backlog has to be transmitted one frame at a time,
/// and the PTY holds about 15 KiB before a runaway child blocks on write - roughly 77 frames, about
/// 17 s of airtime on ShortTurbo and nearly three minutes on LongFast, all of it output the user has
/// just cancelled. Flush before writing the interrupt, so the shell's own response to it survives.
void DMShellModule::flushPendingOutputOnInterrupt(const meshtastic_RemoteShell &frame)
{
    struct termios tio = {};
    if (tcgetattr(session.masterFd, &tio) != 0 || (tio.c_lflag & ISIG) == 0) {
        return; // signals are not being generated, so nothing here would be cancelled
    }

    const cc_t intr = tio.c_cc[VINTR];
    if (intr == _POSIX_VDISABLE) {
        return;
    }

    for (size_t i = 0; i < frame.payload.size; i++) {
        if (frame.payload.bytes[i] != intr) {
            continue;
        }
        // TCIFLUSH from the master's point of view: data written by the child and not yet read by us.
        if (tcflush(session.masterFd, TCIFLUSH) == 0) {
            LOG_INFO("DMShell: interrupt in input, discarded pending shell output");
        }
        return;
    }
}

bool DMShellModule::writeSessionInput(const meshtastic_RemoteShell &frame)
{
    if (session.masterFd < 0) {
        return false;
    }
    if (frame.payload.size == 0) {
        return true;
    }

    flushPendingOutputOnInterrupt(frame);

    if (!flushPendingWrite()) {
        // The PTY is still backed up, so this frame queues behind what is already waiting.
        return queuePendingWrite(frame.payload.bytes, frame.payload.size);
    }

    const ssize_t bytesWritten = write(session.masterFd, frame.payload.bytes, frame.payload.size);
    if (bytesWritten < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return queuePendingWrite(frame.payload.bytes, frame.payload.size);
        }
        return false;
    }
    if ((size_t)bytesWritten < frame.payload.size) {
        // A nonblocking write may take less than it was given; the tail is ours to keep.
        return queuePendingWrite(frame.payload.bytes + bytesWritten, frame.payload.size - bytesWritten);
    }
    return true;
}

bool DMShellModule::flushPendingWrite()
{
    if (session.pendingWriteLen == 0) {
        return true;
    }
    if (session.masterFd < 0) {
        session.pendingWriteLen = 0;
        return true;
    }

    while (session.pendingWriteLen > 0) {
        const ssize_t bytesWritten = write(session.masterFd, session.pendingWrite, session.pendingWriteLen);
        if (bytesWritten <= 0) {
            return false;
        }
        session.pendingWriteLen -= (size_t)bytesWritten;
        if (session.pendingWriteLen > 0) {
            memmove(session.pendingWrite, session.pendingWrite + bytesWritten, session.pendingWriteLen);
        }
    }
    return true;
}

bool DMShellModule::queuePendingWrite(const uint8_t *bytes, size_t len)
{
    if (len == 0) {
        return true;
    }
    if (session.pendingWriteLen + len > sizeof(session.pendingWrite)) {
        LOG_WARN("DMShell: PTY write backlog full (%u queued, %u more)", (unsigned)session.pendingWriteLen, (unsigned)len);
        return false;
    }
    memcpy(session.pendingWrite + session.pendingWriteLen, bytes, len);
    session.pendingWriteLen += len;
    LOG_DEBUG("DMShell: PTY took a partial write, %u bytes queued", (unsigned)session.pendingWriteLen);
    return true;
}

void DMShellModule::closeSession(const char *reason, bool notifyPeer)
{
    if (!session.active) {
        return;
    }

    if (notifyPeer) {
        const size_t reasonLen = strnlen(reason, 256);
        meshtastic_RemoteShell frame = {
            .op = meshtastic_RemoteShell_OpCode_CLOSED,
            .session_id = session.sessionId,
            .seq = session.nextTxSeq++,
            .ack_seq = session.lastAckedRxSeq,
            .cols = 0,
            .rows = 0,
            .flags = 0,
        };
        assert(reasonLen <= sizeof(frame.payload.bytes));
        memcpy(frame.payload.bytes, reason, reasonLen);
        frame.payload.size = reasonLen;
        sendFrameToPeer(session.peer, frame, true);
    }

    if (session.masterFd >= 0) {
        close(session.masterFd);
        session.masterFd = -1;
    }

    if (session.childPid > 0) {
        // Run this to avoid forgetting a child
        processPendingChildReap();

        if (kill(session.childPid, SIGTERM) < 0 && errno != ESRCH) {
            LOG_WARN("DMShell: failed to send SIGTERM to pid=%d errno=%d", session.childPid, errno);
        }

        rememberPendingChild(session.childPid);
        session.childPid = -1;
    }

    LOG_INFO("DMShell: closed session=0x%x reason=%s", session.sessionId, reason);
    session = DMShellSession{};
}

void DMShellModule::reapChildIfExited()
{
    if (!session.active || session.childPid <= 0) {
        return;
    }

    int status = 0;
    const pid_t result = waitpid(session.childPid, &status, WNOHANG);
    if (result == session.childPid) {
        closeSession("shell_exited", true);
    }
}

/// Take a slot for a child that has been asked to go away, so its exit is collected later.
void DMShellModule::rememberPendingChild(pid_t pid)
{
    if (pid <= 0) {
        return;
    }

    // A slot may have come free since the last tick.
    processPendingChildReap();

    for (PendingChild &pending : pendingChildren) {
        if (pending.pid <= 0) {
            pending.pid = pid;
            pending.killed = false;
            return;
        }
    }

    // Every slot still holds a child that has not exited. Waiting here would stall the thread, so
    // the pid is lost and init will collect it; one warning, because it should not be reachable
    // with a single session at a time.
    LOG_WARN("DMShell: no slot left for pid=%d, dropping it", pid);
}

void DMShellModule::processPendingChildReap()
{
    for (PendingChild &pending : pendingChildren) {
        if (pending.pid <= 0) {
            continue;
        }

        int status = 0;
        const pid_t result = waitpid(pending.pid, &status, WNOHANG);

        if (result == pending.pid || (result < 0 && errno == ECHILD)) {
            pending = PendingChild{};
            continue;
        }

        if (result < 0) {
            LOG_WARN("DMShell: waitpid failed for pid=%d errno=%d", pending.pid, errno);
            pending = PendingChild{};
            continue;
        }

        // Still running. SIGKILL ends it but does not reap it, so the pid stays here until a later
        // waitpid() collects the corpse; clearing it now would leave a zombie behind.
        if (!pending.killed) {
            if (kill(pending.pid, SIGKILL) < 0 && errno != ESRCH) {
                LOG_WARN("DMShell: failed to send SIGKILL to pid=%d errno=%d", pending.pid, errno);
                pending = PendingChild{};
                continue;
            }
            pending.killed = true;
        }
    }
}

// Invariant, relied on by DMShellTxHistoryWindow: every sequence number drawn from nextTxSeq gets
// stored here exactly once, in order, so the retained range can be derived arithmetically rather than
// scanned. Anything that consumes a sequence number without being remembered breaks that - so a new
// send site passes remember=true, and an unsequenced frame uses seq=0 (as ACK does) rather than
// burning a number. Getting it wrong degrades to the pre-fix behaviour rather than corrupting
// anything: resendFramesFrom still scans the ring and falls through to "not found in history" if
// classify() was optimistic.
void DMShellModule::rememberSentFrame(meshtastic_RemoteShell frame)
{
    if (frame.seq == 0 || frame.op == meshtastic_RemoteShell_OpCode_ACK) {
        return;
    }

    auto &entry = session.txHistory[session.txHistoryNext];
    entry.valid = true;
    entry.op = frame.op;
    entry.sessionId = frame.session_id;
    entry.seq = frame.seq;
    entry.ackSeq = frame.ack_seq;
    entry.cols = frame.cols;
    entry.rows = frame.rows;
    entry.flags = frame.flags;
    entry.payloadLen = frame.payload.size;
    entry.lastSentMs = millis();
    if (frame.payload.size > 0) {
        memcpy(entry.payload, frame.payload.bytes, frame.payload.size);
    }

    session.txHistoryNext = (session.txHistoryNext + 1) % session.txHistory.size();
    session.txHistoryWindow.noteStored(frame.seq);
    session.txWindow.noteSent(frame.seq);
}

/// Feed the peer's cumulative receive cursor into the send window.
///
/// Both fields mean "the highest sequence number I have in order": ack_seq is set on every frame the
/// peer originates, and last_rx_seq is set explicitly when it asks for a replay. Taking the larger
/// tolerates a peer that populates only one of them; DMShellTxWindow clamps and refuses to regress.
void DMShellModule::notePeerReceiveCursor(const meshtastic_RemoteShell &frame)
{
    const uint32_t cursor = frame.last_rx_seq > frame.ack_seq ? frame.last_rx_seq : frame.ack_seq;
    const uint32_t before = session.txWindow.peerAcked();
    session.txWindow.notePeerAcked(cursor);
    if (session.txWindow.peerAcked() != before) {
        // The time from last sending the frame the cursor has just reached to seeing it acknowledged is
        // the whole wait a retransmission has to sit out: our queue, both frames' airtime, and both
        // sides' channel backoff. The client samples the same point.
        if (const DMShellSession::SentFrame *acked = findSentFrame(session.txWindow.peerAcked())) {
            session.ackLatency.noteSample(millis() - acked->lastSentMs);
        }
        // Progress: the next stall gets a fresh interval, and the run of retransmissions that would
        // eventually declare the peer gone starts over.
        session.nextRetransmitMs = 0;
        session.retransmitRun.clear();
    }
}

/// Retransmit the oldest frame the peer has not acknowledged, while the window is shut.
///
/// Recovery here was purely receiver-driven: the peer asked for a replay, and it only asked when a
/// frame arrived carrying a higher sequence number. Bounding the sender removed exactly that
/// stimulus. Once the window shuts the peer sees nothing new, so it never re-asks, and its cursor can
/// never advance past the gap that shut the window - a silent, permanent latch. Measured on hardware:
/// the session stopped after ~20 s with frames still arriving from the peer and the window never
/// reopening.
///
/// So the sender takes responsibility for its own unacknowledged data, which is the missing piece
/// the original analysis called out as "no sender-side retransmit timer". We know the peer's
/// cumulative cursor, so we know exactly which frame it is waiting for; resend that rather than
/// something new, because sending something new would push us further ahead of the gap, which is the
/// thing the window exists to prevent.
void DMShellModule::retransmitOldestUnacked()
{
    if (legacyRecovery) {
        return; // legacy mode has no window, so it never blocks here
    }

    const uint32_t missing = session.txWindow.peerAcked() + 1;
    if (missing == 0 || missing >= session.nextTxSeq) {
        return; // the peer is level with us; the window is shut for some other reason
    }

    if (session.nextRetransmitMs != 0 && !Throttle::deadlinePassedAt(millis(), session.nextRetransmitMs)) {
        return;
    }
    // Measured, floored at the round trip derived from the modem config, which is all there is until the
    // first acknowledgement arrives.
    const uint32_t intervalMs = session.ackLatency.intervalMs(replayRequestIntervalMs(), REPLAY_REQUEST_MAX_MS);
    // A cursor advance clears nextRetransmitMs, so without this the next unacknowledged frame - queued a
    // moment ago, and very likely still waiting for the radio - was repeated on this very poll. Measured
    // on hardware as a third of all frames arriving twice on a lossless bulk transfer. A frame that has
    // left the history carries no send time, and falls through to resendFramesFrom() as before.
    if (const DMShellSession::SentFrame *oldest = findSentFrame(missing)) {
        if (!retransmitDue(millis(), oldest->lastSentMs, intervalMs)) {
            return;
        }
    }
    session.nextRetransmitMs = millis() + intervalMs;

    // Measured on hardware, a sender that never stops repeating one frame is talking to a peer that
    // has gone away - its teardown did not survive - so the session is already over. The reason names
    // the peer rather than the link, because that is what the evidence shows.
    if (!session.retransmitRun.allowRetransmit(missing)) {
        LOG_ERROR("DMShell: peer has not acknowledged seq=%u after %u retransmissions, closing session", missing,
                  (unsigned)session.retransmitRun.repeatCount());
        closeSession("peer_unresponsive", true);
        return;
    }

    LOG_WARN("DMShell: window shut and peer still missing seq=%u, retransmitting (%u) interval=%ums ack_latency=%ums", missing,
             (unsigned)session.retransmitRun.repeatCount(), (unsigned)intervalMs, (unsigned)session.ackLatency.estimateMs());
    resendFramesFrom(missing);
}

DMShellSession::SentFrame *DMShellModule::findSentFrame(uint32_t seq)
{
    for (auto &entry : session.txHistory) {
        if (entry.valid && entry.seq == seq) {
            return &entry;
        }
    }
    return nullptr;
}

void DMShellModule::resendFramesFrom(uint32_t startSeq)
{
    if (startSeq == 0) {
        return;
    }

    // The ring is bounded in frames, so on a fast preset it can be outrun while a request is still
    // in flight. Once that happens the peer will never get this frame, and it will not advance past
    // the hole either - so it asks again forever. Say so and tear the session down instead.
    if (!legacyRecovery && session.txHistoryWindow.classify(startSeq, session.nextTxSeq) == DMShellReplayLookup::Evicted) {
        LOG_ERROR("DMShell: replay request for seq=%u aged out of the %u-frame history, closing session", startSeq,
                  (unsigned)DMShellSession::TX_HISTORY_LEN);
        // The CLOSED frame carries the reason, so no separate ERROR: a non-terminal ERROR has to stay
        // on the peer's ordered path, and this gap is exactly what it could not get past.
        closeSession("replay_evicted", true);
        return;
    }

    DMShellSession::SentFrame *match = findSentFrame(startSeq);
    if (!match) {
        LOG_WARN("DMShell: replay request for seq=%u not found in history", startSeq);
        return;
    }

    LOG_INFO("DMShell: replaying frame seq=%u op=%d", match->seq, match->op);
    // The next wait for this frame is measured from now, as the client does for its own resends.
    match->lastSentMs = millis();
    meshtastic_RemoteShell frame = {
        .op = match->op,
        .session_id = match->sessionId,
        .seq = match->seq,
        .ack_seq = match->ackSeq,
        .cols = match->cols,
        .rows = match->rows,
        .flags = match->flags,
    };
    assert(match->payloadLen <= sizeof(frame.payload.bytes));
    memcpy(frame.payload.bytes, match->payload, match->payloadLen);
    frame.payload.size = match->payloadLen;
    sendFrameToPeer(session.peer, frame, false);
}

void DMShellModule::sendReplayRequest(uint32_t replayFromSeq)
{
    if (replayFromSeq == 0) {
        // Would underflow last_rx_seq to 0xffffffff, which the peer reads as a replay request for a
        // sequence number it has never sent.
        return;
    }

    LOG_WARN("DMShell: requesting replay from seq=%u", replayFromSeq);
    meshtastic_RemoteShell frame = {
        .op = meshtastic_RemoteShell_OpCode_ACK,
        .session_id = session.sessionId,
        .seq = 0,
        .ack_seq = session.lastAckedRxSeq,
        .cols = 0,
        .rows = 0,
        .flags = 0,
        .last_rx_seq = replayFromSeq - 1,
    };
    frame.payload.size = 0;
    sendFrameToPeer(session.peer, frame, false);
}

DMShellRxDecision DMShellModule::classifyIncomingFrame(const meshtastic_RemoteShell &frame)
{
    const DMShellRxDecision decision = session.rxWindow.classify(frame.seq, millis(), replayRequestIntervalMs());

    // Asking for a frame we are already holding is pure channel load, and the ordering window cannot
    // know what the reorder buffer has - it deals only in sequence numbers it has passed to the
    // session. So the suppression belongs here, and it has to run on every classification, including
    // the ones made while draining the buffer.
    if (decision.requestReplay && !session.rxReorder.holds(decision.replaySeq)) {
        sendReplayRequest(decision.replaySeq);
    }

    if (decision.process && frame.seq != 0) {
        session.lastAckedRxSeq = frame.seq;
        // Only an in-order frame moves the cursor the peer is waiting on, so only this case creates a
        // debt. Mirrors the client's note_received_seq().
        session.framesSinceOutbound++;
    } else if (decision.duplicate && !decision.requestReplay) {
        // The peer is repeating a frame we already have, which means it has not seen our cursor - a
        // sender whose own window has shut looks exactly like this. Answering directly is what makes
        // any peer window size safe, rather than relying on ACK_AFTER_RX_FRAMES being the smaller of
        // the two numbers. A replay request carries the same cursor, so it counts as the answer; a
        // damped gap deliberately stays silent, which is what the damping is for.
        sendBareAck();
    }

    return decision;
}

/// Hold a frame that arrived above a gap, rather than discarding something that arrived intact.
void DMShellModule::rememberOutOfOrderFrame(const meshtastic_RemoteShell &frame)
{
    const int slot = session.rxReorder.claimSlotFor(frame.seq);
    if (slot < 0) {
        return; // already held, or the buffer is full of frames we need sooner
    }
    session.rxReorderFrames[slot] = frame;
    LOG_DEBUG("DMShell: holding out-of-order seq=%u, %u frame(s) buffered", frame.seq, (unsigned)session.rxReorder.count());
}

bool DMShellModule::takeBufferedFrame(uint32_t seq, meshtastic_RemoteShell &outFrame)
{
    const int slot = session.rxReorder.find(seq);
    if (slot < 0) {
        return false;
    }
    outFrame = session.rxReorderFrames[slot];
    // Freed before the caller applies it, so a frame can never be applied twice and the drain loop
    // always makes progress.
    session.rxReorder.release(slot);
    return true;
}

/// Deliver whatever the gap was holding back, in order, now that it has filled.
void DMShellModule::drainBufferedFrames()
{
    while (session.active && session.rxReorder.count() > 0) {
        meshtastic_RemoteShell next = meshtastic_RemoteShell_init_zero;
        if (!takeBufferedFrame(session.rxWindow.nextExpected(), next)) {
            return; // the next frame in order is still missing
        }
        if (!classifyIncomingFrame(next).process) {
            return; // cannot happen with a contiguous cursor, but never spin on it
        }
        applySessionFrame(next);
    }
}

void DMShellModule::sendFrameToPeer(NodeNum peer, meshtastic_RemoteShell frame, bool remember)
{
    // A sequenced frame that never reaches the air has to give its number back. The callers draw it
    // from nextTxSeq before we are called, so dropping out here would leave a hole no replay can
    // fill: the peer waits on a frame that was never remembered, and resendFramesFrom() ends the
    // session when it cannot find it. Only the number just drawn can be returned, which is exactly
    // the case here - a replay arrives with an older seq and does not qualify.
    auto returnSequence = [this, &frame]() {
        if (frame.seq != 0 && session.active && session.nextTxSeq == frame.seq + 1) {
            session.nextTxSeq = frame.seq;
        }
    };

    meshtastic_MeshPacket *packet = allocDataPacket();
    if (!packet) {
        LOG_WARN("DMShell: no packet available for op=%u seq=%u", frame.op, frame.seq);
        returnSequence();
        return;
    }
    LOG_TRACE("DMShell: building packet op=%u session=0x%x seq=%u payloadLen=%zu", frame.op, frame.session_id, frame.seq,
              frame.payload.size);
    const size_t encoded = pb_encode_to_bytes(packet->decoded.payload.bytes, sizeof(packet->decoded.payload.bytes),
                                              meshtastic_RemoteShell_fields, &frame);
    if (encoded == 0) {
        LOG_WARN("DMShell: failed to encode op=%u seq=%u", frame.op, frame.seq);
        packetPool.release(packet);
        returnSequence();
        return;
    }
    packet->decoded.payload.size = encoded;

    if (remember) {
        rememberSentFrame(frame);
    }

    // Anything carrying the current cursor settles the debt, whatever its opcode. A replay carries the
    // cursor it was first sent with, so a stale one deliberately does not.
    if (frame.ack_seq == session.lastAckedRxSeq) {
        session.framesSinceOutbound = 0;
    }

    packet->to = peer;
    packet->hop_limit = 0;
    packet->hop_start = 0;
    packet->channel = 0;
    packet->want_ack = false;
    packet->pki_encrypted = true;
    packet->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    service->sendToMesh(packet);
}

/// Tell the peer where our receive cursor is when we have nothing else to send.
///
/// seq 0 keeps it out of the peer's ordering window, and last_rx_seq stays 0 so the peer does not read
/// it as a replay request; ack_seq alone carries the cursor. Not remembered: it is unsequenced, so
/// there is nothing to replay.
void DMShellModule::sendBareAck()
{
    meshtastic_RemoteShell frame = {
        .op = meshtastic_RemoteShell_OpCode_ACK,
        .session_id = session.sessionId,
        .seq = 0,
        .ack_seq = session.lastAckedRxSeq,
        .cols = 0,
        .rows = 0,
        .flags = 0,
        .last_tx_seq = 0,
        .last_rx_seq = 0,
    };
    frame.payload.size = 0;
    sendFrameToPeer(session.peer, frame, false);
}

void DMShellModule::sendError(const char *message, NodeNum peer)
{
    const size_t len = strnlen(message, MAX_MESSAGE_SIZE);
    meshtastic_RemoteShell frame = {
        .op = meshtastic_RemoteShell_OpCode_ERROR,
        .session_id = session.sessionId,
        .seq = session.nextTxSeq++,
        .ack_seq = session.lastAckedRxSeq,
        .cols = 0,
        .rows = 0,
        .flags = 0,
    };
    if (message && len > 0) {
        assert(len <= sizeof(frame.payload.bytes));
        memcpy(frame.payload.bytes, message, len);
        frame.payload.size = len;
    }
    if (peer == 0) {
        peer = session.peer;
    }
    sendFrameToPeer(peer, frame, true);
}

/// A sequenced ERROR outside the session took the next sequence number. With no session that was seq 1, which a
/// client still waiting for its OPEN_OK took in order, and then dropped the real OPEN_OK (also seq 1) as a duplicate.
void DMShellModule::sendSessionlessError(const char *message, NodeNum peer, uint32_t sessionId)
{
    const size_t len = strnlen(message, MAX_MESSAGE_SIZE);
    meshtastic_RemoteShell frame = {
        .op = meshtastic_RemoteShell_OpCode_ERROR,
        .session_id = sessionId,
        .seq = 0,
        .ack_seq = 0,
        .cols = 0,
        .rows = 0,
        .flags = 0,
    };
    if (message && len > 0) {
        assert(len <= sizeof(frame.payload.bytes));
        memcpy(frame.payload.bytes, message, len);
        frame.payload.size = len;
    }
    sendFrameToPeer(peer, frame, false);
}
#endif