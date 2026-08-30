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
bool MeshBeaconModule::originalUsePreset;

// One entry per broadcast target - the proto holds 4 - each covering the legacy split pair.
static MeshBeaconModule_TargetRadioSettings targetRadioSettings[4];

// Role_DISABLED is the zero value, so an unprovisioned slot reads as disabled. Neither a blank name
// nor an empty PSK says anything: the name falls back to the preset's, and Channels::getKey() reads
// an empty PSK as either the primary's key or deliberate cleartext. Same test the firmware uses.
static bool channelSlotUsable(const meshtastic_Channel &slot)
{
    return slot.has_settings && slot.role != meshtastic_Channel_Role_DISABLED;
}

// Explicit switch state, not inferred: "live config differs from the snapshot" missed name/PSK-only
// swaps and fired on legitimate channel edits.
static bool radioSwitched = false;
static uint32_t switchedForId = 0;

// A beacon still queued a broadcast interval after it was armed describes a mesh that has moved on,
// and the next cycle is due. Expire it so it cannot transmit, and so it frees its table slot.
static bool targetRadioSettingsStale(const MeshBeaconModule_TargetRadioSettings &entry)
{
    return entry.idCount && Throttle::hasElapsed(entry.armedAtMs, default_mesh_beacon_min_broadcast_interval_secs * 1000UL);
}

// The config this entry transmits on, right now. An inherited region is resolved here rather than
// at send time, so a region changed while the beacon was queued cannot key it up on the old one.
static meshtastic_Config_LoRaConfig effectiveLora(const MeshBeaconModule_TargetRadioSettings &entry)
{
    meshtastic_Config_LoRaConfig lora = entry.lora;
    if (entry.regionInherited)
        lora.region = config.lora.region;
    return lora;
}

static bool entryHoldsId(const MeshBeaconModule_TargetRadioSettings &entry, PacketId id)
{
    for (uint8_t i = 0; i < entry.idCount; i++)
        if (entry.ids[i] == id)
            return true;
    return false;
}

const MeshBeaconModule_TargetRadioSettings *MeshBeaconModule::getTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    if (!p)
        return nullptr;
    for (const auto &entry : targetRadioSettings)
        if (entry.idCount && entryHoldsId(entry, p->id))
            return &entry;
    return nullptr;
}

// Has that beacon finished? Answered from the table, without asking the radio.
static bool targetRadioSettingsLive(uint32_t id)
{
    for (const auto &entry : targetRadioSettings)
        if (entry.idCount && entryHoldsId(entry, id))
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// MeshBeaconModule base
// ---------------------------------------------------------------------------

MeshBeaconModule::MeshBeaconModule()
{
    originalModemPreset = config.lora.modem_preset;
    originalUsePreset = config.lora.use_preset;
    originalLoraChannel = config.lora.channel_num;
    originalRegion = config.lora.region;
}

int MeshBeaconModule::setTargetRadioSettings(const meshtastic_MeshPacket *p, const MeshBeaconModule_TargetRadioSettings &s,
                                             int shareWith)
{
    constexpr int kEntries = (int)(sizeof(targetRadioSettings) / sizeof(targetRadioSettings[0]));
    if (!p)
        return -1;
    clearTargetRadioSettingsById(p->id); // a re-arm must not leave this id on its old entry

    // Reap anything a previous cycle left behind before looking for room, so a beacon that never
    // transmitted cannot hold a slot against the cycle that follows it.
    for (auto &entry : targetRadioSettings) {
        if (targetRadioSettingsStale(entry)) {
            LOG_WARN("Beacon: target entry for 0x%08x expired unsent, freeing its slot", entry.ids[0]);
            entry.idCount = 0;
        }
    }

    // The caller names the split pair rather than us inferring it: guessing loose would share an
    // entry between packets that need different radios.
    if (shareWith >= 0 && shareWith < kEntries) {
        auto &entry = targetRadioSettings[shareWith];
        if (entry.idCount && entry.idCount < (uint8_t)(sizeof(entry.ids) / sizeof(entry.ids[0]))) {
            entry.ids[entry.idCount++] = p->id;
            return shareWith;
        }
    }

    MeshBeaconModule_TargetRadioSettings *target = nullptr;
    for (auto &entry : targetRadioSettings) {
        if (!entry.idCount) {
            target = &entry;
            break;
        }
    }
    if (!target) {
        // Table full. Never evict the entry the outstanding switch is gated on: dropping it would
        // unblock the restore and put the home config back under a beacon that has not keyed up.
        for (auto &entry : targetRadioSettings) {
            if (!radioSwitched || !entryHoldsId(entry, switchedForId)) {
                target = &entry;
                break;
            }
        }
        if (!target) {
            LOG_WARN("Beacon: target table full and every slot is in flight, drop target for 0x%08x", p->id);
            return -1;
        }
        LOG_WARN("Beacon: target table full (%u slots), evicting packet 0x%08x for 0x%08x", (unsigned)kEntries, target->ids[0],
                 p->id);
    }
    *target = s;
    target->idCount = 1;
    target->ids[0] = p->id;
    // Armed on allocation, not on attach: the split pair expires together, timed from the first.
    target->armedAtMs = millis();
    target->channelName[sizeof(target->channelName) - 1] = '\0'; // s may carry an unterminated name
    return (int)(target - targetRadioSettings);
}

bool MeshBeaconModule::hasTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    return getTargetRadioSettings(p) != nullptr;
}

// Only ERRNO_OK means the interface queued the packet and now owns its sidecar entry too. Every
// other return has already freed p - bar ERRNO_SHOULD_RELEASE - so clear by id, never through p.
static void releaseIfNotQueued(ErrorCode sendResult, meshtastic_MeshPacket *p, PacketId id)
{
    if (sendResult == ERRNO_OK)
        return;
    MeshBeaconModule::clearTargetRadioSettingsById(id);
    if (sendResult == ERRNO_SHOULD_RELEASE)
        packetPool.release(p); // this one is still ours to free
}

void MeshBeaconModule::clearTargetRadioSettingsById(PacketId id)
{
    for (auto &entry : targetRadioSettings) {
        for (uint8_t i = 0; i < entry.idCount; i++) {
            if (entry.ids[i] != id)
                continue;
            // The other half of a legacy split may still be queued on these settings, so the entry
            // is only freed once its last packet has gone.
            entry.ids[i] = entry.ids[entry.idCount - 1];
            entry.idCount--;
            return;
        }
    }
}

void MeshBeaconModule::clearTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    if (p)
        clearTargetRadioSettingsById(p->id);
}

void MeshBeaconModule::clearAllTargetRadioSettings()
{
    for (auto &entry : targetRadioSettings)
        entry.idCount = 0;
}

bool MeshBeaconModule::beaconTxConfigInvalid(const meshtastic_MeshPacket *p)
{
    const MeshBeaconModule_TargetRadioSettings *s = getTargetRadioSettings(p);
    if (!s)
        return false; // not a beacon-switch packet - nothing to validate, normal traffic unaffected

    // Queued for a whole broadcast interval: the mesh it advertises has moved on and the next
    // beacon is due, so drop it rather than transmit an hour-old description.
    if (targetRadioSettingsStale(*s)) {
        LOG_WARN("Beacon: packet 0x%08x queued past its broadcast interval, drop", p->id);
        return true;
    }

    const meshtastic_Config_LoRaConfig lora = effectiveLora(*s);

    // An unlicensed node must never key up on a ham-only (licensed-only) region. The reverse is
    // allowed: a licensed (ham) node may operate in a non-ham region - and the switch only touches
    // preset/region/channel, never owner.is_licensed, so it cannot deactivate licensed mode.
    const RegionInfo *r = getRegion(lora.region);
    if (r && r->profile->licensedOnly && !owner.is_licensed)
        return true;

    // Validate the config the switch will actually install, against the channel name this target
    // resolved at send time - the slot is picked by hashing it.
    return !RadioInterface::validateConfigLora(lora, s->channelName);
}

// Reject only what can never become valid, leaving the rest as the operator wrote it; sendBeacon()
// resolves the rest. Also runs at boot, since a userPrefs config never passes through admin.
void MeshBeaconModule::sanitiseConfig(meshtastic_ModuleConfig_MeshBeaconConfig &bcfg)
{
    // Hard cap at 100 chars.
    bcfg.broadcast_message[100] = '\0';
    // Enforce interval minimum (0 means unset/use default).
    if (bcfg.broadcast_interval_secs != 0 && bcfg.broadcast_interval_secs < default_mesh_beacon_min_broadcast_interval_secs)
        bcfg.broadcast_interval_secs = default_mesh_beacon_min_broadcast_interval_secs;
    // Same order and rules as a target below: region first so a bad one cannot reject a good
    // preset, then channel index, preset, and the pinned slot. UNSET region = use running.
    if (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        const RegionInfo *r = getRegion(bcfg.broadcast_offer_region);
        if (r->code != bcfg.broadcast_offer_region) {
            LOG_WARN("Beacon: broadcast_offer_region %d invalid, clearing", bcfg.broadcast_offer_region);
            bcfg.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        }
    }
    // Range only, as for a target: an unprovisioned slot must not be rejected here, and a
    // slot disabled later is handled by recheckBeaconAfterChannelEdit().
    if (bcfg.has_broadcast_offer_channel_index && bcfg.broadcast_offer_channel_index >= MAX_NUM_CHANNELS) {
        LOG_WARN("Beacon: broadcast_offer_channel_index %u out of range, clearing", bcfg.broadcast_offer_channel_index);
        bcfg.has_broadcast_offer_channel_index = false;
    }
    // Only a value that is no preset at all: one this region cannot run may be right after a move.
    if (bcfg.has_broadcast_offer_preset && !isKnownModemPreset(bcfg.broadcast_offer_preset)) {
        LOG_WARN("Beacon: broadcast_offer_preset %d is not a preset any region offers, clearing", bcfg.broadcast_offer_preset);
        bcfg.has_broadcast_offer_preset = false;
    }
    // Bounds-checked only with both a region and a preset: that pair fixes the bandwidth, so the
    // slot count cannot move under the pin later. Otherwise only 0, which the proto reserves as
    // unset - checking against the running region would delete a pin on the next region change.
    if (bcfg.has_broadcast_offer_frequency_slot) {
        if (bcfg.broadcast_offer_frequency_slot == 0) {
            LOG_WARN("Beacon: broadcast_offer_frequency_slot 0 means unset, clearing");
            bcfg.has_broadcast_offer_frequency_slot = false;
        } else if (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET &&
                   bcfg.has_broadcast_offer_preset) {
            meshtastic_Config_LoRaConfig probe = config.lora;
            probe.use_preset = true;
            probe.modem_preset = bcfg.broadcast_offer_preset;
            probe.region = bcfg.broadcast_offer_region;
            const uint32_t slots = RadioInterface::frequencySlotCount(probe);
            if (bcfg.broadcast_offer_frequency_slot > slots) {
                LOG_WARN("Beacon: broadcast_offer_frequency_slot %u outside 1..%u, clearing", bcfg.broadcast_offer_frequency_slot,
                         slots);
                bcfg.has_broadcast_offer_frequency_slot = false;
            }
        }
    }
    // Validate each broadcast target so a bad preset/region is cleared on write rather than
    // relying on the runtime TX drop.
    for (pb_size_t i = 0; i < bcfg.broadcast_targets_count; i++) {
        auto &t = bcfg.broadcast_targets[i];
        // Region must be a known region code (UNSET = use running config at TX time).
        if (t.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
            const RegionInfo *r = getRegion(t.region);
            if (r->code != t.region) {
                LOG_WARN("Beacon: broadcast_targets[%u] region %d invalid, clearing", i, t.region);
                t.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
            }
        }
        // Before the preset check, so the name hashed below is this target's. Range only:
        // Role_DISABLED is the zero value, so an unprovisioned slot would read as disabled.
        if (t.has_channel_index && t.channel_index >= MAX_NUM_CHANNELS) {
            LOG_WARN("Beacon: broadcast_targets[%u] channel_index %u out of range, clearing", i, t.channel_index);
            t.has_channel_index = false;
        }
        // As for the offer: only a value that is no preset at all, never one this region cannot run.
        if (t.has_preset && !isKnownModemPreset(t.preset)) {
            LOG_WARN("Beacon: broadcast_targets[%u] preset %d is not a preset any region offers, clearing", i, t.preset);
            t.has_preset = false;
        }
        // Last, so it is checked against the preset and region the clamp above settled on - and
        // only when both are explicit, exactly as for the offer above.
        if (t.has_frequency_slot) {
            if (t.frequency_slot == 0) {
                LOG_WARN("Beacon: broadcast_targets[%u] frequency_slot 0 means unset, clearing", i);
                t.has_frequency_slot = false;
            } else if (t.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET && t.has_preset) {
                meshtastic_Config_LoRaConfig probe = config.lora;
                probe.use_preset = true;
                probe.modem_preset = t.preset;
                probe.region = t.region;
                const uint32_t slots = RadioInterface::frequencySlotCount(probe);
                if (t.frequency_slot > slots) {
                    LOG_WARN("Beacon: broadcast_targets[%u] frequency_slot %u outside 1..%u, clearing", i, t.frequency_slot,
                             slots);
                    t.has_frequency_slot = false;
                }
            }
        }
    }
}

void MeshBeaconModule::fillOffer(meshtastic_MeshBeacon &beacon, const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg)
{
    if (const meshtastic_ChannelSettings *offerCh = offerChannelSettings(bcfg)) {
        beacon.has_offer_channel = true;
        beacon.offer_channel = *offerCh;
        // PSK is included intentionally: this beacon is a public join-invitation. The offered
        // channel is not secret - the PSK here is a convenience token, not a security boundary.
    }
    beacon.has_offer_preset = bcfg.has_broadcast_offer_preset;
    beacon.offer_preset = bcfg.broadcast_offer_preset;
    beacon.offer_region = bcfg.broadcast_offer_region;

    // Spend a slot on the air only where a receiver could not work it out from the region, preset
    // and channel name already in the offer - which covers a region that mandates a slot.
    uint32_t derived = 0;
    const uint32_t advertised = offerFrequencySlot(bcfg, &derived);
    if (advertised != derived) {
        beacon.has_offer_frequency_slot = true;
        beacon.offer_frequency_slot = advertised;
    }
}

uint32_t MeshBeaconModule::offerFrequencySlot(const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg, uint32_t *derivedOut)
{
    const meshtastic_ChannelSettings *offerCh = offerChannelSettings(bcfg);
    const auto preset = bcfg.has_broadcast_offer_preset ? bcfg.broadcast_offer_preset : config.lora.modem_preset;

    meshtastic_Config_LoRaConfig probe = config.lora;
    probe.use_preset = true;
    probe.modem_preset = preset;
    if (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        probe.region = bcfg.broadcast_offer_region;

    // An offer with no channel leaves this empty, which resolveFrequencySlot() reads as "hash the
    // preset name" - what a receiver derives when the offer names no channel to hash.
    char name[sizeof(meshtastic_ChannelSettings::name)] = "";
    if (offerCh)
        strncpy(name, beaconChannelSettings(*offerCh, preset).name, sizeof(name) - 1);

    probe.channel_num = 0;
    if (derivedOut)
        *derivedOut = RadioInterface::resolveFrequencySlot(probe, name);
    // Resolved, not verbatim: a pin outside the region falls back to the derived slot rather than
    // advertising one no receiver can tune.
    probe.channel_num = bcfg.has_broadcast_offer_frequency_slot ? bcfg.broadcast_offer_frequency_slot : 0;
    return RadioInterface::resolveFrequencySlot(probe, name);
}

const meshtastic_ChannelSettings *MeshBeaconModule::offerChannelSettings(const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg)
{
    if (!bcfg.has_broadcast_offer_channel_index || bcfg.broadcast_offer_channel_index >= (uint32_t)channels.getNumChannels())
        return nullptr;
    // Unlike a target, an unusable slot advertises nothing rather than falling back to the primary:
    // the offer describes a mesh to join, and a retired slot would hand out a deleted PSK.
    const meshtastic_Channel &slot = channels.getByIndex((ChannelIndex)bcfg.broadcast_offer_channel_index);
    return channelSlotUsable(slot) ? &slot.settings : nullptr;
}

meshtastic_ChannelSettings MeshBeaconModule::beaconChannelSettings(const meshtastic_ChannelSettings &base,
                                                                   meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    meshtastic_ChannelSettings ch = base;
    // A blank name defaults to the preset's display name, so the beacon channel is identifiable
    // rather than borrowing whatever the running node happens to call its primary.
    if (ch.name[0] == '\0')
        strncpy(ch.name, DisplayFormatters::getModemPresetDisplayName(preset, false, true), sizeof(ch.name) - 1);
    ch.name[sizeof(ch.name) - 1] = '\0';
    return ch;
}

MeshBeaconModule::BeaconChannel MeshBeaconModule::resolveBeaconChannel(bool hasIndex, uint32_t index,
                                                                       meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    BeaconChannel out = {};
    out.index = channels.getPrimaryIndex();
    out.usable = true;
    if (hasIndex) {
        // Unusable means the caller skips this target: redirecting to the primary would beacon on
        // a channel nobody named.
        out.usable = index < (uint32_t)channels.getNumChannels() && channelSlotUsable(channels.getByIndex((ChannelIndex)index));
        if (out.usable)
            out.index = (ChannelIndex)index;
    }
    const meshtastic_ChannelSettings resolved = beaconChannelSettings(channels.getByIndex(out.index).settings, preset);
    strncpy(out.name, resolved.name, sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = '\0';
    return out;
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
        // Legacy compatibility: older firmware (pre-v2.7.20) drops hop_start==0 packets via the
        // pre-hop check before decryption, so they can't see has_bitfield to validate them.
        // Setting hop_start=1 (with hop_limit remaining 0) makes the packet pass the old check
        // while still being zero-hop (hop_limit=0 prevents any rebroadcast).
        if (s->legacyHopOverride)
            p->hop_start = 1;

        const meshtastic_Config_LoRaConfig target = effectiveLora(*s);

        // Only RF parameters are switched; the channel travels on the packet. Resolve the live slot
        // too - channel_num may still be 0 ("derive"), which compares unequal to a concrete slot.
        const uint16_t liveSlot =
            (uint16_t)RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));
        if (target.modem_preset == config.lora.modem_preset && target.use_preset == config.lora.use_preset &&
            target.channel_num == liveSlot && target.region == config.lora.region)
            return false;

        // Guard: never key up on an invalid target config - bad preset for the region, or an
        // unlicensed node keying up on a ham-only region. Refuse the switch here so we never
        // transmit on it; the radio driver drops the packet outright (see RadioLibInterface,
        // beaconTxConfigInvalid) rather than letting it fall through onto the current config.
        if (beaconTxConfigInvalid(p)) {
            LOG_DEBUG("Beacon: target preset %d/region %d invalid (or ham mismatch), skip", target.modem_preset, target.region);
            return false;
        }

        // Snapshot the live (non-beacon) config as "home". Skipped while a switch is already active,
        // so a second switch before the restore cannot capture the beacon config instead.
        if (!radioSwitched) {
            originalModemPreset = config.lora.modem_preset;
            originalUsePreset = config.lora.use_preset;
            originalLoraChannel = config.lora.channel_num;
            originalRegion = config.lora.region;
            switchDepth = 0;
        }
        switchDepth++;

        LOG_INFO("Beacon: switch #%u radio for packet 0x%08x to preset=%d slot=%u region=%d", switchDepth, p->id,
                 target.modem_preset, target.channel_num, target.region);
        if (switchDepth > 1)
            LOG_WARN("Beacon: switching again with no restore between; home preset=%d slot=%u region=%d still held",
                     originalModemPreset, originalLoraChannel, originalRegion);
        // Only the RF fields: the sidecar's copy of the rest is a snapshot of this same config, and
        // installing it wholesale would undo any edit made while the beacon was in flight.
        config.lora.modem_preset = target.modem_preset;
        // A target naming a preset means "run this preset", which a node on custom modem params
        // would otherwise ignore entirely - applyModemConfig() only reads modem_preset when set.
        config.lora.use_preset = target.use_preset;
        config.lora.channel_num = target.channel_num;
        config.lora.region = target.region;
        // TODO: bandwidth, spread_factor and coding_rate ride in the sidecar but are not installed
        // here, so a target's custom modem params are inert - applyModemConfig() reads the live ones.
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
        config.lora.use_preset = originalUsePreset;
        config.lora.channel_num = originalLoraChannel;
        config.lora.region = originalRegion;
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
    // p->channel already names the target's channel-table slot, so perhapsEncode() picks that
    // channel's key and stamps its hash. Beacons uplink to MQTT on that channel's uplink_enabled.
    const PacketId id = p->id; // every send() failure path frees p before returning
    releaseIfNotQueued(router->send(p), p, id);
}

void MeshBeaconBroadcastModule::sendBeacon()
{
    const auto &bcfg = moduleConfig.mesh_beacon;

    const bool hasText = bcfg.broadcast_message[0] != '\0';
    const bool hasRadioContent = bcfg.has_broadcast_offer_preset || offerChannelSettings(bcfg) != nullptr ||
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
        bool usePreset; // false = leave the node's custom modem params alone
        uint16_t slot;
        meshtastic_Config_LoRaConfig_RegionCode region;
        ChannelIndex channelIndex; // table slot to encrypt on; the primary when no channel is named
        // Resolved once here, then reused for the slot hash and the sidecar. Never empty.
        char channelName[sizeof(meshtastic_ChannelSettings::name)];
    };

    // The only place a target's slot is worked out, so a pin and a derivation see the same region,
    // preset and bandwidth. An inherited home slot the target's band cannot hold falls back to the
    // hash; a pin that does not fit is skipped before it reaches here.
    // Takes the resolved region, not the requested one: an EU sibling swap changes the band, and
    // with it the slot count the hash divides by.
    const auto targetProbe = [](const EffTarget &tgt, meshtastic_Config_LoRaConfig_RegionCode region, uint32_t seedSlot) {
        meshtastic_Config_LoRaConfig probe = config.lora;
        probe.use_preset = tgt.usePreset;
        probe.modem_preset = tgt.preset;
        probe.region = region;
        probe.channel_num = seedSlot;
        return probe;
    };
    const auto targetSlot = [&targetProbe](const EffTarget &tgt, meshtastic_Config_LoRaConfig_RegionCode region,
                                           uint32_t seedSlot) {
        return (uint16_t)RadioInterface::resolveFrequencySlot(targetProbe(tgt, region, seedSlot), tgt.channelName);
    };

    // The mesh the offer describes, resolved the same way a target's is so the two compare.
    const meshtastic_ChannelSettings *offerCh = offerChannelSettings(bcfg);
    const auto offerPreset = bcfg.has_broadcast_offer_preset ? bcfg.broadcast_offer_preset : config.lora.modem_preset;
    const auto offerRegion = (bcfg.broadcast_offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
                                 ? bcfg.broadcast_offer_region
                                 : config.lora.region;
    const uint16_t offerSlot = (uint16_t)offerFrequencySlot(bcfg);

    // The slot the node is already on, resolved the same way a target's is, so the two compare.
    const uint16_t homeSlot =
        (uint16_t)RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));

    // An empty list still beacons once, on the node's running preset and region over the primary
    // channel. Each entry below overrides only what it sets.
    const int targetCount = bcfg.broadcast_targets_count > 0 ? (int)bcfg.broadcast_targets_count : 1;

    // The payload is identical across targets, so a repeat of one already sent is pure wasted
    // airtime. Keyed on resolved values, so an explicit "current region" dedups against an UNSET one.
    EffTarget sent[4];
    meshtastic_Config_LoRaConfig_RegionCode sentRegion[4];
    int sentCount = 0;
    // Everything that reaches the air: the RF the packet goes out on, plus the channel slot that
    // keys its encryption. All equal means a byte-identical transmission.
    const auto sameEffectiveTarget = [](const EffTarget &a, meshtastic_Config_LoRaConfig_RegionCode ar, const EffTarget &b,
                                        meshtastic_Config_LoRaConfig_RegionCode br) {
        return a.preset == b.preset && a.usePreset == b.usePreset && ar == br && a.slot == b.slot &&
               a.channelIndex == b.channelIndex;
    };

    for (int ti = 0; ti < targetCount; ti++) {
        // Defaults: the running radio config over the primary channel. An entry overrides from here.
        EffTarget tgt = {};
        tgt.preset = config.lora.modem_preset;
        tgt.usePreset = config.lora.use_preset;
        const auto *bt = ti < (int)bcfg.broadcast_targets_count ? &bcfg.broadcast_targets[ti] : nullptr;
        if (bt) {
            // A named preset is an instruction to run it, so it also turns use_preset on.
            if (bt->has_preset) {
                tgt.preset = bt->preset;
                tgt.usePreset = true;
            }
            tgt.region = bt->region;
        }

        // The channel decides both the key and, through its name, the frequency slot. An index that
        // is out of range or disabled cannot be transmitted on - see resolveBeaconChannel.
        const BeaconChannel bc = resolveBeaconChannel(bt && bt->has_channel_index, bt ? bt->channel_index : 0, tgt.preset);
        if (!bc.usable) {
            // Skipped, not redirected: the config still asks for that channel, so turning it back
            // on brings this target back without the operator having to rewrite anything.
            LOG_DEBUG("Beacon: target %d channel_index %u unusable, skip", ti, bt->channel_index);
            continue;
        }
        tgt.channelIndex = bc.index;
        strncpy(tgt.channelName, bc.name, sizeof(tgt.channelName) - 1);

        // Must precede the slot: the band sets the slot count, so deriving from the region as
        // written puts two spellings of one mesh on different frequencies.
        meshtastic_Config_LoRaConfig_RegionCode resolvedRegion =
            (tgt.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? tgt.region : config.lora.region;
        if (tgt.usePreset) {
            const RegionInfo *r = getRegion(resolvedRegion);
            if (r && !r->supportsPreset(tgt.preset)) {
                const RegionInfo *swap = RadioInterface::regionSwapForPreset(resolvedRegion, tgt.preset);
                if (!swap) {
                    // Nothing this node can run: skip rather than transmit something else. The
                    // request stands, and a later region change may make it good again.
                    LOG_DEBUG("Beacon: target %d preset %d has no region here, skip", ti, tgt.preset);
                    continue;
                }
                resolvedRegion = swap->code;
            }
        }

        // A pin wins; a target on its own channel derives from that name; one on the primary
        // inherits the home slot, re-derived when the target's band is too small to hold it.
        const bool pinned = bt && bt->has_frequency_slot && bt->frequency_slot > 0;
        if (pinned && bt->frequency_slot > RadioInterface::frequencySlotCount(targetProbe(tgt, resolvedRegion, 0))) {
            // Skipped rather than derived, as for a channel or a preset: the operator named a
            // frequency, and beaconing on a different one is worse than not beaconing at all.
            LOG_DEBUG("Beacon: target %d frequency_slot %u not in the resolved region, skip", ti, bt->frequency_slot);
            continue;
        }
        const uint32_t seedSlot = pinned                                     ? bt->frequency_slot
                                  : (bc.index != channels.getPrimaryIndex()) ? 0
                                                                             : config.lora.channel_num;
        tgt.slot = targetSlot(tgt, resolvedRegion, seedSlot);

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

        // A target already on the offered mesh gains nothing from the offer. Gated on the offer
        // actually carrying a channel: without one it is an announcement, valid on any target.
        const bool offerRedundant = offerCh && (uint32_t)tgt.channelIndex == bcfg.broadcast_offer_channel_index &&
                                    offerPreset == tgt.preset && offerRegion == resolvedRegion && offerSlot == tgt.slot;
        if (offerRedundant)
            LOG_DEBUG("Beacon: target %d already runs the offered mesh, omit offer", ti);

        // Only RF parameters can require a switch now; the channel is named on the packet instead.
        // Against the resolved region, not the requested one: a target that named a sibling the
        // preset moved it off is already on the node's region, and switching to it is a no-op.
        const bool radioDiffers = (tgt.preset != config.lora.modem_preset) || (tgt.usePreset != config.lora.use_preset) ||
                                  (tgt.slot != homeSlot) || (resolvedRegion != config.lora.region);
        // Both halves of a legacy split are this one target, so the second joins the first's entry.
        int sharedEntry = -1;
        const auto applyTarget = [&](meshtastic_MeshPacket *p) {
            // Address the packet at the target's channel slot; perhapsEncode() keys off this index
            // and swaps in the wire hash, so the primary slot is never touched.
            p->channel = tgt.channelIndex;
            if (radioDiffers || legacySplit) {
                MeshBeaconModule_TargetRadioSettings s = {};
                // Resolve here, once: the entry describes the radio this packet needs, so nothing
                // downstream has to re-derive the region or the slot from a half-set config.
                s.lora = config.lora;
                s.lora.modem_preset = tgt.preset;
                s.lora.use_preset = tgt.usePreset;
                s.lora.channel_num = tgt.slot;
                s.lora.region = resolvedRegion;
                // Re-read at key-up: the resolved value alone would pin this beacon to the region
                // the node has since left.
                s.regionInherited = (tgt.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET);
                s.legacyHopOverride = legacySplit;
                strncpy(s.channelName, tgt.channelName, sizeof(s.channelName) - 1);
                sharedEntry = setTargetRadioSettings(p, s, sharedEntry);
            }
            sendBeaconPacket(p);
        };

        if (sendOfferOnly && offerSize > 0 && !offerRedundant) {
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

        // Re-encode without the offer for a target that already runs it; the text still stands.
        const uint8_t *combinedBuf = payloadCache;
        pb_size_t combinedSize = payloadCacheSize;
        uint8_t msgOnlyBuf[meshtastic_MeshBeacon_size];
        if (sendCombined && offerRedundant) {
            meshtastic_MeshBeacon msgOnly = meshtastic_MeshBeacon_init_zero;
            strncpy(msgOnly.message, bcfg.broadcast_message, sizeof(msgOnly.message) - 1);
            combinedBuf = msgOnlyBuf;
            combinedSize = (pb_size_t)pb_encode_to_bytes(msgOnlyBuf, sizeof(msgOnlyBuf), &meshtastic_MeshBeacon_msg, &msgOnly);
        }

        if (sendCombined && combinedSize > 0) {
            meshtastic_MeshPacket *p = allocDataPacket();
            if (!p) {
                LOG_WARN("Beacon: failed to allocate beacon packet (target %d)", ti);
                return;
            }
            memcpy(p->decoded.payload.bytes, combinedBuf, combinedSize);
            p->decoded.payload.size = combinedSize;
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
