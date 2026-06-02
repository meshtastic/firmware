#include "MeshBeaconModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "RadioInterface.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>
#include <string.h>

// Static members
meshtastic_Config_LoRaConfig_ModemPreset MeshBeaconModule::originalModemPreset;
uint16_t MeshBeaconModule::originalLoraChannel;
char MeshBeaconModule::originalChannelName[12];

static MeshBeaconModule_TargetRadioSettings targetRadioSettings[8];

static bool getTargetRadioSettings(const meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset *preset,
                                   uint16_t *slot)
{
    if (!p)
        return false;
    for (auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == p->id) {
            if (preset)
                *preset = entry.preset;
            if (slot)
                *slot = entry.slot;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// MeshBeaconModule base
// ---------------------------------------------------------------------------

MeshBeaconModule::MeshBeaconModule()
{
    originalModemPreset = config.lora.modem_preset;
    originalLoraChannel = config.lora.channel_num;
    strncpy(originalChannelName, channels.getPrimary().name, sizeof(originalChannelName));
}

void MeshBeaconModule::setTargetRadioSettings(const meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset preset,
                                              uint16_t slot)
{
    if (!p)
        return;
    MeshBeaconModule_TargetRadioSettings *target = nullptr;
    for (auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == p->id) {
            target = &entry;
            break;
        }
        if (!target && !entry.inUse)
            target = &entry;
    }
    if (!target)
        target = &targetRadioSettings[0];
    target->inUse = true;
    target->id = p->id;
    target->preset = preset;
    target->slot = slot;
}

bool MeshBeaconModule::hasTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    return getTargetRadioSettings(p, nullptr, nullptr);
}

void MeshBeaconModule::clearTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    if (!p)
        return;
    for (auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == p->id) {
            entry.inUse = false;
            return;
        }
    }
}

bool MeshBeaconModule::reconfigureForBeaconTX(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    meshtastic_ChannelSettings *c = (meshtastic_ChannelSettings *)&channels.getPrimary();
    meshtastic_Config_LoRaConfig_ModemPreset targetPreset;
    uint16_t targetSlot;

    if (p && getTargetRadioSettings(p, &targetPreset, &targetSlot) &&
        (targetPreset != config.lora.modem_preset || targetSlot != config.lora.channel_num)) {

        LOG_INFO("Beacon: switch radio for packet %#08lx to preset=%d slot=%u", p->id, targetPreset, targetSlot);
        config.lora.modem_preset = targetPreset;
        config.lora.channel_num = targetSlot;
        memset(c->name, 0, sizeof(c->name));

        const auto &bcfg = moduleConfig.mesh_beacon;
        if (bcfg.has_broadcast_on_channel && strlen(bcfg.broadcast_on_channel.name) > 0) {
            strncpy(c->name, bcfg.broadcast_on_channel.name, sizeof(c->name));
        } else {
            switch (targetPreset) {
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
                strncpy(c->name, "ShortTurbo", sizeof(c->name));
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
                strncpy(c->name, "ShortFast", sizeof(c->name));
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
                strncpy(c->name, "ShortSlow", sizeof(c->name));
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
                strncpy(c->name, "MediumFast", sizeof(c->name));
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
                strncpy(c->name, "MediumSlow", sizeof(c->name));
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
                strncpy(c->name, "LongFast", sizeof(c->name));
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
                strncpy(c->name, "LongMod", sizeof(c->name));
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
                strncpy(c->name, "LongSlow", sizeof(c->name));
                break;
            default:
                break;
            }
        }

        channels.fixupChannel(channels.getPrimaryIndex());
        p->channel = channels.getHash(channels.getPrimaryIndex());
        iface->reconfigure();
        return true;

    } else if ((!p || !getTargetRadioSettings(p, nullptr, nullptr)) &&
               (config.lora.modem_preset != originalModemPreset || config.lora.channel_num != originalLoraChannel)) {

        LOG_INFO("Beacon: restoring radio config after beacon TX");
        config.lora.modem_preset = originalModemPreset;
        config.lora.channel_num = originalLoraChannel;
        memset(c->name, 0, sizeof(c->name));
        strncpy(c->name, originalChannelName, sizeof(c->name));

        channels.fixupChannel(channels.getPrimaryIndex());
        iface->reconfigure();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MeshBeaconBroadcastModule
// ---------------------------------------------------------------------------

MeshBeaconBroadcastModule *meshBeaconBroadcastModule;

MeshBeaconBroadcastModule::MeshBeaconBroadcastModule() : MeshBeaconModule(), concurrency::OSThread("MeshBeaconBroadcast")
{
    setIntervalFromNow(setStartDelay());
}

void MeshBeaconBroadcastModule::sendBeacon()
{
    const auto &bcfg = moduleConfig.mesh_beacon;

    meshtastic_MeshBeacon beacon = meshtastic_MeshBeacon_init_zero;
    strncpy(beacon.message, bcfg.broadcast_message, sizeof(beacon.message) - 1);

    if (bcfg.has_broadcast_offer_channel) {
        beacon.has_offer_channel = true;
        beacon.offer_channel = bcfg.broadcast_offer_channel;
    }
    beacon.offer_preset = bcfg.broadcast_offer_preset;
    beacon.offer_region = bcfg.broadcast_offer_region;

    meshtastic_MeshPacket *p = allocDataProtobuf(beacon);
    if (!p) {
        LOG_WARN("Beacon: failed to allocate packet");
        return;
    }

    p->to = NODENUM_BROADCAST;
    p->from = (bcfg.broadcast_send_as_node != 0) ? bcfg.broadcast_send_as_node : nodeDB->getNodeNum();
    p->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
    p->hop_limit = 3;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = false;
    p->rx_time = getValidTime(RTCQualityFromNet);

    if (bcfg.broadcast_on_preset != _meshtastic_Config_LoRaConfig_ModemPreset_MIN &&
        (bcfg.broadcast_on_preset != config.lora.modem_preset ||
         (bcfg.has_broadcast_on_channel && bcfg.broadcast_on_channel.channel_num != config.lora.channel_num))) {
        uint16_t targetSlot = bcfg.has_broadcast_on_channel ? bcfg.broadcast_on_channel.channel_num : config.lora.channel_num;
        setTargetRadioSettings(p, bcfg.broadcast_on_preset, targetSlot);
    }

    LOG_INFO("Beacon: broadcast from=%#08lx msg='%.40s'", p->from, beacon.message);
    service->sendToMesh(p, RX_SRC_LOCAL, false);
}

int32_t MeshBeaconBroadcastModule::runOnce()
{
    const auto &bcfg = moduleConfig.mesh_beacon;
    if (bcfg.broadcast_enabled && airTime->isTxAllowedAirUtil() &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        sendBeacon();
    }

    uint32_t interval = bcfg.broadcast_interval_secs;
    if (interval < 3600)
        interval = 3600;
    if (interval > 259200)
        interval = 259200;
    return interval * 1000;
}

// ---------------------------------------------------------------------------
// MeshBeaconListenerModule
// ---------------------------------------------------------------------------

MeshBeaconListenerModule *meshBeaconListenerModule;
MeshBeaconListenerModule::BeaconOffer MeshBeaconListenerModule::lastReceivedOffer;

MeshBeaconListenerModule::MeshBeaconListenerModule()
    : ProtobufModule("beacon_listen", meshtastic_PortNum_MESH_BEACON_APP, &meshtastic_MeshBeacon_msg)
{
    lastReceivedOffer = {};
}

bool MeshBeaconListenerModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return moduleConfig.has_mesh_beacon && moduleConfig.mesh_beacon.listen_enabled &&
           p->decoded.portnum == meshtastic_PortNum_MESH_BEACON_APP;
}

bool MeshBeaconListenerModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MeshBeacon *b)
{
    if (!b || strlen(b->message) == 0)
        return false;

    LOG_INFO("Beacon: received from %#08lx: '%.40s'", mp.from, b->message);

    // Deliver text to the local inbox as a TEXT_MESSAGE_APP packet.
    meshtastic_MeshPacket *txt = packetPool.allocCopy(mp);
    txt->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    txt->to = nodeDB->getNodeNum();
    memset(txt->decoded.payload.bytes, 0, sizeof(txt->decoded.payload.bytes));
    txt->decoded.payload.size = (pb_size_t)strnlen(b->message, sizeof(b->message) - 1);
    memcpy(txt->decoded.payload.bytes, b->message, txt->decoded.payload.size);
    service->handleToRadio(*txt);
    packetPool.release(txt);

    // Cache any offer for the client app — never auto-applied.
    if (b->has_offer_channel || b->offer_preset != 0) {
        lastReceivedOffer.valid = true;
        lastReceivedOffer.sender = mp.from;
        lastReceivedOffer.has_channel = b->has_offer_channel;
        if (b->has_offer_channel)
            lastReceivedOffer.channel = b->offer_channel;
        lastReceivedOffer.region = b->offer_region;
        lastReceivedOffer.preset = b->offer_preset;
        lastReceivedOffer.received_at = getValidTime(RTCQualityFromNet);
        LOG_INFO("Beacon: stored offer from %#08lx (preset=%d)", mp.from, b->offer_preset);
    }

    powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);
    return false;
}
