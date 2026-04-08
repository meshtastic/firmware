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

struct DMShellFrame {
    meshtastic_DMShell_OpCode op = meshtastic_DMShell_OpCode_ERROR;
    uint32_t sessionId = 0;
    uint32_t seq = 0;
    uint32_t ackSeq = 0;
    uint32_t cols = 0;
    uint32_t rows = 0;
    uint32_t flags = 0;
    uint8_t payload[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};
    size_t payloadLen = 0;
};

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
    uint32_t lastActivityMs = 0;
    struct SentFrame {
        bool valid = false;
        meshtastic_DMShell_OpCode op = meshtastic_DMShell_OpCode_ERROR;
        uint32_t sessionId = 0;
        uint32_t seq = 0;
        uint32_t ackSeq = 0;
        uint32_t cols = 0;
        uint32_t rows = 0;
        uint32_t flags = 0;
        uint8_t payload[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};
        size_t payloadLen = 0;
    };
    std::array<SentFrame, 10> txHistory = {};
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

    bool parseFrame(const meshtastic_MeshPacket &mp, DMShellFrame &outFrame);
    bool isAuthorizedPacket(const meshtastic_MeshPacket &mp) const;
    bool openSession(const meshtastic_MeshPacket &mp, const DMShellFrame &frame);
    bool handleAckFrame(const DMShellFrame &frame);
    bool shouldProcessIncomingFrame(const DMShellFrame &frame);
    bool writeSessionInput(const DMShellFrame &frame);
    void closeSession(const char *reason, bool notifyPeer);
    void reapChildIfExited();

    uint32_t allocTxSeq();
    void rememberSentFrame(meshtastic_DMShell_OpCode op, uint32_t sessionId, uint32_t seq, const uint8_t *payload,
                           size_t payloadLen, uint32_t cols, uint32_t rows, uint32_t ackSeq, uint32_t flags);
    void pruneSentFrames(uint32_t ackSeq);
    void resendFramesFrom(uint32_t startSeq);
    void sendAck(uint32_t replayFromSeq = 0);
    void sendControl(meshtastic_DMShell_OpCode op, const uint8_t *payload, size_t payloadLen);
    void sendFrameToPeer(NodeNum peer, uint8_t channel, meshtastic_DMShell_OpCode op, uint32_t sessionId, uint32_t seq,
                         const uint8_t *payload, size_t payloadLen, uint32_t cols = 0, uint32_t rows = 0, uint32_t ackSeq = 0,
                         uint32_t flags = 0, bool remember = true);
    void sendError(const char *message);
    meshtastic_MeshPacket *buildFramePacket(meshtastic_DMShell_OpCode op, uint32_t sessionId, uint32_t seq,
                                            const uint8_t *payload, size_t payloadLen, uint32_t cols, uint32_t rows,
                                            uint32_t ackSeq, uint32_t flags);
};

extern DMShellModule *dmShellModule;

#endif