#include "MeshBeaconModule.h"
#include "Default.h"
#include "DisplayFormatters.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Router.h"
#include "TransmitHistory.h"
#include "UptimeClock.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "main.h"
#include <Throttle.h>
#include <string.h>

// Static members
meshtastic_Config_LoRaConfig_ModemPreset MeshBeaconModule::originalModemPreset;
uint16_t MeshBeaconModule::originalLoraChannel;
meshtastic_Config_LoRaConfig_RegionCode MeshBeaconModule::originalRegion;

static MeshBeaconModule_TargetRadioSettings targetRadioSettings[8];

// Ids whose entry was evicted while the packet may still be queued. Without this such a packet would
// reach the radio as ordinary traffic and key up on the home config with the target channel's key.
static PacketId evictedIds[8];
static uint8_t evictedNext;

// Explicit switch state, not inferred: "live config differs from the snapshot" missed name/PSK-only
// swaps and fired on legitimate channel edits.
static bool radioSwitched = false;
static uint32_t switchedForId = 0;

// The interval runOnce() schedules on: configured, else the default, never below the minimum.
static uint32_t beaconIntervalMs()
{
    const uint32_t secs = Default::getConfiguredOrDefault(moduleConfig.mesh_beacon.broadcast_interval_secs,
                                                          default_mesh_beacon_min_broadcast_interval_secs);
    return Default::getConfiguredOrMinimumValue(secs, default_mesh_beacon_min_broadcast_interval_secs) * 1000;
}

// Queued a whole broadcast interval: the next beacon is due, and this one describes a mesh that may have moved on.
static bool targetRadioSettingsStale(const MeshBeaconModule_TargetRadioSettings &entry)
{
    return entry.inUse && Throttle::hasElapsed(entry.armedAtMs, beaconIntervalMs());
}

static void rememberEvicted(PacketId id)
{
    evictedIds[evictedNext] = id;
    evictedNext = (uint8_t)((evictedNext + 1) % (sizeof(evictedIds) / sizeof(evictedIds[0])));
}

static bool wasEvicted(PacketId id)
{
    for (const PacketId e : evictedIds)
        if (id && e == id)
            return true;
    return false;
}

static void forgetEvicted(PacketId id)
{
    for (PacketId &e : evictedIds)
        if (id && e == id)
            e = 0;
}

const MeshBeaconModule_TargetRadioSettings *MeshBeaconModule::getTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    if (!p)
        return nullptr;
    for (const auto &entry : targetRadioSettings)
        if (entry.inUse && entry.id == p->id)
            return &entry;
    return nullptr;
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
}

bool MeshBeaconModule::setTargetRadioSettings(const meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset preset,
                                              uint16_t slot, bool legacyHopOverride,
                                              meshtastic_Config_LoRaConfig_RegionCode region, int16_t channelHash)
{
    if (!p)
        return false;
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
        // Table full. A stale entry goes first. Never evict the entry the outstanding switch is gated on:
        // dropping it would unblock the restore and put the home config back under a beacon that has not keyed up.
        for (auto &entry : targetRadioSettings) {
            if (targetRadioSettingsStale(entry) && (!radioSwitched || entry.id != switchedForId)) {
                target = &entry;
                break;
            }
        }
        for (auto &entry : targetRadioSettings) {
            if (!target && (!radioSwitched || entry.id != switchedForId))
                target = &entry;
        }
        if (!target) {
            LOG_WARN("Beacon: target table full and every slot is in flight, drop target for 0x%08x", p->id);
            return false;
        }
        LOG_WARN("Beacon: target table full (%u slots), evicting packet 0x%08x for 0x%08x",
                 (unsigned)(sizeof(targetRadioSettings) / sizeof(targetRadioSettings[0])), target->id, p->id);
        rememberEvicted(target->id);
    }
    target->inUse = true;
    target->id = p->id;
    target->preset = preset;
    target->slot = slot;
    target->legacyHopOverride = legacyHopOverride;
    target->region = region;
    target->channelHash = channelHash;
    target->armedAtMs = Time::getMillis(); // the clock Throttle::hasElapsed() reads it against
    return true;
}

bool MeshBeaconModule::hasTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    return getTargetRadioSettings(p) != nullptr;
}

static void clearTargetRadioSettingsById(PacketId id)
{
    forgetEvicted(id);
    for (auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == id) {
            entry.inUse = false;
            return;
        }
    }
}

void MeshBeaconModule::clearTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    if (p)
        clearTargetRadioSettingsById(p->id);
}

bool MeshBeaconModule::beaconTxConfigInvalid(const meshtastic_MeshPacket *p)
{
    const MeshBeaconModule_TargetRadioSettings *s = getTargetRadioSettings(p);
    if (!s) {
        if (p && wasEvicted(p->id)) {
            LOG_WARN("Beacon: packet 0x%08x lost its target entry while queued, drop", p->id);
            return true;
        }
        return false; // not a beacon-switch packet - nothing to validate, normal traffic unaffected
    }

    if (targetRadioSettingsStale(*s)) {
        LOG_WARN("Beacon: packet 0x%08x queued past its broadcast interval, drop", p->id);
        return true;
    }

    const meshtastic_Config_LoRaConfig_RegionCode region =
        (s->region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? s->region : config.lora.region;

    // An unlicensed node must never key up on a ham-only (licensed-only) region. The reverse is
    // allowed: a licensed (ham) node may operate in a non-ham region - and the switch only touches
    // preset/region/slot, never owner.is_licensed, so it cannot deactivate licensed mode.
    const RegionInfo *r = getRegion(region);
    if (r && r->profile->licensedOnly && !owner.is_licensed)
        return true;

    // Preset must be valid for the target region.
    meshtastic_Config_LoRaConfig probe = config.lora;
    probe.use_preset = true;
    probe.modem_preset = s->preset;
    probe.region = region;
    if (!RadioInterface::validateConfigLora(probe))
        return true;
    // And the slot must exist at the bandwidth the switch will actually run, which on custom modem params is the node's own.
    // 0 is "derive", which always lands in range.
    probe.use_preset = config.lora.use_preset;
    return s->slot > RadioInterface::frequencySlotCount(probe);
}

void MeshBeaconModule::beaconChannelName(const meshtastic_ChannelSettings &ch, meshtastic_Config_LoRaConfig_ModemPreset preset,
                                         char (&out)[sizeof(meshtastic_ChannelSettings::name)])
{
    // A blank name is the default channel, which on the target preset is that preset's display name -
    // the name every node there hashes its slot and channel hash from.
    const char *name = ch.name[0] ? ch.name : DisplayFormatters::getModemPresetDisplayName(preset, false, true);
    strncpy(out, name, sizeof(out) - 1);
    out[sizeof(out) - 1] = '\0';
}

uint32_t MeshBeaconModule::offerFrequencySlot(const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg, uint32_t *derivedOut)
{
    meshtastic_Config_LoRaConfig probe = config.lora;
    probe.use_preset = true;
    if (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        probe.region = bcfg.broadcast_offer_region;
    // An unset preset is the offered region's default: what a receiver derives with, not what this node runs.
    probe.modem_preset =
        bcfg.has_broadcast_offer_preset ? bcfg.broadcast_offer_preset : getRegion(probe.region)->getDefaultPreset();

    // No offered channel leaves this empty, which resolveFrequencySlot() hashes as the preset name.
    char name[sizeof(meshtastic_ChannelSettings::name)] = "";
    if (bcfg.has_broadcast_offer_channel)
        beaconChannelName(bcfg.broadcast_offer_channel, probe.modem_preset, name);

    probe.channel_num = 0;
    const uint32_t derived = RadioInterface::resolveFrequencySlot(probe, name);
    if (derivedOut)
        *derivedOut = derived;
    if (!bcfg.has_broadcast_offer_frequency_slot || bcfg.broadcast_offer_frequency_slot == 0)
        return derived;
    probe.channel_num = bcfg.broadcast_offer_frequency_slot;
    return RadioInterface::resolveFrequencySlot(probe, name);
}

bool MeshBeaconModule::offerSlotUsable(const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg)
{
    if (!bcfg.has_broadcast_offer_frequency_slot || bcfg.broadcast_offer_frequency_slot == 0)
        return true;
    meshtastic_Config_LoRaConfig probe = config.lora;
    probe.use_preset = true;
    if (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        probe.region = bcfg.broadcast_offer_region;
    probe.modem_preset =
        bcfg.has_broadcast_offer_preset ? bcfg.broadcast_offer_preset : getRegion(probe.region)->getDefaultPreset();
    return bcfg.broadcast_offer_frequency_slot <= RadioInterface::frequencySlotCount(probe);
}

void MeshBeaconModule::fillOffer(meshtastic_MeshBeacon &beacon, const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg)
{
    // Withheld whole, not advertised on a substitute slot: a receiver that joins the derived slot would
    // land on a different mesh from the one the operator described.
    if (!offerSlotUsable(bcfg)) {
        LOG_WARN("Beacon: offer slot %u not in the offered region, offer withheld", bcfg.broadcast_offer_frequency_slot);
        return;
    }
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

    // Spend bytes on a slot only where a receiver could not work it out from what the offer already says.
    uint32_t derived = 0;
    const uint32_t advertised = offerFrequencySlot(bcfg, &derived);
    if (advertised != derived) {
        beacon.has_offer_frequency_slot = true;
        beacon.offer_frequency_slot = advertised;
    }
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

    const MeshBeaconModule_TargetRadioSettings *s = getTargetRadioSettings(p);
    if (s) {
        const meshtastic_Config_LoRaConfig_ModemPreset targetPreset = s->preset;
        const uint16_t targetSlot = s->slot;

        // Legacy compatibility: older firmware (pre-v2.7.20) drops hop_start==0 packets via the
        // pre-hop check before decryption, so they can't see has_bitfield to validate them.
        // Setting hop_start=1 (with hop_limit remaining 0) makes the packet pass the old check
        // while still being zero-hop (hop_limit=0 prevents any rebroadcast).
        if (s->legacyHopOverride)
            p->hop_start = 1;

        const meshtastic_Config_LoRaConfig_RegionCode targetRegion =
            (s->region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? s->region : config.lora.region;

        // Only RF settings switch; the channel travels on the packet. Compare against the slot the radio is
        // actually on, since channel_num may still be 0 ("derive"), which never equals a concrete slot.
        const uint16_t liveSlot =
            (uint16_t)RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));
        if (targetPreset == config.lora.modem_preset && (targetSlot == 0 || targetSlot == liveSlot) &&
            targetRegion == config.lora.region)
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
            switchDepth = 0;
        }
        switchDepth++;

        LOG_INFO("Beacon: switch #%u radio for packet 0x%08x to preset=%d slot=%u region=%d", switchDepth, p->id, targetPreset,
                 targetSlot, targetRegion);
        if (switchDepth > 1)
            LOG_WARN("Beacon: switching again with no restore between; home preset=%d slot=%u region=%d still held",
                     originalModemPreset, originalLoraChannel, originalRegion);
        // Before config.lora stops describing the config we are committed to, so the committed slot
        // stays pinned to ours while we key up on someone else's preset.
        if (nodeDB)
            nodeDB->setLoraSlotTransient(true);
        config.lora.modem_preset = targetPreset;
        config.lora.channel_num = targetSlot;
        if (targetRegion != config.lora.region)
            config.lora.region = targetRegion;

        radioSwitched = true; // set before reconfigure(), so the flag never lags the radio it describes
        switchedForId = p->id;
        iface->reconfigure();
        return true;

    } else if (radioSwitched) { // s is null here: either no packet, or one carrying no target

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

        if (nodeDB) { // config.lora describes the committed config again
            nodeDB->setLoraSlotTransient(false);
            nodeDB->refreshCommittedLoraSlot();
        }
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
    // perhapsEncode() hashed a blank-named channel under the running preset's name; the header byte is
    // written after this hook, so put the target preset's hash there.
    const MeshBeaconModule_TargetRadioSettings *s = MeshBeaconModule::getTargetRadioSettings(p);
    if (s && s->channelHash >= 0)
        p->channel = (uint8_t)s->channelHash;
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
    fillOffer(beacon, bcfg);
    // Note: an empty config legitimately encodes to 0 bytes, and pb_encode_to_bytes can't distinguish
    // that from a (here effectively impossible - buffer is max-sized) failure, so we always clear the
    // dirty flag. The combined send is gated on payloadCacheSize > 0, so an empty payload is never TX'd.
    payloadCacheSize = (pb_size_t)pb_encode_to_bytes(payloadCache, sizeof(payloadCache), &meshtastic_MeshBeacon_msg, &beacon);
    payloadCacheDirty = false;
    LOG_DEBUG("Beacon: payload cache rebuilt (%u bytes)", payloadCacheSize);
}

void MeshBeaconBroadcastModule::sendBeaconPacket(meshtastic_MeshPacket *p)
{
    // Beacons uplink to MQTT on their channel's uplink_enabled, like any other packet on that channel.
    const PacketId id = p->id; // every send() failure path but ERRNO_SHOULD_RELEASE has freed p by the time it returns
    const ErrorCode sent = router->send(p);
    if (sent == ERRNO_OK)
        return;
    // Not queued, so the interface never owns the entry and no TX hook will ever release it
    clearTargetRadioSettingsById(id);
    if (sent == ERRNO_SHOULD_RELEASE)
        packetPool.release(p);
}

void MeshBeaconBroadcastModule::sendBeacon()
{
    const auto &bcfg = moduleConfig.mesh_beacon;

    const bool hasText = bcfg.broadcast_message[0] != '\0';
    // A pinned slot is offer content only when fillOffer() puts it on the air, i.e. it differs from the derived one.
    // An offer withheld for an unusable pin is no content at all: the text still goes out, the invitation does not.
    uint32_t derivedOfferSlot = 0;
    const bool offerSlotOnAir =
        bcfg.has_broadcast_offer_frequency_slot && offerFrequencySlot(bcfg, &derivedOfferSlot) != derivedOfferSlot;
    const bool hasRadioContent = (bcfg.has_broadcast_offer_preset || bcfg.has_broadcast_offer_channel || offerSlotOnAir ||
                                  (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET)) &&
                                 offerSlotUsable(bcfg);

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
        fillOffer(offerOnly, bcfg);
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
        uint16_t slot; // resolved, never 0
        meshtastic_Config_LoRaConfig_RegionCode region;
        ChannelIndex channelIndex; // table slot to encrypt on; the primary when none is named
        // The name the channel goes out under on the target preset, for its slot hash and its wire hash
        char channelName[sizeof(meshtastic_ChannelSettings::name)];
    };

    // The slot the node is already on, resolved the same way a target's is, so the two compare.
    const uint16_t homeSlot =
        (uint16_t)RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));

    // An empty list still beacons once, on the node's running preset and region over the primary
    // channel. Each entry below overrides only what it sets.
    const int targetCount = bcfg.broadcast_targets_count > 0 ? (int)bcfg.broadcast_targets_count : 1;

    // Dedup state: the beacon payload is identical across targets, so two targets that resolve to
    // the same effective radio config (preset + resolved region + slot + channel) would just re-broadcast
    // the same packet - wasted airtime and a redundant radio switch each. We skip the later one.
    // Keyed on the *resolved* values so an explicit "current region" dedups against an UNSET one.
    EffTarget sent[4];
    meshtastic_Config_LoRaConfig_RegionCode sentRegion[4];
    int sentCount = 0;
    const auto sameEffectiveTarget = [](const EffTarget &a, meshtastic_Config_LoRaConfig_RegionCode ar, const EffTarget &b,
                                        meshtastic_Config_LoRaConfig_RegionCode br) {
        return a.preset == b.preset && ar == br && a.slot == b.slot && a.channelIndex == b.channelIndex;
    };

    for (int ti = 0; ti < targetCount; ti++) {
        // Defaults: running radio config, primary channel. A target entry overrides from here.
        EffTarget tgt = {};
        tgt.preset = config.lora.modem_preset;
        tgt.channelIndex = channels.getPrimaryIndex();
        const auto *bt = ti < (int)bcfg.broadcast_targets_count ? &bcfg.broadcast_targets[ti] : nullptr;
        if (bt) {
            if (bt->has_preset)
                tgt.preset = bt->preset;
            tgt.region = bt->region;
            // Resolve the channel from the device's channel table by index. An out-of-range index, or a
            // disabled or blank slot (no name, no PSK), falls back to the default channel for the target
            // preset, exactly as an unset channel_index does.
            if (bt->has_channel_index) {
                if (bt->channel_index >= (uint32_t)channels.getNumChannels()) {
                    LOG_WARN("Beacon: target %d channel_index %u out of range, use preset default", ti, bt->channel_index);
                } else {
                    const meshtastic_Channel &ch = channels.getByIndex((ChannelIndex)bt->channel_index);
                    if (ch.has_settings && ch.role != meshtastic_Channel_Role_DISABLED &&
                        (ch.settings.name[0] != '\0' || ch.settings.psk.size > 0))
                        tgt.channelIndex = (ChannelIndex)bt->channel_index;
                    else
                        LOG_DEBUG("Beacon: target %d channel_index %u unusable, use preset default", ti, bt->channel_index);
                }
            }
        }
        beaconChannelName(channels.getByIndex(tgt.channelIndex).settings, tgt.preset, tgt.channelName);

        const meshtastic_Config_LoRaConfig_RegionCode resolvedRegion =
            (tgt.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? tgt.region : config.lora.region;

        // The slot, per the proto: a pin wins; unset derives it the way a node running this channel would,
        // from the target region's override slot or the channel name's hash. A target on the home radio keeps
        // the home slot, which is what secondary channels share.
        meshtastic_Config_LoRaConfig probe = config.lora;
        probe.modem_preset = tgt.preset;
        probe.region = resolvedRegion;
        const bool pinned = bt && bt->has_frequency_slot && bt->frequency_slot > 0;
        const bool homeRadio = resolvedRegion == config.lora.region && tgt.preset == config.lora.modem_preset;
        if (pinned && bt->frequency_slot > RadioInterface::frequencySlotCount(probe)) {
            // Skipped, not derived: the operator named a frequency, and beaconing on another one is worse than not at all.
            LOG_WARN("Beacon: target %d frequency_slot %u not in region %d, skip", ti, bt->frequency_slot, resolvedRegion);
            continue;
        }
        probe.channel_num = pinned ? bt->frequency_slot : homeRadio ? homeSlot : 0;
        tgt.slot = (uint16_t)RadioInterface::resolveFrequencySlot(probe, tgt.channelName);

        // Skip a target whose effective radio config duplicates one already sent this cycle.
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

        // Only RF settings can need a switch; the channel is named on the packet instead.
        const bool radioDiffers =
            tgt.preset != config.lora.modem_preset || resolvedRegion != config.lora.region || tgt.slot != homeSlot;
        // On another preset a blank-named channel hashes under that preset's name, not the one perhapsEncode() uses
        const int16_t wireHash = radioDiffers ? channels.hashFor(tgt.channelIndex, tgt.channelName) : -1;

        const auto applyTarget = [&](meshtastic_MeshPacket *p) {
            // perhapsEncode() keys encryption and the channel hash off this index, so the primary slot is never touched
            p->channel = tgt.channelIndex;
            if (radioDiffers || legacySplit) {
                const bool armed = setTargetRadioSettings(p, tgt.preset, tgt.slot, legacySplit, resolvedRegion, wireHash);
                // No entry means no switch and no TX gate: it would key up on the home radio with this target's key
                if (!armed && radioDiffers) {
                    packetPool.release(p);
                    return;
                }
            }
            sendBeaconPacket(p);
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
    const uint32_t intervalMs = beaconIntervalMs();

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
    const bool hasOfferContent = b && (b->has_offer_channel || b->offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET ||
                                       b->has_offer_preset || b->has_offer_frequency_slot);
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
        lastReceivedOffer.has_frequency_slot = b->has_offer_frequency_slot;
        lastReceivedOffer.frequency_slot = b->has_offer_frequency_slot ? b->offer_frequency_slot : 0;
        lastReceivedOffer.received_at =
            getValidTime(RTCQualityFromNet); // 0 if no RTC fix yet - consumers must not treat 0 as valid
        LOG_INFO("Beacon: stored offer from 0x%08x (preset=%d)", mp.from, b->offer_preset);
    }

    notifyObservers(&mp);
    return false;
}
