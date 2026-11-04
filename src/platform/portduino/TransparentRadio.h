#pragma once

#include "TransparentLink.h"

#if HAS_TRANSPARENT_RADIO

#include "MeshPacketQueue.h"
#include "PhoneAPI.h"
#include "RadioInterface.h"
#include "concurrency/OSThread.h"
#include "meshtastic/admin.pb.h"

/**
 * meshtasticd's radio for Lora.Module "transparent": the LoRa chip belongs to another node, running
 * LoRaConfig.transparent_mode and reached over its client API. That node puts this node's frames on the air
 * unchanged and hands back every frame it hears, so routing, encryption and airtime accounting all happen here.
 */
class TransparentRadio : public RadioInterface, protected concurrency::OSThread
{
  public:
    enum class State : uint8_t {
        Disconnected, // waiting to (re)open the link
        Configuring,  // asked for the device's config dump
        Pushing,      // sent our LoRaConfig, waiting to read it back
        Ready,        // device confirmed transparent_mode with our settings
        Unsupported,  // device firmware ignored transparent_mode; stays here until restart
    };

    explicit TransparentRadio(std::unique_ptr<TransparentLink> link);

    bool init() override;
    bool reconfigure() override;
    ErrorCode send(meshtastic_MeshPacket *p) override;
    bool cancelSending(NodeNum from, PacketId id) override;
    bool findInTxQueue(NodeNum from, PacketId id) override;
    bool removePendingTXPacket(NodeNum from, PacketId id, uint32_t hop_limit_lt) override;
    void clampToLateRebroadcastWindow(NodeNum from, PacketId id) override;
    meshtastic_QueueStatus getQueueStatus() override;
    bool canSleep(bool deepSleep = false) override { return txQueue.empty(); }

    using RadioInterface::getPacketTime;
    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override;

    State getState() const { return state; }

    uint32_t rxGood = 0, txGood = 0, txDrop = 0;

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint32_t POLL_INTERVAL_MS = 10;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
    static constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 15000;
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5 * 60 * 1000; // the device drops an API client silent for 15 minutes
    static constexpr size_t FRAME_HEADER_LEN = 4;

    void setState(State next);
    void startHandshake();
    void linkLost();
    bool pumpRx();
    void parseByte(uint8_t c);
    void handleFromRadio();
    void handleAdminReply(const meshtastic_MeshPacket &p);
    void pushConfig();
    meshtastic_Config_LoRaConfig desiredLoraConfig() const;
    bool sendAdmin(const meshtastic_AdminMessage &msg, bool wantResponse, PacketId *idOut = nullptr);
    bool sendHeartbeat();
    bool writeToRadio();
    bool forward(const meshtastic_MeshPacket *p);
    void deliverReceived(const meshtastic_MeshPacket &p);
    void releaseRemoved(meshtastic_MeshPacket *p);
    void scheduleTx();
    void serviceTxQueue();

    std::unique_ptr<TransparentLink> link;
    State state = State::Disconnected;

    MeshPacketQueue txQueue = MeshPacketQueue(MAX_TX_QUEUE);
    const meshtastic_MeshPacket *scheduledPacket = nullptr; // the queue front txDueMs was computed for
    uint32_t txDueMs = 0;
    uint32_t txBusyUntilMs = 0; // end of the last forwarded frame's airtime
    bool txBusySet = false;

    bool openAttempted = false;
    bool openFailureLogged = false;
    uint32_t lastOpenAttemptMs = 0;
    uint32_t stateSinceMs = 0;
    uint32_t lastWriteMs = 0;

    NodeNum deviceNodeNum = 0;
    bool haveDeviceLora = false;
    meshtastic_Config_LoRaConfig deviceLora = meshtastic_Config_LoRaConfig_init_zero;
    PacketId verifyRequestId = 0;
    int32_t deviceQueueFree = -1; // unknown until the device reports a QueueStatus

    uint8_t rxBuf[FRAME_HEADER_LEN + MAX_TO_FROM_RADIO_SIZE];
    size_t rxPtr = 0;
    uint8_t txFrame[FRAME_HEADER_LEN + MAX_TO_FROM_RADIO_SIZE];
    meshtastic_FromRadio fromRadio = meshtastic_FromRadio_init_zero;
    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_zero;
};

#endif
