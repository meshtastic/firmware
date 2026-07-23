#pragma once

#if defined(RADIOMASTER_NOMAD_DUAL_RADIO) && RADIOLIB_EXCLUDE_LR11X0 != 1

#include "LR1121Interface.h"
#include "RadioInterface.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <memory>

class NomadDualRadioInterface final : public RadioInterface, protected concurrency::OSThread
{
  public:
    explicit NomadDualRadioInterface(LockingArduinoHal *hal);

    bool init() override;
    bool reconfigure() override;
    bool canSleep(bool deepSleep = false) override;
    bool sleep() override;
    bool wideLora() override;
    bool supportsSubGhz() override;
    bool isIRQPending() override;

    ErrorCode send(meshtastic_MeshPacket *p) override;
    meshtastic_QueueStatus getQueueStatus() override;
    bool cancelSending(NodeNum from, PacketId id) override;
    bool findInTxQueue(NodeNum from, PacketId id) override;
    void clampToLateRebroadcastWindow(NodeNum from, PacketId id) override;
    bool removePendingTXPacket(NodeNum from, PacketId id, uint32_t hop_limit_lt) override;
    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override;
    float getFreq() override;

    RadioLibInterface *getPrimaryRadio() const { return primary.get(); }
    RadioLibInterface *getSecondaryRadio() const { return secondary.get(); }

  private:
    bool useSecondary(const meshtastic_MeshPacket *p) const;
    void configureSecondary();
    bool applyConfiguration();
    int32_t runOnce() override;

    std::unique_ptr<LR1121Interface> primary;
    std::unique_ptr<LR1121Interface> secondary;
    meshtastic_Config_LoRaConfig secondaryConfig = meshtastic_Config_LoRaConfig_init_zero;
    bool reconfigurePending = false;
};

#endif
