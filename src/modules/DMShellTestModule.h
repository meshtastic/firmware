#pragma once

// Bench: -DDMSHELL_TEST_PEER=0x<node> builds a DMShell client into the board under test. It opens a session to that
// node and types the bench workload itself, so a run needs no API client and the serial port can stay in logging mode.
// It speaks the same protocol as bin/dmshell_client.py, on the bookkeeping in DMShellRecovery.h.
#ifdef DMSHELL_TEST_PEER

#include "DMShellRecovery.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

class DMShellTestModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    DMShellTestModule();

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    int32_t runOnce() override;

  private:
    enum class Phase : uint8_t { Waiting, Opening, Active, Closing, Done };

    struct SentFrame {
        bool valid = false;
        uint32_t lastSentMs = 0;
        meshtastic_RemoteShell frame = meshtastic_RemoteShell_init_zero;
    };
    static constexpr uint32_t HISTORY_LEN = 16;
    static constexpr uint32_t PENDING_INPUT_MAX = 512;
    static constexpr uint32_t BATCH_MAX_BYTES = 64;

    // Cumulative per session, named as the client's stats so the two can be compared directly
    struct Stats {
        uint32_t rxFramesTotal, rxNewInOrder, rxDuplicate, rxAheadOfCursor, rxHeldForGap, rxDroppedReorderFull;
        uint32_t rxOutputBytes, rxOtherSession;
        uint32_t txFramesTotal, txInput, txAck, txPing, txOpen, txClose, txPayloadBytes;
        uint32_t framesResent, inputRetransmits, replayRequestsSent, replaysSentToPeer, replayUnavailable, replayEvicted;
        uint32_t openRetries, inputWindowClosed, pendingInputDropped;
    };

    Phase phase = Phase::Waiting;
    uint32_t sessionsDone = 0;
    uint32_t nextSessionAtMs = 0;

    uint32_t sessionId = 0;
    uint32_t nextTxSeq = 1;
    uint32_t openSeq = 0;
    uint32_t openedAtMs = 0;
    uint32_t sessionStartMs = 0;
    uint32_t nextOpenRetryMs = 0;
    uint32_t openRetryIntervalMs = 0;
    uint32_t closeDeadlineMs = 0;
    bool closeSent = false;
    const char *endReason = nullptr;

    DMShellRxWindow rxWindow;
    DMShellRxReorder rxReorder;
    meshtastic_RemoteShell reorderFrames[DMShellRxReorder::SLOTS] = {};
    uint32_t peerHighestSeq = 0;
    uint32_t framesSinceOutbound = 0;
    uint32_t lastInboundMs = 0;
    uint32_t lastHeartbeatMs = 0;
    uint32_t lastPongReplaySeq = 0;
    uint32_t lastPongReplayMs = 0;

    DMShellTxWindow txWindow;
    bool txWindowBlocked = false;
    SentFrame history[HISTORY_LEN] = {};
    DMShellTxHistoryWindow historyWindow{HISTORY_LEN};
    DMShellAckLatency ackLatency;
    uint32_t retransmitSeq = 0;
    uint32_t retransmitAttempts = 0;
    uint32_t retransmitIntervalMs = 0;
    uint32_t nextRetransmitMs = 0;

    // The workload: the command once, then one keystroke per interval
    uint8_t pendingInput[PENDING_INPUT_MAX] = {};
    uint32_t pendingInputLen = 0;
    bool commandTyped = false;
    bool commandRepeated = false;
    uint32_t keystrokesTyped = 0;
    uint32_t nextKeystrokeMs = 0;
    // The batch the client's stdin read would return, before it becomes one INPUT
    uint8_t batch[BATCH_MAX_BYTES] = {};
    uint32_t batchLen = 0;
    uint32_t batchFirstByteMs = 0;
    uint32_t batchLastByteMs = 0;
    bool batchEndsHere = false;

    Stats stats = {};

    void startSession(uint32_t now);
    void endSession(uint32_t now, const char *reason);
    void serviceActive(uint32_t now);
    void typeWorkload(uint32_t now);
    void typeCommand(uint32_t now);
    void addByte(uint8_t b, uint32_t now);
    bool batchDue(uint32_t now) const;
    void closeBatch();
    void flushInput(uint32_t now);
    void serviceRetransmit(uint32_t now);
    void serviceHeartbeat(uint32_t now);

    void handleFrame(const meshtastic_RemoteShell &frame, uint32_t now);
    void handleInOrder(const meshtastic_RemoteShell &frame, uint32_t now);
    void notePeerReceiveCursor(const meshtastic_RemoteShell &frame, uint32_t now);
    void replayFrom(uint32_t seq, uint32_t now);
    uint32_t baseIntervalMs() const;

    uint32_t sendFrame(meshtastic_RemoteShell_OpCode op, uint32_t now, const uint8_t *payload = nullptr, size_t len = 0,
                       bool remember = true, uint32_t lastTxSeq = 0, uint32_t lastRxSeq = 0);
    void sendAck(uint32_t now, uint32_t replayFrom = 0);
    void transmit(const meshtastic_RemoteShell &frame, uint32_t now);
    SentFrame *findSent(uint32_t seq);
    void logStats(uint32_t now);
};

extern DMShellTestModule *dmShellTestModule;

#endif
