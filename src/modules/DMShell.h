#pragma once

#include "DMShellRecovery.h"
#include "MeshModule.h"
#include "Router.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <Arduino.h>
#include <array>
#include <functional>

#if defined(ARCH_PORTDUINO)

struct DMShellSession {
    bool active = false;
    uint32_t sessionId = 0;
    NodeNum peer = 0;
    uint8_t channel = 0;
    int masterFd = -1;
    int childPid = -1;
    uint32_t nextTxSeq = 1;
    uint32_t lastAckedRxSeq = 0;
    // In-order frames processed since we last originated one carrying lastAckedRxSeq. The peer bounds
    // its own unacknowledged input, so it needs this cursor even when we have no output to send.
    uint32_t framesSinceOutbound = 0;
    uint32_t lastActivityMs = 0;
    DMShellRxWindow rxWindow;
    // Frames that arrived above a gap, held until the gap fills. Slot bookkeeping lives in rxReorder;
    // these are the slots it hands out.
    DMShellRxReorder rxReorder;
    meshtastic_RemoteShell rxReorderFrames[DMShellRxReorder::SLOTS] = {};
    DMShellTxWindow txWindow;
    // Purely for logging: the window opens and closes many times a second, so only transitions are
    // worth a line, and without them there is no way to tell from a log whether it ever engaged.
    bool txWindowBlocked = false;
    // Sender-side retransmission of the oldest unacknowledged frame while the window is shut.
    uint32_t nextRetransmitMs = 0;
    DMShellRetransmitRun retransmitRun;
    // How long the peer takes to acknowledge a frame, measured, which sets the retransmission interval.
    DMShellAckLatency ackLatency;
    struct SentFrame {
        bool valid = false;
        meshtastic_RemoteShell_OpCode op = meshtastic_RemoteShell_OpCode_ERROR;
        uint32_t sessionId = 0;
        uint32_t seq = 0;
        uint32_t ackSeq = 0;
        uint32_t cols = 0;
        uint32_t rows = 0;
        uint32_t flags = 0;
        uint8_t payload[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};
        size_t payloadLen = 0;
        uint32_t lastSentMs = 0; // when this frame was last handed to the radio, first send or resend
    };
    static constexpr size_t TX_HISTORY_LEN = 50;
    std::array<SentFrame, TX_HISTORY_LEN> txHistory = {};
    size_t txHistoryNext = 0;
    DMShellTxHistoryWindow txHistoryWindow{TX_HISTORY_LEN};
};

class DMShellModule : private concurrency::OSThread, public SinglePortModule
{

  public:
    DMShellModule();

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual int32_t runOnce() override;

  private:
    static constexpr uint32_t SESSION_IDLE_TIMEOUT_MS = 5 * 60 * 1000;

    DMShellSession session;
    pid_t pendingChildPid = -1;
    // Set once at construction from DMSHELL_TX_WINDOW (0 = unbounded); see the constructor.
    uint32_t txWindowFrames = 0;
    // Set once at construction from DMSHELL_MAX_RETRANSMITS (0 = no bound).
    uint32_t maxConsecutiveRetransmits = 0;
    // Set once at construction from DMSHELL_LEGACY_RECOVERY; see the constructor.
    bool legacyRecovery = false;

    uint32_t replayRequestIntervalMs() const;
    void notePeerReceiveCursor(const meshtastic_RemoteShell &frame);
    void retransmitOldestUnacked();
    void flushPendingOutputOnInterrupt(const meshtastic_RemoteShell &frame);
    void sendBareAck();

    void applySessionFrame(const meshtastic_RemoteShell &frame);
    void rememberOutOfOrderFrame(const meshtastic_RemoteShell &frame);
    bool takeBufferedFrame(uint32_t seq, meshtastic_RemoteShell &outFrame);
    void drainBufferedFrames();
    bool parseFrame(const meshtastic_MeshPacket &mp, meshtastic_RemoteShell &outFrame);
    bool isAuthorizedPacket(const meshtastic_MeshPacket &mp) const;
    bool openSession(const meshtastic_MeshPacket &mp, const meshtastic_RemoteShell &frame);
    DMShellRxDecision classifyIncomingFrame(const meshtastic_RemoteShell &frame);
    bool writeSessionInput(const meshtastic_RemoteShell &frame);
    void closeSession(const char *reason, bool notifyPeer);
    void reapChildIfExited();
    void processPendingChildReap();

    void rememberSentFrame(meshtastic_RemoteShell frame);
    void resendFramesFrom(uint32_t startSeq);
    DMShellSession::SentFrame *findSentFrame(uint32_t seq);
    /// Ask the peer to replay replayFromSeq. Never called with 0; see the definition.
    void sendReplayRequest(uint32_t replayFromSeq);
    void sendFrameToPeer(NodeNum peer, meshtastic_RemoteShell frame, bool remember = true);
    void sendError(const char *message, NodeNum peer = 0);
};

extern DMShellModule *dmShellModule;

#endif