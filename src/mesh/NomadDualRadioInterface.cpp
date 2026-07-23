#include "NomadDualRadioInterface.h"

#if defined(RADIOMASTER_NOMAD_DUAL_RADIO) && RADIOLIB_EXCLUDE_LR11X0 != 1

#include "Channels.h"
#include "DebugConfiguration.h"
#include "MeshRadio.h"
#include <algorithm>

static constexpr float NOMAD_SECONDARY_FREQUENCY_MHZ = 926.750f;
static constexpr uint32_t NOMAD_SECONDARY_CHANNEL_NUM = 50;
static constexpr int8_t NOMAD_SECONDARY_TX_POWER_DBM = 10;
static constexpr ChannelIndex NOMAD_SECONDARY_LOGICAL_CHANNEL = 1;

NomadDualRadioInterface::NomadDualRadioInterface(LockingArduinoHal *hal)
    : concurrency::OSThread("NomadDualRadio", 250),
      primary(new LR1121Interface(hal, LR1121_SPI_NSS_PIN, LR1121_IRQ_PIN, LR1121_NRESET_PIN, LR1121_BUSY_PIN)),
      secondary(new LR1121Interface(hal, LR1121_SPI_NSS2_PIN, LR1121_IRQ2_PIN, LR1121_NRESET2_PIN, LR1121_BUSY2_PIN))
{
    primary->setEventObserversEnabled(false);
    secondary->setEventObserversEnabled(false);
    primary->setTransportMechanism(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA);
    secondary->setTransportMechanism(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA_ALT1);
    configureSecondary();
    secondary->setLoRaConfigOverride(&secondaryConfig);
}

void NomadDualRadioInterface::configureSecondary()
{
    secondaryConfig = config.lora;
    secondaryConfig.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    secondaryConfig.use_preset = true;
    secondaryConfig.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    secondaryConfig.channel_num = NOMAD_SECONDARY_CHANNEL_NUM;
    secondaryConfig.override_frequency = NOMAD_SECONDARY_FREQUENCY_MHZ;
    secondaryConfig.frequency_offset = 0;
    secondaryConfig.tx_power = NOMAD_SECONDARY_TX_POWER_DBM;
    secondaryConfig.tx_enabled = config.lora.tx_enabled && config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_US;
}

bool NomadDualRadioInterface::init()
{
    if (!primary->init()) {
        LOG_WARN("Nomad primary LR1121 init failed");
        return false;
    }

    if (!secondary->init()) {
        LOG_WARN("Nomad secondary LR1121 init failed");
        primary->sleep();
        return false;
    }

    primary->setTransmitPeer(secondary.get());
    secondary->setTransmitPeer(primary.get());
    registerEventObservers();
    LOG_INFO("Nomad dual radio: primary %.3f MHz, secondary ShortTurbo %.3f MHz at %d dBm", primary->getFreq(),
             secondary->getFreq(), NOMAD_SECONDARY_TX_POWER_DBM);
    return true;
}

bool NomadDualRadioInterface::reconfigure()
{
    if (primary->isSending() || secondary->isSending()) {
        LOG_INFO("Nomad dual radio: defer configuration change until both transmitters are idle");
        reconfigurePending = true;
        return true;
    }

    return applyConfiguration();
}

bool NomadDualRadioInterface::applyConfiguration()
{
    RadioLibInterface::setCoordinatedTransition(true);
    configureSecondary();
    const bool primaryConfigured = primary->reconfigure();
    const bool secondaryConfigured = secondary->reconfigure();
    RadioLibInterface::setCoordinatedTransition(false);
    return primaryConfigured && secondaryConfigured;
}

int32_t NomadDualRadioInterface::runOnce()
{
    if (reconfigurePending && !primary->isSending() && !secondary->isSending()) {
        reconfigurePending = !applyConfiguration();
    }
    return 250;
}

bool NomadDualRadioInterface::canSleep(bool deepSleep)
{
    return primary->canSleep(deepSleep) && secondary->canSleep(deepSleep);
}

bool NomadDualRadioInterface::sleep()
{
    if (primary->isSending() || secondary->isSending())
        return false;

    if (disabled) {
        primary->RadioInterface::disable();
        secondary->RadioInterface::disable();
        return true;
    }

    RadioLibInterface::setCoordinatedTransition(true);
    const bool primarySlept = primary->sleep();
    const bool secondarySlept = secondary->sleep();
    RadioLibInterface::setCoordinatedTransition(false);
    return primarySlept && secondarySlept;
}

bool NomadDualRadioInterface::wideLora()
{
    return primary->wideLora();
}

bool NomadDualRadioInterface::supportsSubGhz()
{
    return primary->supportsSubGhz() && secondary->supportsSubGhz();
}

bool NomadDualRadioInterface::isIRQPending()
{
    return primary->isIRQPending() || secondary->isIRQPending();
}

bool NomadDualRadioInterface::useSecondary(const meshtastic_MeshPacket *p) const
{
    if (p->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA_ALT1)
        return true;

    if (channels.getNumChannels() <= NOMAD_SECONDARY_LOGICAL_CHANNEL)
        return false;

    const int16_t secondaryHash = channels.getHash(NOMAD_SECONDARY_LOGICAL_CHANNEL);
    return secondaryHash >= 0 && p->channel == static_cast<uint8_t>(secondaryHash);
}

ErrorCode NomadDualRadioInterface::send(meshtastic_MeshPacket *p)
{
    if (disabled) {
        packetPool.release(p);
        return ERRNO_DISABLED;
    }

    if (useSecondary(p)) {
        LOG_DEBUG("Nomad route packet 0x%08x through ShortTurbo radio", p->id);
        return secondary->send(p);
    }
    return primary->send(p);
}

meshtastic_QueueStatus NomadDualRadioInterface::getQueueStatus()
{
    RadioInterface *primaryInterface = primary.get();
    RadioInterface *secondaryInterface = secondary.get();
    meshtastic_QueueStatus primaryStatus = primaryInterface->getQueueStatus();
    meshtastic_QueueStatus secondaryStatus = secondaryInterface->getQueueStatus();
    primaryStatus.free += secondaryStatus.free;
    primaryStatus.maxlen += secondaryStatus.maxlen;
    return primaryStatus;
}

bool NomadDualRadioInterface::cancelSending(NodeNum from, PacketId id)
{
    const bool primaryCanceled = primary->cancelSending(from, id);
    const bool secondaryCanceled = secondary->cancelSending(from, id);
    return primaryCanceled || secondaryCanceled;
}

bool NomadDualRadioInterface::findInTxQueue(NodeNum from, PacketId id)
{
    return primary->findInTxQueue(from, id) || secondary->findInTxQueue(from, id);
}

void NomadDualRadioInterface::clampToLateRebroadcastWindow(NodeNum from, PacketId id)
{
    static_cast<RadioInterface *>(primary.get())->clampToLateRebroadcastWindow(from, id);
    static_cast<RadioInterface *>(secondary.get())->clampToLateRebroadcastWindow(from, id);
}

bool NomadDualRadioInterface::removePendingTXPacket(NodeNum from, PacketId id, uint32_t hop_limit_lt)
{
    const bool primaryRemoved = static_cast<RadioInterface *>(primary.get())->removePendingTXPacket(from, id, hop_limit_lt);
    const bool secondaryRemoved = static_cast<RadioInterface *>(secondary.get())->removePendingTXPacket(from, id, hop_limit_lt);
    return primaryRemoved || secondaryRemoved;
}

uint32_t NomadDualRadioInterface::getPacketTime(uint32_t totalPacketLen, bool received)
{
    RadioInterface *primaryInterface = primary.get();
    RadioInterface *secondaryInterface = secondary.get();
    return std::max(primaryInterface->getPacketTime(totalPacketLen, received),
                    secondaryInterface->getPacketTime(totalPacketLen, received));
}

float NomadDualRadioInterface::getFreq()
{
    return primary->getFreq();
}

#endif
