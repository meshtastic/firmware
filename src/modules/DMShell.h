#pragma once

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
    uint32_t nextExpectedRxSeq = 1;
    uint32_t highestSeenRxSeq = 0;
    uint32_t lastActivityMs = 0;
    // --- Half-duplex turn-taking ("talking stick") state ---
    bool turnManaged = false; // becomes true once the peer uses turn-taking flags; enables gating
    bool hasToken = false;    // do we currently hold the right to transmit?
    uint32_t lastRtsMs = 0;   // last time we asked the client for a turn (rate-limit)
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
    };
    std::array<SentFrame, 50> txHistory = {};
    size_t txHistoryNext = 0;
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

    bool parseFrame(const meshtastic_MeshPacket &mp, meshtastic_RemoteShell &outFrame);
    bool isAuthorizedPacket(const meshtastic_MeshPacket &mp) const;
    bool openSession(const meshtastic_MeshPacket &mp, const meshtastic_RemoteShell &frame);
    bool shouldProcessIncomingFrame(const meshtastic_RemoteShell &frame);
    bool writeSessionInput(const meshtastic_RemoteShell &frame);
    void closeSession(const char *reason, bool notifyPeer);
    void reapChildIfExited();
    void processPendingChildReap();

    void rememberSentFrame(meshtastic_RemoteShell frame);
    void resendFramesFrom(uint32_t startSeq);
    void sendAck(uint32_t replayFromSeq = 0);
    void sendFrameToPeer(NodeNum peer, meshtastic_RemoteShell frame, bool remember = true);
    void sendError(const char *message, NodeNum peer = 0);

    // --- Turn-taking helpers ---
    void applyTurnFlags(const meshtastic_RemoteShell &frame);
    bool ptyHasOutput();
    void serviceTurn();
    void sendOutputFrame(const uint8_t *data, size_t len, uint32_t extraFlags);
    void sendTurnGrant(bool more);
    void sendRts();
};

extern DMShellModule *dmShellModule;

#endif