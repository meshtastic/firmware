#include "DMShell.h"

#if defined(ARCH_PORTDUINO)

#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Throttle.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/mesh-pb-constants.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

// --- Half-duplex turn-taking ("talking stick") protocol, carried in RemoteShell.flags ---
// On a 2-party LoRa link, Meshtastic's CSMA-CA breaks down when both ends transmit at once
// (synchronized same-slot collisions that CAD can't prevent). These flags let exactly one
// side transmit at a time, eliminating those collisions. The client is the master/idle-owner.
constexpr uint32_t TURN_FLAG_GRANT = 0x01; // I am handing you the turn; you may transmit now
constexpr uint32_t TURN_FLAG_MORE = 0x02;  // I yielded under a budget but still have data queued
constexpr uint32_t TURN_FLAG_RTS = 0x04;   // I have output but no turn; please grant me one
constexpr size_t TURN_BUDGET_FRAMES = 8;   // max output frames per granted turn before yielding (bounds interrupt latency)
constexpr uint32_t RTS_RETRY_MS = 250;     // min interval between request-to-send frames
// After being granted the turn we keep it for a short "linger" window, continuing to drain shell
// output as it appears, instead of yielding the instant the PTY drains. This lets a command's
// output (and the next prompt) ride the same turn as the keystroke that triggered it, avoiding a
// full RTS->grant round-trip per command. The turn still ends promptly once the PTY is idle this
// long, or once TURN_BUDGET_FRAMES is hit (so the client can interject, e.g. Ctrl-C).
constexpr uint32_t TURN_LINGER_MS = 120;
} // namespace

DMShellModule::DMShellModule()
    : SinglePortModule("DMShellModule", meshtastic_PortNum_REMOTE_SHELL_APP), concurrency::OSThread("DMShell", 100)
{
    LOG_WARN("DMShell enabled on Portduino: remote shell access is dangerous and intended for trusted debugging only");
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
            LOG_WARN("DMShell: Received ack from 0x%x 0x%x", getFrom(&mp), session.peer);
            applyTurnFlags(frame);
            if (frame.last_rx_seq > 0) {
                resendFramesFrom(frame.last_rx_seq + 1);
            }
            // A standalone grant (client re-granting for MORE, replying to our RTS, or a heartbeat
            // poll) is our cue to flush any pending shell output during this turn.
            serviceTurn();
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

    // Honor channel-access flags before ordering checks: a GRANT transfers the turn regardless of
    // whether this frame's payload is in order.
    applyTurnFlags(frame);

    if (!shouldProcessIncomingFrame(frame)) {
        // We won't process the payload (gap/duplicate), but we may now hold the turn, so flush
        // output and/or hand it back rather than stalling the link.
        serviceTurn();
        return ProcessMessage::STOP;
    }

    session.lastActivityMs = millis();

    switch (frame.op) {
    case meshtastic_RemoteShell_OpCode_INPUT:
        if (!writeSessionInput(frame)) {
            sendError("input_write_failed");
        } else if (!session.turnManaged) {
            // Legacy peer (no turn-taking): echo immediately as before. In managed mode the
            // serviceTurn() call at the end of handleReceived drains the echo and yields the turn.
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
    case meshtastic_RemoteShell_OpCode_CLOSE:
        closeSession("peer_close", true);
        break;
    default:
        sendError("unsupported_op");
        break;
    }

    // If the peer granted us the turn, flush pending shell output and hand the turn back.
    serviceTurn();

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

    if (RadioLibInterface::instance->packetsInTxQueue() > 1) {
        return 50;
    }

    if (session.turnManaged) {
        if (session.hasToken) {
            // We hold the turn: flush output and hand it back.
            serviceTurn();
        } else if (ptyHasOutput() && !Throttle::isWithinTimespanMs(session.lastRtsMs, RTS_RETRY_MS)) {
            // Unsolicited shell output but no turn: ask the client to grant us one.
            sendRts();
            session.lastRtsMs = millis();
        }
        return 50;
    }

    // Legacy free-send path (peer is not using turn-taking).
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
    session.nextExpectedRxSeq = frame.seq + 1;
    session.highestSeenRxSeq = frame.seq;
    session.lastActivityMs = millis();
    session.turnManaged = true;
    session.hasToken = false;
    session.lastRtsMs = 0;

    // Honor any GRANT the client put on OPEN (opts this session into turn-taking).
    applyTurnFlags(frame);

    meshtastic_RemoteShell newFrame = {
        .op = meshtastic_RemoteShell_OpCode_OPEN_OK,
        .session_id = session.sessionId,
        .seq = session.nextTxSeq++,
        .ack_seq = frame.seq,
        .cols = ws.ws_col,
        .rows = ws.ws_row,
        .flags = session.turnManaged ? TURN_FLAG_GRANT : 0u,
    };
    newFrame.payload.size = 0;
    sendFrameToPeer(session.peer, newFrame, true);
    if (session.turnManaged) {
        // OPEN_OK handed the turn back to the client; it is now the idle-owner.
        session.hasToken = false;
    }

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
}

void DMShellModule::resendFramesFrom(uint32_t startSeq)
{
    if (startSeq == 0) {
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

void DMShellModule::sendAck(uint32_t replayFromSeq)
{
    if (replayFromSeq > 0) {
        LOG_WARN("DMShell: requesting replay from seq=%u", replayFromSeq);
    }
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
    if (frame.seq == 0) {
        return true;
    }

    if (frame.seq < session.nextExpectedRxSeq) {
        if (session.highestSeenRxSeq >= session.nextExpectedRxSeq) {
            sendAck(session.nextExpectedRxSeq);
        } else {
            sendAck();
        }
        return false;
    }

    if (frame.seq > session.nextExpectedRxSeq) {
        if (frame.seq > session.highestSeenRxSeq) {
            session.highestSeenRxSeq = frame.seq;
        }
        sendAck(session.nextExpectedRxSeq);
        return false;
    }

    session.lastAckedRxSeq = frame.seq;
    session.nextExpectedRxSeq = frame.seq + 1;
    if (frame.seq > session.highestSeenRxSeq) {
        session.highestSeenRxSeq = frame.seq;
    }
    if (session.highestSeenRxSeq >= session.nextExpectedRxSeq) {
        sendAck(session.nextExpectedRxSeq);
    } else {
        session.highestSeenRxSeq = 0;
    }
    return true;
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

void DMShellModule::applyTurnFlags(const meshtastic_RemoteShell &frame)
{
    if (frame.flags & (TURN_FLAG_GRANT | TURN_FLAG_MORE | TURN_FLAG_RTS)) {
        session.turnManaged = true; // peer speaks turn-taking; enable gating for this session
    }
    if (frame.flags & TURN_FLAG_GRANT) {
        if (!session.hasToken) {
            // Fresh turn: start the linger window and reset the per-turn budget.
            session.turnDeadlineMs = millis() + TURN_LINGER_MS;
            session.turnFramesSent = 0;
        }
        session.hasToken = true;
    }
}

bool DMShellModule::ptyHasOutput()
{
    if (session.masterFd < 0) {
        return false;
    }
    struct pollfd pfd = {};
    pfd.fd = session.masterFd;
    pfd.events = POLLIN;
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}

void DMShellModule::sendOutputFrame(const uint8_t *data, size_t len, uint32_t extraFlags)
{
    meshtastic_RemoteShell frame = {
        .op = meshtastic_RemoteShell_OpCode_OUTPUT,
        .session_id = session.sessionId,
        .seq = session.nextTxSeq++,
        .ack_seq = session.lastAckedRxSeq,
        .cols = 0,
        .rows = 0,
        .flags = extraFlags,
    };
    assert(len <= sizeof(frame.payload.bytes));
    memcpy(frame.payload.bytes, data, len);
    frame.payload.size = len;
    sendFrameToPeer(session.peer, frame, true);
}

void DMShellModule::sendTurnGrant(bool more)
{
    meshtastic_RemoteShell frame = {
        .op = meshtastic_RemoteShell_OpCode_ACK,
        .session_id = session.sessionId,
        .seq = 0,
        .ack_seq = session.lastAckedRxSeq,
        .cols = 0,
        .rows = 0,
        .flags = TURN_FLAG_GRANT | (more ? TURN_FLAG_MORE : 0u),
        .last_rx_seq = 0,
    };
    frame.payload.size = 0;
    sendFrameToPeer(session.peer, frame, false);
}

void DMShellModule::sendRts()
{
    meshtastic_RemoteShell frame = {
        .op = meshtastic_RemoteShell_OpCode_ACK,
        .session_id = session.sessionId,
        .seq = 0,
        .ack_seq = session.lastAckedRxSeq,
        .cols = 0,
        .rows = 0,
        .flags = TURN_FLAG_RTS,
        .last_rx_seq = 0,
    };
    frame.payload.size = 0;
    sendFrameToPeer(session.peer, frame, false);
}

// Called (every tick) while we hold the turn. Drains available shell output and sends it
// immediately, then decides whether to keep the turn (linger, to catch output that is about to
// appear) or hand it back. The turn is yielded once the per-turn budget is hit (so the client can
// interject, e.g. Ctrl-C) or once the PTY has been idle past the linger window. Output frames go
// out as soon as they are read (no extra delay); the grant is a trailing ACK.
void DMShellModule::serviceTurn()
{
    if (!session.active || !session.turnManaged || !session.hasToken) {
        return;
    }

    uint8_t buf[MAX_MESSAGE_SIZE];
    bool eof = false;
    bool readError = false;

    // Drain whatever is available right now, up to the remaining per-turn budget.
    while (session.turnFramesSent < TURN_BUDGET_FRAMES && session.masterFd >= 0) {
        const ssize_t n = read(session.masterFd, buf, sizeof(buf));
        if (n > 0) {
            sendOutputFrame(buf, (size_t)n, 0u);
            session.turnFramesSent++;
            session.lastActivityMs = millis();
            session.turnDeadlineMs = millis() + TURN_LINGER_MS; // extend the linger while output flows
        } else if (n == 0) {
            eof = true;
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                readError = true;
            }
            break;
        }
    }

    if (eof || readError) {
        session.hasToken = false;
        sendTurnGrant(false);
        if (eof) {
            closeSession("pty_eof", true);
        } else {
            LOG_WARN("DMShell: PTY read error errno=%d", errno);
            closeSession("pty_read_error", true);
        }
        return;
    }

    const bool morePending = ptyHasOutput();

    if (session.turnFramesSent >= TURN_BUDGET_FRAMES) {
        // Hit the per-turn budget: yield so the client gets a chance to interject (e.g. Ctrl-C).
        session.hasToken = false;
        sendTurnGrant(morePending);
        return;
    }

    if (!morePending && (int32_t)(millis() - session.turnDeadlineMs) >= 0) {
        // PTY has been idle past the linger window: hand the turn back.
        session.hasToken = false;
        sendTurnGrant(false);
        return;
    }

    // Otherwise keep holding the turn: there is more to drain next pass, or we are lingering for
    // output that may be about to appear. runOnce re-enters serviceTurn on the next tick.
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