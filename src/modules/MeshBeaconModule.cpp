#include "MeshBeaconModule.h"
#include "Default.h"
#include "DisplayFormatters.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "RadioInterface.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>
#include <string.h>

// Static members
meshtastic_Config_LoRaConfig_ModemPreset MeshBeaconModule::originalModemPreset;
uint16_t MeshBeaconModule::originalLoraChannel;
meshtastic_Config_LoRaConfig_RegionCode MeshBeaconModule::originalRegion;
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
    originalRegion = config.lora.region;
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
    meshtastic_ChannelSettings *c = &channels.getByIndex(channels.getPrimaryIndex()).settings;
    meshtastic_Config_LoRaConfig_ModemPreset targetPreset;
    uint16_t targetSlot;

    if (p && getTargetRadioSettings(p, &targetPreset, &targetSlot) &&
        (targetPreset != config.lora.modem_preset || targetSlot != config.lora.channel_num)) {

        const auto &bcfg = moduleConfig.mesh_beacon;
        meshtastic_Config_LoRaConfig_RegionCode targetRegion;
        if (bcfg.broadcast_on_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            targetRegion = bcfg.broadcast_on_region;
        else
            targetRegion = config.lora.region;

        // Guard: skip TX if preset is invalid for the target region.
        meshtastic_Config_LoRaConfig broadcastOnSetting = config.lora;
        broadcastOnSetting.use_preset = true;
        broadcastOnSetting.modem_preset = targetPreset;
        broadcastOnSetting.region = targetRegion;
        if (!RadioInterface::validateConfigLora(broadcastOnSetting)) {
            LOG_WARN("Beacon: preset %d invalid for region %d, skipping radio switch", targetPreset, targetRegion);
            return false;
        }

        // Snapshot current (non-beacon) settings so we restore to the latest config.
        originalModemPreset = config.lora.modem_preset;
        originalLoraChannel = config.lora.channel_num;
        originalRegion = config.lora.region;
        strncpy(originalChannelName, c->name, sizeof(originalChannelName) - 1);
        originalChannelName[sizeof(originalChannelName) - 1] = '\0';

        LOG_INFO("Beacon: switch radio for packet %#08lx to preset=%d slot=%u region=%d", p->id, targetPreset, targetSlot,
                 targetRegion);
        config.lora.modem_preset = targetPreset;
        config.lora.channel_num = targetSlot;
        if (targetRegion != config.lora.region)
            config.lora.region = targetRegion;
        memset(c->name, 0, sizeof(c->name));

        if (bcfg.has_broadcast_on_channel && strlen(bcfg.broadcast_on_channel.name) > 0) {
            strncpy(c->name, bcfg.broadcast_on_channel.name, sizeof(c->name) - 1);
        } else if (originalChannelName[0] != '\0') {
            strncpy(c->name, originalChannelName, sizeof(c->name) - 1);
        } else {
            strncpy(c->name, DisplayFormatters::getModemPresetDisplayName(targetPreset, false, true), sizeof(c->name) - 1);
        }
        c->name[sizeof(c->name) - 1] = '\0';

        channels.fixupChannel(channels.getPrimaryIndex());
        p->channel = channels.getHash(channels.getPrimaryIndex());
        iface->reconfigure();
        return true;

    } else if ((!p || !getTargetRadioSettings(p, nullptr, nullptr)) &&
               (config.lora.modem_preset != originalModemPreset || config.lora.channel_num != originalLoraChannel ||
                config.lora.region != originalRegion)) {

        LOG_INFO("Beacon: restoring radio config after beacon TX");
        config.lora.modem_preset = originalModemPreset;
        config.lora.channel_num = originalLoraChannel;
        config.lora.region = originalRegion;
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

MeshBeaconBroadcastModule::MeshBeaconBroadcastModule()
    : MeshBeaconModule(), ProtobufModule("beacon_tx", meshtastic_PortNum_MESH_BEACON_APP, &meshtastic_MeshBeacon_msg),
      concurrency::OSThread("MeshBeaconBroadcast")
{
    setIntervalFromNow(setStartDelay());
}

void MeshBeaconBroadcastModule::rebuildCache()
{
    const auto &bcfg = moduleConfig.mesh_beacon;
    meshtastic_MeshBeacon beacon = meshtastic_MeshBeacon_init_zero;
    strncpy(beacon.message, bcfg.broadcast_message, sizeof(beacon.message) - 1);
    if (bcfg.has_broadcast_offer_channel) {
        beacon.has_offer_channel = true;
        beacon.offer_channel = bcfg.broadcast_offer_channel;
        // PSK is included intentionally: this beacon is a public join-invitation.
        // The offered channel is not secret — the PSK here is a convenience token,
        // not a security boundary.  Operators who want a private channel must
        // distribute the PSK out-of-band and leave offer_channel unset.
    }
    beacon.offer_preset = bcfg.broadcast_offer_preset;
    beacon.offer_region = bcfg.broadcast_offer_region;
    payloadCacheSize = (pb_size_t)pb_encode_to_bytes(payloadCache, sizeof(payloadCache), &meshtastic_MeshBeacon_msg, &beacon);
    payloadCacheDirty = false;
    LOG_DEBUG("Beacon: payload cache rebuilt (%u bytes)", payloadCacheSize);
}

void MeshBeaconBroadcastModule::sendBeacon()
{
    const auto &bcfg = moduleConfig.mesh_beacon;

    bool hasRadioContent = (bcfg.broadcast_offer_preset != _meshtastic_Config_LoRaConfig_ModemPreset_MIN) ||
                           bcfg.has_broadcast_offer_channel ||
                           (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    if (bcfg.broadcast_message[0] == '\0' && !hasRadioContent) {
        LOG_DEBUG("Beacon: nothing to send (empty message, no offer), skipping");
        return;
    }

    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        LOG_WARN("Beacon: failed to allocate packet");
        return;
    }

    // Use MESH_BEACON_APP only when radio settings are being offered to listeners;
    // otherwise fall back to TEXT_MESSAGE_APP so standard clients receive the text
    // without needing a MESH_BEACON_APP decoder.  broadcast_on_* controls which
    // radio config to use for TX and has no bearing on the portnum.
    if (hasRadioContent) {
        if (payloadCacheDirty)
            rebuildCache();
        memcpy(p->decoded.payload.bytes, payloadCache, payloadCacheSize);
        p->decoded.payload.size = payloadCacheSize;
        p->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
    } else {
        pb_size_t msgLen = (pb_size_t)strnlen(bcfg.broadcast_message, sizeof(bcfg.broadcast_message) - 1);
        memcpy(p->decoded.payload.bytes, bcfg.broadcast_message, msgLen);
        p->decoded.payload.size = msgLen;
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    }
    p->to = NODENUM_BROADCAST;
    p->from = (bcfg.broadcast_send_as_node != 0) ? bcfg.broadcast_send_as_node : nodeDB->getNodeNum();
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

    LOG_INFO("Beacon: broadcast from=%#08lx msg='%.40s'", p->from, bcfg.broadcast_message);
    router->send(p);
}

int32_t MeshBeaconBroadcastModule::runOnce()
{
    const auto &bcfg = moduleConfig.mesh_beacon;
    if (bcfg.broadcast_enabled && airTime->isTxAllowedAirUtil() &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        sendBeacon();
    }

    const uint32_t intervalSecs =
        Default::getConfiguredOrDefault(bcfg.broadcast_interval_secs, default_mesh_beacon_min_broadcast_interval_secs);
    return static_cast<int32_t>(
               Default::getConfiguredOrMinimumValue(intervalSecs, default_mesh_beacon_min_broadcast_interval_secs)) *
           1000;
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
    if (!txt) {
        LOG_WARN("Beacon: failed to alloc inbox copy");
        return false;
    }
    txt->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    txt->to = nodeDB->getNodeNum();
    memset(txt->decoded.payload.bytes, 0, sizeof(txt->decoded.payload.bytes));
    txt->decoded.payload.size = (pb_size_t)strnlen(b->message, sizeof(b->message) - 1);
    memcpy(txt->decoded.payload.bytes, b->message, txt->decoded.payload.size);
    service->handleToRadio(*txt);
    packetPool.release(txt);

    // Cache any offer for the client app — never auto-applied.
    if (b->has_offer_channel || b->offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET ||
        b->offer_preset != _meshtastic_Config_LoRaConfig_ModemPreset_MIN) {
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
