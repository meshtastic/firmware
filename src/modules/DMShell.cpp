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
constexpr size_t MAX_PTY_READ_SIZE = 200;

struct BytesDecodeState {
    uint8_t *buf;
    size_t maxLen;
    size_t outLen;
};

struct BytesEncodeState {
    const uint8_t *buf;
    size_t len;
};

bool decodeBytesField(pb_istream_t *stream, const pb_field_iter_t *field, void **arg)
{
    (void)field;
    auto *state = static_cast<BytesDecodeState *>(*arg);
    const size_t fieldLen = stream->bytes_left;
    if (fieldLen > state->maxLen) {
        return false;
    }
    if (!pb_read(stream, state->buf, fieldLen)) {
        return false;
    }
    state->outLen = fieldLen;
    return true;
}

bool encodeBytesField(pb_ostream_t *stream, const pb_field_iter_t *field, void *const *arg)
{
    auto *state = static_cast<const BytesEncodeState *>(*arg);
    if (!state || !state->buf || state->len == 0) {
        return true;
    }
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }
    return pb_encode_string(stream, state->buf, state->len);
}
} // namespace

DMShellModule::DMShellModule()
    : SinglePortModule("DMShellModule", meshtastic_PortNum_DM_SHELL_APP), concurrency::OSThread("DMShell", 100)
{
    LOG_WARN("DMShell enabled on Portduino: remote shell access is dangerous and intended for trusted debugging only");
}

ProcessMessage DMShellModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    DMShellFrame frame;
    if (!parseFrame(mp, frame)) {
        LOG_WARN("DMShell: ignoring malformed frame");
        return ProcessMessage::STOP;
    }

    if (frame.op >= 64) {
        LOG_WARN("DMShell: ignoring frame with op code %d", frame.op);
        return ProcessMessage::CONTINUE;
    }

    if (!isAuthorizedPacket(mp)) {
        LOG_WARN("DMShell: unauthorized sender 0x%x", mp.from);
        myReply = allocErrorResponse(meshtastic_Routing_Error_NOT_AUTHORIZED, &mp);
        return ProcessMessage::STOP;
    }

    if (frame.op == meshtastic_DMShell_OpCode_OPEN) {
        LOG_WARN("DMShell: received OPEN from 0x%x sessionId=0x%x", mp.from, frame.sessionId);
        if (!openSession(mp, frame)) {
            const char *msg = "open_failed";
            sendFrameToPeer(getFrom(&mp), mp.channel, meshtastic_DMShell_OpCode_ERROR, frame.sessionId, frame.seq,
                            reinterpret_cast<const uint8_t *>(msg), strlen(msg));
        }
        return ProcessMessage::STOP;
    }

    if (!session.active || frame.sessionId != session.sessionId || getFrom(&mp) != session.peer) {
        const char *msg = "invalid_session";
        sendFrameToPeer(getFrom(&mp), mp.channel, meshtastic_DMShell_OpCode_ERROR, frame.sessionId, frame.seq,
                        reinterpret_cast<const uint8_t *>(msg), strlen(msg));
        return ProcessMessage::STOP;
    }

    session.lastActivityMs = millis();

    switch (frame.op) {
    case meshtastic_DMShell_OpCode_INPUT:
        if (!writeSessionInput(frame)) {
            sendError("input_write_failed");
        }
        break;
    case meshtastic_DMShell_OpCode_RESIZE:
        if (frame.rows > 0 && frame.cols > 0) {
            struct winsize ws = {};
            ws.ws_row = frame.rows;
            ws.ws_col = frame.cols;
            if (session.masterFd >= 0) {
                ioctl(session.masterFd, TIOCSWINSZ, &ws);
            }
        }
        break;
    case meshtastic_DMShell_OpCode_PING:
        sendControl(meshtastic_DMShell_OpCode_PONG, nullptr, 0, frame.seq);
        break;
    case meshtastic_DMShell_OpCode_CLOSE:
        closeSession("peer_close", true);
        break;
    default:
        sendError("unsupported_op");
        break;
    }

    return ProcessMessage::STOP;
}

int32_t DMShellModule::runOnce()
{
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

    uint8_t outBuf[MAX_PTY_READ_SIZE];
    while (session.masterFd >= 0) {
        const ssize_t bytesRead = read(session.masterFd, outBuf, sizeof(outBuf));
        if (bytesRead > 0) {
            LOG_WARN("DMShell: read %d bytes from PTY", bytesRead);
            sendControl(meshtastic_DMShell_OpCode_OUTPUT, outBuf, static_cast<size_t>(bytesRead), session.txSeq++);
            session.lastActivityMs = millis();
            continue;
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

bool DMShellModule::parseFrame(const meshtastic_MeshPacket &mp, DMShellFrame &outFrame)
{
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        return false;
    }

    meshtastic_DMShell decodedMsg = meshtastic_DMShell_init_zero;
    if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_DMShell_fields, &decodedMsg)) {
        LOG_INFO("Received a DMShell message");
    } else {
        LOG_ERROR("Error decoding DMShell message!");
        return false;
    }

    outFrame.op = decodedMsg.op;
    outFrame.sessionId = decodedMsg.session_id;
    outFrame.seq = decodedMsg.seq;
    outFrame.ackSeq = decodedMsg.ack_seq;
    outFrame.cols = decodedMsg.cols;
    outFrame.rows = decodedMsg.rows;
    outFrame.flags = decodedMsg.flags;
    outFrame.payloadLen = decodedMsg.payload.size;
    memcpy(outFrame.payload, decodedMsg.payload.bytes, outFrame.payloadLen);

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

bool DMShellModule::openSession(const meshtastic_MeshPacket &mp, const DMShellFrame &frame)
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
    session.sessionId = (frame.sessionId != 0) ? frame.sessionId : static_cast<uint32_t>(random(1, 0x7fffffff));
    session.peer = getFrom(&mp);
    session.channel = mp.channel;
    session.masterFd = masterFd;
    session.childPid = childPid;
    session.txSeq = 0;
    session.lastActivityMs = millis();

    uint8_t payload[sizeof(uint32_t)] = {0};
    uint32_t pidBE = (static_cast<uint32_t>(session.childPid) << 24) | ((static_cast<uint32_t>(session.childPid) >> 8) & 0xff00) |
                     ((static_cast<uint32_t>(session.childPid) << 8) & 0xff0000) |
                     (static_cast<uint32_t>(session.childPid) >> 24);
    memcpy(payload, &pidBE, sizeof(payload));
    sendFrameToPeer(session.peer, session.channel, meshtastic_DMShell_OpCode_OPEN_OK, session.sessionId, frame.seq, payload,
                    sizeof(payload), ws.ws_col, ws.ws_row);

    LOG_INFO("DMShell: opened session=0x%x peer=0x%x pid=%d", session.sessionId, session.peer, session.childPid);
    return true;
}

bool DMShellModule::writeSessionInput(const DMShellFrame &frame)
{
    if (session.masterFd < 0) {
        return false;
    }
    if (frame.payloadLen == 0) {
        return true;
    }

    const ssize_t bytesWritten = write(session.masterFd, frame.payload, frame.payloadLen);
    return bytesWritten >= 0;
}

void DMShellModule::closeSession(const char *reason, bool notifyPeer)
{
    if (!session.active) {
        return;
    }

    if (notifyPeer) {
        const size_t reasonLen = strnlen(reason, 256);
        sendControl(meshtastic_DMShell_OpCode_CLOSED, reinterpret_cast<const uint8_t *>(reason), reasonLen, session.txSeq++);
    }

    if (session.masterFd >= 0) {
        close(session.masterFd);
        session.masterFd = -1;
    }

    if (session.childPid > 0) {
        kill(session.childPid, SIGTERM);
        int status = 0;
        waitpid(session.childPid, &status, WNOHANG);
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

void DMShellModule::sendControl(meshtastic_DMShell_OpCode op, const uint8_t *payload, size_t payloadLen, uint32_t seq)
{
    sendFrameToPeer(session.peer, session.channel, op, session.sessionId, seq, payload, payloadLen);
}

void DMShellModule::sendFrameToPeer(NodeNum peer, uint8_t channel, meshtastic_DMShell_OpCode op, uint32_t sessionId, uint32_t seq,
                                    const uint8_t *payload, size_t payloadLen, uint32_t cols, uint32_t rows, uint32_t ackSeq,
                                    uint32_t flags)
{
    meshtastic_MeshPacket *packet = buildFramePacket(op, sessionId, seq, payload, payloadLen, cols, rows, ackSeq, flags);
    if (!packet) {
        return;
    }

    packet->to = peer;
    packet->channel = channel;
    packet->want_ack = false;
    packet->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    service->sendToMesh(packet);
}

void DMShellModule::sendError(const char *message)
{
    const size_t len = strnlen(message, meshtastic_Constants_DATA_PAYLOAD_LEN);
    sendControl(meshtastic_DMShell_OpCode_ERROR, reinterpret_cast<const uint8_t *>(message), len, session.txSeq++);
}

meshtastic_MeshPacket *DMShellModule::buildFramePacket(meshtastic_DMShell_OpCode op, uint32_t sessionId, uint32_t seq,
                                                       const uint8_t *payload, size_t payloadLen, uint32_t cols, uint32_t rows,
                                                       uint32_t ackSeq, uint32_t flags)
{
    meshtastic_DMShell frame = meshtastic_DMShell_init_zero;
    frame.op = op;
    frame.session_id = sessionId;
    frame.seq = seq;
    frame.ack_seq = ackSeq;
    frame.cols = cols;
    frame.rows = rows;
    frame.flags = flags;

    if (payload && payloadLen > 0) {
        memcpy(frame.payload.bytes, payload, payloadLen);
        frame.payload.size = payloadLen;
    }

    meshtastic_MeshPacket *packet = allocDataPacket();
    if (!packet) {
        return nullptr;
    }
    LOG_WARN("DMShell: building packet op=%d session=0x%x seq=%d payloadLen=%d", op, sessionId, seq, payloadLen);
    const size_t encoded = pb_encode_to_bytes(packet->decoded.payload.bytes, sizeof(packet->decoded.payload.bytes),
                                              meshtastic_DMShell_fields, &frame);
    if (encoded == 0) {
        return nullptr;
    }
    packet->decoded.payload.size = encoded;
    return packet;
}

#endif