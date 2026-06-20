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
                                   uint16_t *slot, bool *legacyHopOverride = nullptr,
                                   meshtastic_Config_LoRaConfig_RegionCode *region = nullptr, bool *has_channel = nullptr,
                                   meshtastic_ChannelSettings *channel = nullptr)
{
    if (!p)
        return false;
    for (const auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == p->id) {
            if (preset)
                *preset = entry.preset;
            if (slot)
                *slot = entry.slot;
            if (legacyHopOverride)
                *legacyHopOverride = entry.legacyHopOverride;
            if (region)
                *region = entry.region;
            if (has_channel)
                *has_channel = entry.has_channel;
            if (channel && entry.has_channel)
                *channel = entry.channel;
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
                                              uint16_t slot, bool legacyHopOverride,
                                              meshtastic_Config_LoRaConfig_RegionCode region, bool has_channel,
                                              const meshtastic_ChannelSettings *channel)
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
    target->legacyHopOverride = legacyHopOverride;
    target->region = region;
    target->has_channel = has_channel;
    if (has_channel && channel)
        target->channel = *channel;
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

bool MeshBeaconModule::beaconTxConfigInvalid(const meshtastic_MeshPacket *p)
{
    meshtastic_Config_LoRaConfig_ModemPreset preset;
    meshtastic_Config_LoRaConfig_RegionCode sidecarRegion = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    if (!getTargetRadioSettings(p, &preset, nullptr, nullptr, &sidecarRegion))
        return false; // not a beacon-switch packet — nothing to validate, normal traffic unaffected

    const meshtastic_Config_LoRaConfig_RegionCode region =
        (sidecarRegion != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? sidecarRegion : config.lora.region;

    // An unlicensed node must never key up on a ham-only (licensed-only) region. The reverse is
    // allowed: a licensed (ham) node may operate in a non-ham region — and the switch only touches
    // preset/region/channel, never owner.is_licensed, so it cannot deactivate licensed mode.
    const RegionInfo *r = getRegion(region);
    if (r && r->profile->licensedOnly && !owner.is_licensed)
        return true;

    // Preset must be valid for the target region.
    meshtastic_Config_LoRaConfig probe = config.lora;
    probe.use_preset = true;
    probe.modem_preset = preset;
    probe.region = region;
    return !RadioInterface::validateConfigLora(probe);
}

meshtastic_ChannelSettings MeshBeaconModule::beaconChannelSettings(const meshtastic_ChannelSettings &base,
                                                                   meshtastic_Config_LoRaConfig_ModemPreset preset,
                                                                   const meshtastic_ChannelSettings *overrideChannel)
{
    meshtastic_ChannelSettings ch = base;
    if (overrideChannel) {
        ch.channel_num = overrideChannel->channel_num;
        if (overrideChannel->name[0] != '\0')
            strncpy(ch.name, overrideChannel->name, sizeof(ch.name) - 1);
        if (overrideChannel->psk.size > 0)
            ch.psk = overrideChannel->psk;
    } else if (ch.name[0] == '\0') {
        strncpy(ch.name, DisplayFormatters::getModemPresetDisplayName(preset, false, true), sizeof(ch.name) - 1);
    }
    ch.name[sizeof(ch.name) - 1] = '\0';
    return ch;
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

    bool legacyHopOverride = false;
    meshtastic_Config_LoRaConfig_RegionCode sidecarRegion = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    bool sidecarHasChannel = false;
    meshtastic_ChannelSettings sidecarChannel = {};
    if (p && getTargetRadioSettings(p, &targetPreset, &targetSlot, &legacyHopOverride, &sidecarRegion, &sidecarHasChannel,
                                    &sidecarChannel)) {

        // Legacy compatibility: older firmware (pre-v2.7.20) drops hop_start==0 packets via the
        // pre-hop check before decryption, so they can't see has_bitfield to validate them.
        // Setting hop_start=1 (with hop_limit remaining 0) makes the packet pass the old check
        // while still being zero-hop (hop_limit=0 prevents any rebroadcast).
        if (legacyHopOverride)
            p->hop_start = 1;

        const meshtastic_Config_LoRaConfig_RegionCode targetRegion =
            (sidecarRegion != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? sidecarRegion : config.lora.region;
        const meshtastic_ChannelSettings *overrideCh = sidecarHasChannel ? &sidecarChannel : nullptr;

        meshtastic_ChannelSettings targetChannel = beaconChannelSettings(*c, targetPreset, overrideCh);

        if (targetPreset == config.lora.modem_preset && targetSlot == config.lora.channel_num &&
            targetRegion == config.lora.region && !channelDiffers(targetChannel))
            return false;

        // Guard: never key up on an invalid target config — bad preset for the region, or an
        // unlicensed node keying up on a ham-only region. Refuse the switch here so we never
        // transmit on it; the radio driver drops the packet outright (see RadioLibInterface,
        // beaconTxConfigInvalid) rather than letting it fall through onto the current config.
        if (beaconTxConfigInvalid(p)) {
            LOG_DEBUG("Beacon: target preset %d/region %d invalid (or ham mismatch), not switching", targetPreset, targetRegion);
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

void MeshBeaconBroadcastModule::sendBeaconPacket(meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset targetPreset,
                                                 bool has_channel, const meshtastic_ChannelSettings *overrideChannel)
{
    const bool cryptoOverride =
        has_channel && overrideChannel && (overrideChannel->name[0] != '\0' || overrideChannel->psk.size > 0);
    if (!cryptoOverride) {
        router->send(p);
        return;
    }

    // perhapsEncode() keys encryption (and the channel-hash hint) off the PRIMARY channel slot, and
    // the radio-thread channel switch only happens AFTER encryption — so a beacon on an override
    // channel would otherwise be encrypted with the PRIMARY PSK, not the beacon channel's. Install the
    // beacon channel into the primary slot for the synchronous duration of send(), then restore.
    // Meshtastic threading is cooperative (no preemption between the swap and restore).
    meshtastic_Channel &primary = channels.getByIndex(channels.getPrimaryIndex());
    const meshtastic_ChannelSettings saved = primary.settings;
    primary.settings = beaconChannelSettings(saved, targetPreset, overrideChannel);
    channels.fixupChannel(channels.getPrimaryIndex());

    router->send(p); // encrypts with the beacon channel's key and stamps its hash

    primary.settings = saved;
    channels.fixupChannel(channels.getPrimaryIndex());
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

    // Stamp common fields shared by every outgoing beacon packet.
    const auto stampPacket = [&](meshtastic_MeshPacket *p) {
        p->to = NODENUM_BROADCAST;
        // broadcast_send_as_node overrides the source NodeNum. NOTE: this is a *node-ID* spoof
        // only — it rewrites the 'from' field but does NOT forge any signature. Once 'from' is
        // not us, the packet is no longer isFromUs(), so Router::perhapsEncode() skips XEdDSA
        // signing and receivers get an unsigned packet attributed to another node.
        // When broadcast_send_as_node == 0 the beacon is genuinely from us and Router::perhapsEncode()
        // signs it under the same XEdDSA broadcast policy as normal channel messages (signed iff the
        // payload + signature fits the Data payload) — beacons need no special-case signing here.
        p->from = (bcfg.broadcast_send_as_node != 0) ? bcfg.broadcast_send_as_node : nodeDB->getNodeNum();
        p->hop_limit = 0; // all beacon packets are zero hopped to limit spamming.
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->want_ack = false;
        p->rx_time = getValidTime(RTCQualityFromNet);
        // When broadcast_send_as_node rewrites p->from, perhapsEncode() sees isFromUs()=false and
        // skips setting has_bitfield. Set it here so receivers can classify hop_start correctly
        // and so ok_to_mqtt is honoured on the spoofed packet.
        if (bcfg.broadcast_send_as_node != 0) {
            p->decoded.has_bitfield = true;
            p->decoded.bitfield |= (config.lora.config_ok_to_mqtt << BITFIELD_OK_TO_MQTT_SHIFT);
        }
    };

    // ── Packet type decisions ────────────────────────────────────────────────
    //
    // broadcast_legacy_split: when both text and offer are present, send TWO packets — A)
    //   MESH_BEACON_APP (offer only) and B) TEXT_MESSAGE_APP (text only) — both on the SAME beacon
    // radio settings, so nodes that only decode TEXT_MESSAGE_APP still receive the text. Otherwise a
    // single packet is sent (offer-only, text-only, or the combined offer+text path).
    //
    // These are independent decisions, NOT a mutually-exclusive if/else chain: the split
    // case must emit both A and B. Conditions are spelled out as named booleans to avoid
    // the && / || precedence trap (and a prior bug where the split case dropped the text).
    const bool splitBoth = bcfg.broadcast_legacy_split && hasRadioContent && hasText;
    const bool sendOfferOnly = splitBoth || (hasRadioContent && !hasText);
    const bool sendTextOnly = splitBoth || (!hasRadioContent && hasText);
    const bool sendCombined = !bcfg.broadcast_legacy_split && hasRadioContent && hasText;

    // Build offer payload once — shared across all targets.
    uint8_t offerBuf[meshtastic_MeshBeacon_size] = {};
    pb_size_t offerSize = 0;
    if (sendOfferOnly) {
        meshtastic_MeshBeacon offerOnly = meshtastic_MeshBeacon_init_zero;
        if (bcfg.has_broadcast_offer_channel) {
            offerOnly.has_offer_channel = true;
            offerOnly.offer_channel = bcfg.broadcast_offer_channel;
        }
        offerOnly.has_offer_preset = bcfg.has_broadcast_offer_preset;
        offerOnly.offer_preset = bcfg.broadcast_offer_preset;
        offerOnly.offer_region = bcfg.broadcast_offer_region;
        offerSize = (pb_size_t)pb_encode_to_bytes(offerBuf, sizeof(offerBuf), &meshtastic_MeshBeacon_msg, &offerOnly);
    }
    if (sendCombined && payloadCacheDirty)
        rebuildCache();

    // ── Per-target loop ──────────────────────────────────────────────────────
    //
    // If broadcast_targets is populated, iterate over those. Otherwise use the legacy
    // single-target broadcast_on_preset / broadcast_on_region / broadcast_on_channel fields.
    struct EffTarget {
        meshtastic_Config_LoRaConfig_ModemPreset preset;
        uint16_t slot;
        meshtastic_Config_LoRaConfig_RegionCode region;
        bool has_channel;
        meshtastic_ChannelSettings channel;
    };

    const bool useTargetList = bcfg.broadcast_targets_count > 0;
    const int targetCount = useTargetList ? (int)bcfg.broadcast_targets_count : 1;

    for (int ti = 0; ti < targetCount; ti++) {
        EffTarget tgt = {};
        if (useTargetList) {
            const auto &bt = bcfg.broadcast_targets[ti];
            tgt.preset = bt.has_preset ? bt.preset : config.lora.modem_preset;
            tgt.region = bt.region;
            tgt.has_channel = bt.has_channel;
            if (tgt.has_channel)
                tgt.channel = bt.channel;
            tgt.slot = tgt.has_channel ? bt.channel.channel_num : config.lora.channel_num;
        } else {
            tgt.preset = bcfg.has_broadcast_on_preset ? bcfg.broadcast_on_preset : config.lora.modem_preset;
            tgt.region = bcfg.broadcast_on_region;
            tgt.has_channel = bcfg.has_broadcast_on_channel;
            if (tgt.has_channel)
                tgt.channel = bcfg.broadcast_on_channel;
            tgt.slot = tgt.has_channel ? bcfg.broadcast_on_channel.channel_num : config.lora.channel_num;
        }

        const bool channelOverrideConfigured = tgt.has_channel && (tgt.channel.name[0] != '\0' || tgt.channel.psk.size > 0 ||
                                                                   tgt.channel.channel_num != config.lora.channel_num);
        const bool presetDiffers =
            (tgt.preset != config.lora.modem_preset) ||
            (tgt.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET && tgt.region != config.lora.region) ||
            channelOverrideConfigured;
        const meshtastic_ChannelSettings *chPtr = tgt.has_channel ? &tgt.channel : nullptr;

        const auto applyTarget = [&](meshtastic_MeshPacket *p) {
            if (presetDiffers || bcfg.broadcast_legacy_split)
                setTargetRadioSettings(p, tgt.preset, tgt.slot, bcfg.broadcast_legacy_split, tgt.region, tgt.has_channel, chPtr);
            sendBeaconPacket(p, tgt.preset, tgt.has_channel, chPtr);
        };

        if (sendOfferOnly) {
            meshtastic_MeshPacket *pA = allocDataPacket();
            if (!pA) {
                LOG_WARN("Beacon: failed to allocate split-A packet (target %d)", ti);
                return;
            }
            memcpy(pA->decoded.payload.bytes, offerBuf, offerSize);
            pA->decoded.payload.size = offerSize;
            pA->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
            stampPacket(pA);
            LOG_INFO("Beacon: split-A MESH_BEACON_APP (offer only) from=%#08lx target=%d", pA->from, ti);
            applyTarget(pA);
        }

        if (sendTextOnly) {
            meshtastic_MeshPacket *pB = allocDataPacket();
            if (!pB) {
                LOG_WARN("Beacon: failed to allocate split-B packet (target %d)", ti);
                return;
            }
            pb_size_t msgLen = (pb_size_t)strnlen(bcfg.broadcast_message, sizeof(bcfg.broadcast_message) - 1);
            memcpy(pB->decoded.payload.bytes, bcfg.broadcast_message, msgLen);
            pB->decoded.payload.size = msgLen;
            pB->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            stampPacket(pB);
            LOG_INFO("Beacon: split-B TEXT_MESSAGE_APP msg='%.40s' from=%#08lx target=%d", bcfg.broadcast_message, pB->from, ti);
            applyTarget(pB);
        }

        if (sendCombined) {
            meshtastic_MeshPacket *p = allocDataPacket();
            if (!p) {
                LOG_WARN("Beacon: failed to allocate beacon packet (target %d)", ti);
                return;
            }
            memcpy(p->decoded.payload.bytes, payloadCache, payloadCacheSize);
            p->decoded.payload.size = payloadCacheSize;
            p->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
            stampPacket(p);
            LOG_INFO("Beacon: MESH_BEACON_APP offer+msg from=%#08lx msg='%.40s' target=%d", p->from, bcfg.broadcast_message, ti);
            applyTarget(p);
        }
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
    const pb_size_t msgLen = b ? (pb_size_t)strnlen(b->message, sizeof(b->message) - 1) : 0;
    const bool hasText = msgLen > 0;
    if (!b || (!hasText && !hasOfferContent))
        return false;

    if (hasText) {
        LOG_INFO("Beacon: received from %#08lx: '%.40s'", mp.from, b->message);
        // Surface the text to the locally connected phone/client ONLY, as a TEXT_MESSAGE_APP
        // packet. This is a local inbox delivery, NOT a mesh retransmit: we use sendToPhone()
        // (queues toward the phone) rather than handleToRadio(), which would re-inject the
        // packet into the mesh from this node — amplifying traffic and re-attributing the text
        // to us. The original 'from' (the real beaconer) is preserved for the phone.
        meshtastic_MeshPacket *txt = packetPool.allocCopy(mp);
        if (!txt) {
            LOG_WARN("Beacon: failed to alloc inbox copy");
            return false;
        }
        txt->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        txt->to = nodeDB->getNodeNum();
        memset(txt->decoded.payload.bytes, 0, sizeof(txt->decoded.payload.bytes));
        txt->decoded.payload.size = msgLen;
        memcpy(txt->decoded.payload.bytes, b->message, txt->decoded.payload.size);
        service->sendToPhone(txt); // takes ownership of txt — do not release
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
        lastReceivedOffer.received_at =
            getValidTime(RTCQualityFromNet); // 0 if no RTC fix yet — consumers must not treat 0 as valid
        LOG_INFO("Beacon: stored offer from %#08lx (preset=%d)", mp.from, b->offer_preset);
    }

    if (hasText)
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);
    return false;
}
