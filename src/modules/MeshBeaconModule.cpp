#include "MeshBeaconModule.h"
#include "Default.h"
#include "DisplayFormatters.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Router.h"
#include "TransmitHistory.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "main.h"
#include <Throttle.h>
#include <string.h>

// Static members
meshtastic_Config_LoRaConfig_ModemPreset MeshBeaconModule::originalModemPreset;
uint16_t MeshBeaconModule::originalLoraChannel;
meshtastic_Config_LoRaConfig_RegionCode MeshBeaconModule::originalRegion;
meshtastic_ChannelSettings MeshBeaconModule::originalPrimaryChannel;

static MeshBeaconModule_TargetRadioSettings targetRadioSettings[8];

// Explicit switch state, not inferred: "live config differs from the snapshot" missed name/PSK-only
// swaps and fired on legitimate channel edits.
static bool radioSwitched = false;
static uint32_t switchedForId = 0;

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

// Is a target entry still live for this packet id? Unlike sendingPacket or the radio's standby
// state, this is our own bookkeeping - it answers "has that beacon finished" without asking the radio.
static bool targetRadioSettingsLive(uint32_t id)
{
    for (const auto &entry : targetRadioSettings)
        if (entry.inUse && entry.id == id)
            return true;
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
    if (!target) {
        // Table full. Never evict the entry the outstanding switch is gated on: dropping it would
        // unblock the restore and put the home config back under a beacon that has not keyed up.
        for (auto &entry : targetRadioSettings) {
            if (!radioSwitched || entry.id != switchedForId) {
                target = &entry;
                break;
            }
        }
        if (!target) {
            LOG_WARN("Beacon: target table full and every slot is in flight, drop target for 0x%08x", p->id);
            return;
        }
        LOG_WARN("Beacon: target table full (%u slots), evicting packet 0x%08x for 0x%08x",
                 (unsigned)(sizeof(targetRadioSettings) / sizeof(targetRadioSettings[0])), target->id, p->id);
    }
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

// Only ERRNO_OK means the interface queued the packet and now owns its target radio settings too.
// Any other return means it never reaches the radio, so free the settings entry here or it leaks.
static void releaseIfNotQueued(ErrorCode sendResult, meshtastic_MeshPacket *p)
{
    if (sendResult == ERRNO_OK)
        return;
    MeshBeaconModule::clearTargetRadioSettings(p);
    if (sendResult == ERRNO_SHOULD_RELEASE)
        packetPool.release(p); // this one is still ours to free
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
    uint16_t slot = 0;
    meshtastic_Config_LoRaConfig_RegionCode sidecarRegion = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    bool sidecarHasChannel = false;
    meshtastic_ChannelSettings sidecarChannel = {};
    if (!getTargetRadioSettings(p, &preset, &slot, nullptr, &sidecarRegion, &sidecarHasChannel, &sidecarChannel))
        return false; // not a beacon-switch packet - nothing to validate, normal traffic unaffected

    const meshtastic_Config_LoRaConfig_RegionCode region =
        (sidecarRegion != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? sidecarRegion : config.lora.region;

    // An unlicensed node must never key up on a ham-only (licensed-only) region. The reverse is
    // allowed: a licensed (ham) node may operate in a non-ham region - and the switch only touches
    // preset/region/channel, never owner.is_licensed, so it cannot deactivate licensed mode.
    const RegionInfo *r = getRegion(region);
    if (r && r->profile->licensedOnly && !owner.is_licensed)
        return true;

    // The slot is picked by hashing the channel name, so evaluate against the channel this target
    // will install as primary, not the running one. Never empty - beaconChannelSettings fills it.
    const meshtastic_ChannelSettings targetChannel = beaconChannelSettings(
        channels.getByIndex(channels.getPrimaryIndex()).settings, preset, sidecarHasChannel ? &sidecarChannel : nullptr);

    meshtastic_Config_LoRaConfig probe = config.lora;
    probe.use_preset = true;
    probe.modem_preset = preset;
    probe.region = region;
    probe.channel_num = slot;
    return !RadioInterface::validateConfigLora(probe, targetChannel.name);
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
    }
    // If no usable name survived (no override, or a blank-named one), default to the preset's
    // display name so the beacon channel is identifiable rather than borrowing the primary's name.
    if (ch.name[0] == '\0')
        strncpy(ch.name, DisplayFormatters::getModemPresetDisplayName(preset, false, true), sizeof(ch.name) - 1);
    ch.name[sizeof(ch.name) - 1] = '\0';
    return ch;
}

bool MeshBeaconModule::reconfigureForBeaconTX(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    // Consecutive switches with no restore between them, so a multi-target run can be read off the log
    // and the held home snapshot is attributable to a specific switch.
    static uint8_t switchDepth = 0;

    // Both branches end in iface->reconfigure(), whose setStandby() runs completeSending() and calls
    // straight back in here. Ignore that re-entry: the outer call owns the config it is applying.
    static bool applying = false;
    if (applying) {
        // Expected once per switch and once per restore. A burst of these means something new re-enters.
        LOG_DEBUG("Beacon: ignore re-entrant reconfigure while a radio config is being applied");
        return false;
    }
    struct ApplyingScope {
        bool &flag;
        explicit ApplyingScope(bool &f) : flag(f) { flag = true; }
        ~ApplyingScope() { flag = false; }
    } applyingScope(applying);

    meshtastic_ChannelSettings *primaryCh = &channels.getByIndex(channels.getPrimaryIndex()).settings;
    meshtastic_Config_LoRaConfig_ModemPreset targetPreset;
    uint16_t targetSlot;

    const auto channelDiffers = [&](const meshtastic_ChannelSettings &target) {
        return strncmp(primaryCh->name, target.name, sizeof(primaryCh->name)) != 0 || primaryCh->psk.size != target.psk.size ||
               memcmp(primaryCh->psk.bytes, target.psk.bytes, primaryCh->psk.size) != 0 ||
               primaryCh->channel_num != target.channel_num;
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

        meshtastic_ChannelSettings targetChannel = beaconChannelSettings(*primaryCh, targetPreset, overrideCh);

        if (targetPreset == config.lora.modem_preset && targetSlot == config.lora.channel_num &&
            targetRegion == config.lora.region && !channelDiffers(targetChannel))
            return false;

        // Guard: never key up on an invalid target config - bad preset for the region, or an
        // unlicensed node keying up on a ham-only region. Refuse the switch here so we never
        // transmit on it; the radio driver drops the packet outright (see RadioLibInterface,
        // beaconTxConfigInvalid) rather than letting it fall through onto the current config.
        if (beaconTxConfigInvalid(p)) {
            LOG_DEBUG("Beacon: target preset %d/region %d invalid (or ham mismatch), skip", targetPreset, targetRegion);
            return false;
        }

        // Snapshot the live (non-beacon) config as "home". Skipped while a switch is already active,
        // so a second switch before the restore cannot capture the beacon config instead.
        if (!radioSwitched) {
            originalModemPreset = config.lora.modem_preset;
            originalLoraChannel = config.lora.channel_num;
            originalRegion = config.lora.region;
            originalPrimaryChannel = *primaryCh;
            switchDepth = 0;
        }
        switchDepth++;

        LOG_INFO("Beacon: switch #%u radio for packet 0x%08x to preset=%d slot=%u region=%d", switchDepth, p->id, targetPreset,
                 targetSlot, targetRegion);
        if (switchDepth > 1)
            LOG_WARN("Beacon: switching again with no restore between; home preset=%d slot=%u region=%d still held",
                     originalModemPreset, originalLoraChannel, originalRegion);
        config.lora.modem_preset = targetPreset;
        config.lora.channel_num = targetSlot;
        if (targetRegion != config.lora.region)
            config.lora.region = targetRegion;
        *primaryCh = targetChannel;

        channels.fixupChannel(channels.getPrimaryIndex());
        p->channel = channels.getHash(channels.getPrimaryIndex());
        radioSwitched = true; // set before reconfigure(), so the flag never lags the radio it describes
        switchedForId = p->id;
        iface->reconfigure();
        return true;

    } else if ((!p || !getTargetRadioSettings(p, nullptr, nullptr)) && radioSwitched) {

        // Null p is "release if nothing holds it": hold off until the arming beacon has finished. A
        // non-null untagged p is the driver about to transmit it, so that always restores.
        if (!p && targetRadioSettingsLive(switchedForId)) {
            LOG_DEBUG("Beacon: skip restore, packet 0x%08x has not finished sending", switchedForId);
            return false;
        }

        LOG_INFO("Beacon: restore radio config after TX, undoing %u switch(es) -> preset=%d slot=%u region=%d", switchDepth,
                 originalModemPreset, originalLoraChannel, originalRegion);
        config.lora.modem_preset = originalModemPreset;
        config.lora.channel_num = originalLoraChannel;
        config.lora.region = originalRegion;
        *primaryCh = originalPrimaryChannel;
        primaryCh->name[sizeof(primaryCh->name) - 1] = '\0';

        channels.fixupChannel(channels.getPrimaryIndex());
        radioSwitched = false; // cleared before reconfigure(), so the flag never lags the radio it describes
        switchDepth = 0;
        switchedForId = 0;
        iface->reconfigure();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MeshBeaconTxHook
// ---------------------------------------------------------------------------

MeshBeaconTxHook *meshBeaconTxHook;

RadioTxHook::PreTxAction MeshBeaconTxHook::beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    // Invalid target config (bad preset/region, or an unlicensed node keying up on a ham-only
    // region): the packet must never fall through onto the current (home) config, so drop it.
    if (MeshBeaconModule::beaconTxConfigInvalid(p)) {
        LOG_DEBUG("Beacon: invalid TX radio config, drop packet 0x%08x", p->id);
        return PRETX_DROP;
    }
    // A switch leaves the radio on a channel we have not scanned yet, so the driver owes us a
    // fresh transmit delay before it keys up.
    return MeshBeaconModule::reconfigureForBeaconTX(iface, p) ? PRETX_DEFER : PRETX_SEND;
}

bool MeshBeaconTxHook::holdsRadio(const meshtastic_MeshPacket *p)
{
    return MeshBeaconModule::hasTargetRadioSettings(p);
}

void MeshBeaconTxHook::packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p)
{
    // Clear first: the restore is gated on the switching packet still being live, so dropping our
    // claim before asking is what lets the home config come back.
    MeshBeaconModule::clearTargetRadioSettings(p);
    MeshBeaconModule::reconfigureForBeaconTX(iface, nullptr);
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
        // The offered channel is not secret - the PSK here is a convenience token,
        // not a security boundary.  Operators who want a private channel must
        // distribute the PSK out-of-band and leave offer_channel unset.
    }
    beacon.has_offer_preset = bcfg.has_broadcast_offer_preset;
    beacon.offer_preset = bcfg.broadcast_offer_preset;
    beacon.offer_region = bcfg.broadcast_offer_region;
    // Note: an empty config legitimately encodes to 0 bytes, and pb_encode_to_bytes can't distinguish
    // that from a (here effectively impossible - buffer is max-sized) failure, so we always clear the
    // dirty flag. The combined send is gated on payloadCacheSize > 0, so an empty payload is never TX'd.
    payloadCacheSize = (pb_size_t)pb_encode_to_bytes(payloadCache, sizeof(payloadCache), &meshtastic_MeshBeacon_msg, &beacon);
    payloadCacheDirty = false;
    LOG_DEBUG("Beacon: payload cache rebuilt (%u bytes)", payloadCacheSize);
}

void MeshBeaconBroadcastModule::sendBeaconPacket(meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset targetPreset,
                                                 bool has_channel, const meshtastic_ChannelSettings *overrideChannel)
{
    // Beacons uplink to MQTT like any other primary-slot packet - Router::send() publishes on slot 0's
    // uplink_enabled, and under the swap below the topic is the beacon channel's. Both intentional.
    const bool cryptoOverride =
        has_channel && overrideChannel && (overrideChannel->name[0] != '\0' || overrideChannel->psk.size > 0);
    if (!cryptoOverride) {
        releaseIfNotQueued(router->send(p), p);
        return;
    }

    // perhapsEncode() keys encryption (and the channel-hash hint) off the PRIMARY channel slot, and
    // the radio-thread channel switch only happens AFTER encryption - so a beacon on an override
    // channel would otherwise be encrypted with the PRIMARY PSK, not the beacon channel's. Install the
    // beacon channel into the primary slot for the synchronous duration of send(), then restore.
    // Meshtastic threading is cooperative (no preemption between the swap and restore).
    meshtastic_Channel &primary = channels.getByIndex(channels.getPrimaryIndex());
    const meshtastic_ChannelSettings saved = primary.settings;
    primary.settings = beaconChannelSettings(saved, targetPreset, overrideChannel);
    channels.fixupChannel(channels.getPrimaryIndex());

    // encrypts with the beacon channel's key and stamps its hash
    releaseIfNotQueued(router->send(p), p);

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
        LOG_DEBUG("Beacon: empty msg, no offer, skip");
        return;
    }

    // Stamp common fields shared by every outgoing beacon packet.
    const auto stampPacket = [&](meshtastic_MeshPacket *p) {
        p->to = NODENUM_BROADCAST;
        p->from = nodeDB->getNodeNum();
        p->hop_limit = 0; // all beacon packets are zero hopped to limit spamming.
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->want_ack = false;
        stampRxTime(p);
    };

    // ── Packet type decisions ────────────────────────────────────────────────
    //
    // FLAG_LEGACY_SPLIT: when both text and offer are present, send TWO packets - A)
    //   MESH_BEACON_APP (offer only) and B) TEXT_MESSAGE_APP (text only) - both on the SAME beacon
    // radio settings, so nodes that only decode TEXT_MESSAGE_APP still receive the text. Otherwise a
    // single packet is sent (offer-only, text-only, or the combined offer+text path).
    //
    // These are independent decisions, NOT a mutually-exclusive if/else chain: the split
    // case must emit both A and B. Conditions are spelled out as named booleans to avoid
    // the && / || precedence trap (and a prior bug where the split case dropped the text).
    const bool legacySplit = bcfg.flags & MESH_BEACON_FLAG_LEGACY_SPLIT;
    const bool splitBoth = legacySplit && hasRadioContent && hasText;
    const bool sendOfferOnly = splitBoth || (hasRadioContent && !hasText);
    const bool sendTextOnly = splitBoth || (!hasRadioContent && hasText);
    const bool sendCombined = !legacySplit && hasRadioContent && hasText;

    // Build offer payload once - shared across all targets.
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
        if (offerSize == 0)
            LOG_WARN("Beacon: offer encode failed, skip");
    }
    if (sendCombined && payloadCacheDirty)
        rebuildCache();

    // ── Per-target loop ──────────────────────────────────────────────────────
    //
    // Every destination comes from broadcast_targets. An entry names its TX channel by
    // channel_index, a slot in the device's channel table, so the channel must already be
    // configured on the node - its key is needed to encrypt.
    struct EffTarget {
        meshtastic_Config_LoRaConfig_ModemPreset preset;
        uint16_t slot;
        meshtastic_Config_LoRaConfig_RegionCode region;
        bool has_channel;
        meshtastic_ChannelSettings channel;
    };

    // An empty list still beacons once, on the node's running preset and region over the primary
    // channel. Each entry below overrides only what it sets.
    const int targetCount = bcfg.broadcast_targets_count > 0 ? (int)bcfg.broadcast_targets_count : 1;

    // Dedup state: the beacon payload is identical across targets, so two targets that resolve to
    // the same effective radio config (preset + resolved region + channel) would just re-broadcast
    // the same packet - wasted airtime and a redundant radio switch each. We skip the later one.
    // Keyed on the *resolved* values so an explicit "current region" dedups against an UNSET one.
    EffTarget sent[4];
    meshtastic_Config_LoRaConfig_RegionCode sentRegion[4];
    int sentCount = 0;
    const auto sameEffectiveTarget = [](const EffTarget &a, meshtastic_Config_LoRaConfig_RegionCode ar, const EffTarget &b,
                                        meshtastic_Config_LoRaConfig_RegionCode br) {
        if (a.preset != b.preset || ar != br || a.has_channel != b.has_channel)
            return false;
        if (!a.has_channel)
            return true; // both fall back to the default channel for the (same) preset
        return a.slot == b.slot && strncmp(a.channel.name, b.channel.name, sizeof(a.channel.name)) == 0 &&
               a.channel.psk.size == b.channel.psk.size &&
               memcmp(a.channel.psk.bytes, b.channel.psk.bytes, a.channel.psk.size) == 0;
    };

    for (int ti = 0; ti < targetCount; ti++) {
        // Defaults: running radio config, primary channel. A target entry overrides from here.
        EffTarget tgt = {};
        tgt.preset = config.lora.modem_preset;
        tgt.slot = config.lora.channel_num;
        if (ti < (int)bcfg.broadcast_targets_count) {
            const auto &bt = bcfg.broadcast_targets[ti];
            if (bt.has_preset)
                tgt.preset = bt.preset;
            tgt.region = bt.region;
            // Resolve the channel from the device's channel table by index. A slot is only usable
            // if it is actually configured (has a name or PSK - its key is needed to encrypt). An
            // out-of-range index, or a blank slot, falls back to the default channel for the target
            // preset (see beaconChannelSettings), exactly as an unset channel_index would.
            if (bt.has_channel_index) {
                if (bt.channel_index >= (uint32_t)channels.getNumChannels()) {
                    LOG_WARN("Beacon: target %d channel_index %u out of range, use preset default", ti, bt.channel_index);
                } else {
                    const meshtastic_ChannelSettings &cs = channels.getByIndex(bt.channel_index).settings;
                    if (cs.name[0] != '\0' || cs.psk.size > 0) {
                        tgt.has_channel = true;
                        tgt.channel = cs;
                        tgt.slot = cs.channel_num;
                    } else {
                        LOG_DEBUG("Beacon: target %d channel_index %u blank, use preset default", ti, bt.channel_index);
                    }
                }
            }
        }

        // Skip a target whose effective radio config duplicates one already sent this cycle.
        const meshtastic_Config_LoRaConfig_RegionCode resolvedRegion =
            (tgt.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? tgt.region : config.lora.region;
        bool duplicate = false;
        for (int si = 0; si < sentCount; si++) {
            if (sameEffectiveTarget(tgt, resolvedRegion, sent[si], sentRegion[si])) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            LOG_DEBUG("Beacon: target %d dup radio config, skip", ti);
            continue;
        }
        sent[sentCount] = tgt;
        sentRegion[sentCount] = resolvedRegion;
        sentCount++;

        const bool channelOverrideConfigured = tgt.has_channel && (tgt.channel.name[0] != '\0' || tgt.channel.psk.size > 0 ||
                                                                   tgt.channel.channel_num != config.lora.channel_num);
        const bool presetDiffers =
            (tgt.preset != config.lora.modem_preset) ||
            (tgt.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET && tgt.region != config.lora.region) ||
            channelOverrideConfigured;
        const meshtastic_ChannelSettings *chPtr = tgt.has_channel ? &tgt.channel : nullptr;

        const auto applyTarget = [&](meshtastic_MeshPacket *p) {
            if (presetDiffers || legacySplit)
                setTargetRadioSettings(p, tgt.preset, tgt.slot, legacySplit, tgt.region, tgt.has_channel, chPtr);
            sendBeaconPacket(p, tgt.preset, tgt.has_channel, chPtr);
        };

        if (sendOfferOnly && offerSize > 0) {
            meshtastic_MeshPacket *pA = allocDataPacket();
            if (!pA) {
                LOG_WARN("Beacon: split-A alloc failed (target %d)", ti);
                return;
            }
            memcpy(pA->decoded.payload.bytes, offerBuf, offerSize);
            pA->decoded.payload.size = offerSize;
            pA->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
            stampPacket(pA);
            LOG_INFO("Beacon: split-A MESH_BEACON_APP (offer only) from=0x%08x target=%d", pA->from, ti);
            applyTarget(pA);
        }

        if (sendTextOnly) {
            meshtastic_MeshPacket *pB = allocDataPacket();
            if (!pB) {
                LOG_WARN("Beacon: split-B alloc failed (target %d)", ti);
                return;
            }
            pb_size_t msgLen = (pb_size_t)strnlen(bcfg.broadcast_message, sizeof(bcfg.broadcast_message) - 1);
            memcpy(pB->decoded.payload.bytes, bcfg.broadcast_message, msgLen);
            pB->decoded.payload.size = msgLen;
            pB->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            stampPacket(pB);
            LOG_INFO("Beacon: split-B TEXT_MESSAGE_APP msg='%.40s' from=0x%08x target=%d", bcfg.broadcast_message, pB->from, ti);
            applyTarget(pB);
        }

        if (sendCombined && payloadCacheSize > 0) {
            meshtastic_MeshPacket *p = allocDataPacket();
            if (!p) {
                LOG_WARN("Beacon: failed to allocate beacon packet (target %d)", ti);
                return;
            }
            memcpy(p->decoded.payload.bytes, payloadCache, payloadCacheSize);
            p->decoded.payload.size = payloadCacheSize;
            p->decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
            stampPacket(p);
            LOG_INFO("Beacon: MESH_BEACON_APP offer+msg from=0x%08x msg='%.40s' target=%d", p->from, bcfg.broadcast_message, ti);
            applyTarget(p);
        }
    }
}

int32_t MeshBeaconBroadcastModule::runOnce()
{
    const auto &bcfg = moduleConfig.mesh_beacon;
    const uint32_t intervalSecs =
        Default::getConfiguredOrDefault(bcfg.broadcast_interval_secs, default_mesh_beacon_min_broadcast_interval_secs);
    const uint32_t intervalMs =
        Default::getConfiguredOrMinimumValue(intervalSecs, default_mesh_beacon_min_broadcast_interval_secs) * 1000;

    if ((bcfg.flags & MESH_BEACON_FLAG_BROADCAST_ENABLED) && airTime->isTxAllowedAirUtil() &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        // Throttle against the reboot-safe transmit history (mirrors NodeInfoModule): skip if we
        // broadcast within the interval, even across a reboot. 0 = never sent → send now.
        uint32_t lastSent = transmitHistory ? transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_MESH_BEACON_APP) : 0;
        if (lastSent == 0 || !Throttle::isWithinTimespanMs(lastSent, intervalMs)) {
            // Record the send BEFORE transmitting: the LoRa TX is a high-current event that can
            // brown out a marginal supply, and if that reboots us mid-transmit we still want the
            // "sent" marker persisted so we don't re-broadcast immediately on every boot.
            if (transmitHistory)
                transmitHistory->setLastSentToMesh(meshtastic_PortNum_MESH_BEACON_APP);
            sendBeacon();
        }
    }

    return static_cast<int32_t>(intervalMs);
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
    return moduleConfig.has_mesh_beacon && (moduleConfig.mesh_beacon.flags & MESH_BEACON_FLAG_LISTEN_ENABLED) &&
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

    // NOTE: we deliberately do NOT unwrap the text into a synthesized TEXT_MESSAGE_APP for the
    // phone. The original MESH_BEACON_APP packet already flows to the client (we return CONTINUE),
    // so a beacon-aware client renders `message` directly - injecting a copy would only duplicate
    // it. Broadcasters that need non-beacon-aware clients to see the text use FLAG_LEGACY_SPLIT,
    // which sends a real TEXT_MESSAGE_APP over RF. We also do not fire EVENT_RECEIVED_MSG: a beacon
    // is an advisory broadcast, not a personal message, and must not wake the device from sleep.
    if (hasText)
        LOG_INFO("Beacon: received from 0x%08x: '%.40s'", mp.from, b->message);

    // Cache any offer for the client app - never auto-applied.
    if (hasOfferContent) {
        lastReceivedOffer.valid = true;
        lastReceivedOffer.sender = mp.from;
        lastReceivedOffer.has_channel = b->has_offer_channel;
        if (b->has_offer_channel)
            lastReceivedOffer.channel = b->offer_channel;
        lastReceivedOffer.region = b->offer_region;
        lastReceivedOffer.preset = b->offer_preset;
        lastReceivedOffer.received_at =
            getValidTime(RTCQualityFromNet); // 0 if no RTC fix yet - consumers must not treat 0 as valid
        LOG_INFO("Beacon: stored offer from 0x%08x (preset=%d)", mp.from, b->offer_preset);
    }

    notifyObservers(&mp);
    return false;
}
