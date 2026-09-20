#include "DMShell.h"

#if defined(ARCH_PORTDUINO)

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

/// DMSHELL_LEGACY_RECOVERY=1 restores the pre-damping behaviour on the same build, so a session
/// can be measured with and without the fix without reflashing.
bool legacyRecoveryRequested()
{
    const char *value = getenv("DMSHELL_LEGACY_RECOVERY");
    return value && *value && strcmp(value, "0") != 0;
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
        if (session.active && frame.session_id == session.sessionId && getFrom(&mp) == session.peer && frame.last_rx_seq > 0) {
            resendFramesFrom(frame.last_rx_seq + 1);
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
        if (!openSession(mp, frame)) {
            sendError("open_failed", getFrom(&mp));
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
        sendError("invalid_session", getFrom(&mp));
        return ProcessMessage::STOP;
    }

    // A teardown must not sit behind a gap. The session checks above have already established that
    // this is our peer on our session, and sequence state is discarded by the close anyway, so act on
    // it before the ordering check rather than asking for a replay and holding the shell open until
    // the idle timeout. The client applies the same rule to CLOSED.
    if (frame.op == meshtastic_RemoteShell_OpCode_CLOSE) {
        closeSession("peer_close", true);
        return ProcessMessage::STOP;
    }

    if (!shouldProcessIncomingFrame(frame)) {
        return ProcessMessage::STOP;
    }

    session.lastActivityMs = millis();

    switch (frame.op) {
    case meshtastic_RemoteShell_OpCode_INPUT:
        if (!writeSessionInput(frame)) {
            sendError("input_write_failed");
        } else {
            uint8_t outBuf[MAX_MESSAGE_SIZE];
            const ssize_t bytesRead = read(session.masterFd, outBuf, sizeof(outBuf));
            if (bytesRead > 0) {
                LOG_WARN("DMShell: read %zd bytes from PTY", bytesRead);
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
        uint32_t peerLastRxSeq = frame.ack_seq;
        if (frame.last_rx_seq > 0) {
            peerLastRxSeq = frame.last_rx_seq;
        }

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

    return ProcessMessage::STOP;
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

    if (Throttle::isWithinTimespanMs(session.lastActivityMs, SESSION_IDLE_TIMEOUT_MS) == false) {
        closeSession("idle_timeout", true);
        return 100;
    }

    // Null on a node whose radio is not RadioLib-backed (--sim, SerialHal): nothing to throttle against.
    if (RadioLibInterface::instance != nullptr && RadioLibInterface::instance->packetsInTxQueue() > 1) {
        return 50;
    }

    uint8_t outBuf[MAX_MESSAGE_SIZE];
    while (session.masterFd >= 0) {
        const ssize_t bytesRead = read(session.masterFd, outBuf, sizeof(outBuf));
        if (bytesRead > 0) {
            LOG_WARN("DMShell: read %zd bytes from PTY", bytesRead);

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

    return 100;
}

bool DMShellModule::parseFrame(const meshtastic_MeshPacket &mp, meshtastic_RemoteShell &outFrame)
{
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        return false;
    }

    if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_RemoteShell_fields, &outFrame)) {
        LOG_INFO("Received a DMShell message");
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
    session.txHistoryWindow.reset();
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

bool DMShellModule::writeSessionInput(const meshtastic_RemoteShell &frame)
{
    if (session.masterFd < 0) {
        return false;
    }
    if (frame.payload.size == 0) {
        return true;
    }

    const ssize_t bytesWritten = write(session.masterFd, frame.payload.bytes, frame.payload.size);
    return bytesWritten >= 0;
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

        pendingChildPid = session.childPid;
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

void DMShellModule::processPendingChildReap()
{
    if (pendingChildPid <= 0) {
        return;
    }

    int status = 0;
    const pid_t result = waitpid(pendingChildPid, &status, WNOHANG);

    if (result == pendingChildPid || (result < 0 && errno == ECHILD)) {
        pendingChildPid = -1;
        return;
    }

    if (result < 0) {
        LOG_WARN("DMShell: waitpid failed for pid=%d errno=%d", pendingChildPid, errno);
        pendingChildPid = -1;
        return;
    }

    if (pendingChildPid > 0) {
        if (kill(pendingChildPid, SIGKILL) < 0 && errno != ESRCH) {
            LOG_WARN("DMShell: failed to send SIGKILL to pid=%d errno=%d", pendingChildPid, errno);
        }
        pendingChildPid = -1;
    }
}

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
    if (frame.payload.size > 0) {
        memcpy(entry.payload, frame.payload.bytes, frame.payload.size);
    }

    session.txHistoryNext = (session.txHistoryNext + 1) % session.txHistory.size();
    session.txHistoryWindow.noteStored(frame.seq);
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

    DMShellSession::SentFrame *match = nullptr;
    for (auto &entry : session.txHistory) {
        if (!entry.valid || entry.seq != startSeq) {
            continue;
        }
        match = &entry;
        break;
    }

    if (!match) {
        LOG_WARN("DMShell: replay request for seq=%u not found in history", startSeq);
        return;
    }

    LOG_INFO("DMShell: replaying frame seq=%u op=%d", match->seq, match->op);
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

bool DMShellModule::shouldProcessIncomingFrame(const meshtastic_RemoteShell &frame)
{
    const DMShellRxDecision decision = session.rxWindow.classify(frame.seq, millis(), replayRequestIntervalMs());

    if (decision.requestReplay) {
        sendReplayRequest(decision.replaySeq);
    }

    if (decision.process && frame.seq != 0) {
        session.lastAckedRxSeq = frame.seq;
    }

    return decision.process;
}

void DMShellModule::sendFrameToPeer(NodeNum peer, meshtastic_RemoteShell frame, bool remember)
{
    meshtastic_MeshPacket *packet = allocDataPacket();
    if (!packet) {
        return;
    }
    LOG_WARN("DMShell: building packet op=%u session=0x%x seq=%u payloadLen=%zu", frame.op, frame.session_id, frame.seq,
             frame.payload.size);
    const size_t encoded = pb_encode_to_bytes(packet->decoded.payload.bytes, sizeof(packet->decoded.payload.bytes),
                                              meshtastic_RemoteShell_fields, &frame);
    if (encoded == 0) {
        return;
    }
    packet->decoded.payload.size = encoded;

    if (remember) {
        rememberSentFrame(frame);
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
#endif