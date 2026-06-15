#include "MeshBeaconModule.h"
#include "Default.h"
#include "DisplayFormatters.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
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
meshtastic_ChannelSettings MeshBeaconModule::originalPrimaryChannel;

static MeshBeaconModule_TargetRadioSettings targetRadioSettings[8];

static bool getTargetRadioSettings(const meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset *preset,
                                   uint16_t *slot)
{
    if (!p)
        return false;
    for (const auto &entry : targetRadioSettings) {
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
    originalPrimaryChannel = channels.getPrimary();
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

    const auto channelDiffers = [&](const meshtastic_ChannelSettings &target) {
        return strncmp(c->name, target.name, sizeof(c->name)) != 0 || c->psk.size != target.psk.size ||
               memcmp(c->psk.bytes, target.psk.bytes, c->psk.size) != 0 || c->channel_num != target.channel_num;
    };

    if (p && getTargetRadioSettings(p, &targetPreset, &targetSlot) && true) {

        const auto &bcfg = moduleConfig.mesh_beacon;
        meshtastic_Config_LoRaConfig_RegionCode targetRegion;
        if (bcfg.broadcast_on_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            targetRegion = bcfg.broadcast_on_region;
        else
            targetRegion = config.lora.region;

        meshtastic_ChannelSettings targetChannel = *c;
        if (bcfg.has_broadcast_on_channel) {
            targetChannel.channel_num = bcfg.broadcast_on_channel.channel_num;
            if (bcfg.broadcast_on_channel.name[0] != '\0')
                strncpy(targetChannel.name, bcfg.broadcast_on_channel.name, sizeof(targetChannel.name) - 1);
            if (bcfg.broadcast_on_channel.psk.size > 0)
                targetChannel.psk = bcfg.broadcast_on_channel.psk;
        } else if (targetChannel.name[0] == '\0') {
            strncpy(targetChannel.name, DisplayFormatters::getModemPresetDisplayName(targetPreset, false, true),
                    sizeof(targetChannel.name) - 1);
        }
        targetChannel.name[sizeof(targetChannel.name) - 1] = '\0';

        if (targetPreset == config.lora.modem_preset && targetSlot == config.lora.channel_num &&
            targetRegion == config.lora.region && !channelDiffers(targetChannel))
            return false;

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
        originalPrimaryChannel = *c;

        LOG_INFO("Beacon: switch radio for packet %#08lx to preset=%d slot=%u region=%d", p->id, targetPreset, targetSlot,
                 targetRegion);
        config.lora.modem_preset = targetPreset;
        config.lora.channel_num = targetSlot;
        if (targetRegion != config.lora.region)
            config.lora.region = targetRegion;
        *c = targetChannel;

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
        *c = originalPrimaryChannel;
        c->name[sizeof(c->name) - 1] = '\0';

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
    beacon.has_offer_preset = bcfg.has_broadcast_offer_preset;
    beacon.offer_preset = bcfg.broadcast_offer_preset;
    beacon.offer_region = bcfg.broadcast_offer_region;
    payloadCacheSize = (pb_size_t)pb_encode_to_bytes(payloadCache, sizeof(payloadCache), &meshtastic_MeshBeacon_msg, &beacon);
    payloadCacheDirty = false;
    LOG_DEBUG("Beacon: payload cache rebuilt (%u bytes)", payloadCacheSize);
}

void MeshBeaconBroadcastModule::sendBeacon()
{
    const auto &bcfg = moduleConfig.mesh_beacon;

    const bool hasText = bcfg.broadcast_message[0] != '\0';
    const bool hasRadioContent = bcfg.has_broadcast_offer_preset || bcfg.has_broadcast_offer_channel ||
                                 (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET);

    if (!hasText && !hasRadioContent) {
        LOG_DEBUG("Beacon: nothing to send (empty message, no offer), skipping");
        return;
    }

    // Determine whether the TX radio config actually differs from the running config.
    // Used to gate sidecar insertion.
    const meshtastic_Config_LoRaConfig_ModemPreset targetPreset =
        bcfg.has_broadcast_on_preset ? bcfg.broadcast_on_preset : config.lora.modem_preset;
    const uint16_t targetSlot = bcfg.has_broadcast_on_channel ? bcfg.broadcast_on_channel.channel_num : config.lora.channel_num;
    const bool channelOverrideConfigured =
        bcfg.has_broadcast_on_channel && (bcfg.broadcast_on_channel.name[0] != '\0' || bcfg.broadcast_on_channel.psk.size > 0 ||
                                          bcfg.broadcast_on_channel.channel_num != config.lora.channel_num);
    const bool presetDiffers = (bcfg.has_broadcast_on_preset && targetPreset != config.lora.modem_preset) ||
                               (bcfg.broadcast_on_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET &&
                                bcfg.broadcast_on_region != config.lora.region) ||
                               channelOverrideConfigured;

    // Stamp common fields shared by every outgoing beacon packet.
    const auto stampPacket = [&](meshtastic_MeshPacket *p) {
        p->to = NODENUM_BROADCAST;
        p->from = (bcfg.broadcast_send_as_node != 0) ? bcfg.broadcast_send_as_node : nodeDB->getNodeNum();
        p->hop_limit = 0; // all beacon packets are zero hopped to limit spamming.
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->want_ack = false;
        p->rx_time = getValidTime(RTCQualityFromNet);
    };

    // ── Main packet(s) ──────────────────────────────────────────────────────
    //
    // broadcast_legacy_split: when both text and offer are present, send TWO packets,
    //   A) MESH_BEACON_APP carrying only the offer (no text) on the beacon config, and
    //   B) TEXT_MESSAGE_APP carrying only the message text on the normal config,
    // so nodes that only decode TEXT_MESSAGE_APP still receive the text. Otherwise a
    // single packet is sent (offer-only, text-only, or the combined offer+text path).
    //
    // These are independent decisions, NOT a mutually-exclusive if/else chain: the split
    // case must emit both A and B. Conditions are spelled out as named booleans to avoid
    // the && / || precedence trap (and a prior bug where the split case dropped the text).
    const bool splitBoth = bcfg.broadcast_legacy_split && hasRadioContent && hasText;
    const bool sendOfferOnly = splitBoth || (hasRadioContent && !hasText);
    const bool sendTextOnly = splitBoth || (!hasRadioContent && hasText);
    const bool sendCombined = !bcfg.broadcast_legacy_split && hasRadioContent && hasText;

    if (sendOfferOnly) {
        // Packet A: offer-only MESH_BEACON_APP (message field intentionally empty).
        meshtastic_MeshBeacon offerOnly = meshtastic_MeshBeacon_init_zero;
        if (bcfg.has_broadcast_offer_channel) {
            offerOnly.has_offer_channel = true;
            offerOnly.offer_channel = bcfg.broadcast_offer_channel;
        }
        offerOnly.has_offer_preset = bcfg.has_broadcast_offer_preset;
        offerOnly.offer_preset = bcfg.broadcast_offer_preset;
        offerOnly.offer_region = bcfg.broadcast_offer_region;

        uint8_t offerBuf[meshtastic_MeshBeacon_size] = {};
        pb_size_t offerSize = (pb_size_t)pb_encode_to_bytes(offerBuf, sizeof(offerBuf), &meshtastic_MeshBeacon_msg, &offerOnly);

        meshtastic_MeshPacket *pA = allocDataPacket();
        if (!pA) {
            LOG_WARN("Beacon: failed to allocate split-A packet");
            return;
        }
        memcpy(pA->decoded.payload.bytes, offerBuf, offerSize);
        pA->decoded.payload.size = offerSize;
        pA->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
        stampPacket(pA);
        if (presetDiffers)
            setTargetRadioSettings(pA, targetPreset, targetSlot);
        LOG_INFO("Beacon: split-A MESH_BEACON_APP (offer only) from=%#08lx", pA->from);
        router->send(pA);
    }

    if (sendTextOnly) {
        // Packet B: text-only TEXT_MESSAGE_APP on the beacon radio config (no sidecar).
        meshtastic_MeshPacket *pB = allocDataPacket();
        if (!pB) {
            LOG_WARN("Beacon: failed to allocate split-B packet");
            return;
        }
        pb_size_t msgLen = (pb_size_t)strnlen(bcfg.broadcast_message, sizeof(bcfg.broadcast_message) - 1);
        memcpy(pB->decoded.payload.bytes, bcfg.broadcast_message, msgLen);
        pB->decoded.payload.size = msgLen;
        pB->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        stampPacket(pB);
        if (presetDiffers)
            setTargetRadioSettings(pB, targetPreset, targetSlot);
        LOG_INFO("Beacon: split-B TEXT_MESSAGE_APP msg='%.40s' from=%#08lx", bcfg.broadcast_message, pB->from);
        router->send(pB);
    }

    if (sendCombined) {
        // Combined path: MESH_BEACON_APP carrying offer + optional message text.
        if (payloadCacheDirty)
            rebuildCache();
        meshtastic_MeshPacket *p = allocDataPacket();
        if (!p) {
            LOG_WARN("Beacon: failed to allocate beacon packet");
            return;
        }
        memcpy(p->decoded.payload.bytes, payloadCache, payloadCacheSize);
        p->decoded.payload.size = payloadCacheSize;
        p->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
        stampPacket(p);
        if (presetDiffers)
            setTargetRadioSettings(p, targetPreset, targetSlot);
        LOG_INFO("Beacon: MESH_BEACON_APP offer+msg from=%#08lx msg='%.40s'", p->from, bcfg.broadcast_message);
        router->send(p);
    }
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
    const bool hasOfferContent =
        b && (b->has_offer_channel || b->offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET || b->has_offer_preset);
    if (!b || (strlen(b->message) == 0 && !hasOfferContent))
        return false;

    if (strlen(b->message) > 0)
        LOG_INFO("Beacon: received from %#08lx: '%.40s'", mp.from, b->message);

    if (strlen(b->message) > 0) {
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
    }

    // Cache any offer for the client app — never auto-applied.
    if (hasOfferContent) {
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

    if (strlen(b->message) > 0)
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);
    return false;
}
