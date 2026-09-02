#pragma once
#include "MeshRadio.h"
#include "Observer.h"
#include "ProtobufModule.h"
#include "RadioInterface.h"
#include "RadioTxHook.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh_beacon.pb.h"
#include "mesh/generated/meshtastic/module_config.pb.h"

// Short aliases for the MeshBeaconConfig.flags bitfield (see module_config.proto MeshBeaconConfig.Flags).
#define MESH_BEACON_FLAG_LISTEN_ENABLED meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LISTEN_ENABLED
#define MESH_BEACON_FLAG_BROADCAST_ENABLED meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_BROADCAST_ENABLED
#define MESH_BEACON_FLAG_LEGACY_SPLIT meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LEGACY_SPLIT

// Sidecar entry pairing packet IDs with the target radio settings they share for beacon TX.
typedef struct {
    // Legacy split sends the offer and the text as two packets on identical settings, so one entry
    // serves both. Zero means the entry is free; set by setTargetRadioSettings(), not by callers.
    uint8_t idCount;
    PacketId ids[2];
    // When the entry was armed. A beacon still queued a full broadcast interval later is
    // advertising stale mesh info, so it is dropped rather than transmitted.
    uint32_t armedAtMs;
    // When true, reconfigureForBeaconTX sets hop_start=1 so pre-2.7.20 firmware
    // (which drops hop_start==0 packets) accepts the zero-hop beacon.
    bool legacyHopOverride;
    // The radio this packet needs, whole, so a future per-target bandwidth or SF needs no field
    // added. Always a complete config: lora.region holds what the target resolved to at send time.
    meshtastic_Config_LoRaConfig lora;
    // The target named no region, so it follows the node's. Re-read at key-up rather than trusting
    // the send-time value: a region changed in between must not put this beacon on the old one.
    bool regionInherited;
    // The channel name the slot was hashed from, resolved once at send time so the TX path and
    // the pre-key-up validation cannot derive it two different ways.
    char channelName[sizeof(meshtastic_ChannelSettings::name)];
} MeshBeaconModule_TargetRadioSettings;

/**
 * Base class: holds the radio-switching sidecar table and static helpers.
 * The sidecar avoids touching MeshPacket proto fields for per-packet radio state.
 */
class MeshBeaconModule
{
  public:
    MeshBeaconModule();

    /**
     * Reconfigure the radio for beacon TX, or restore to original config if p is NULL.
     * Returns true if the radio was reconfigured (caller must re-run transmit delay for CCA).
     * Driven by the broadcast_targets entry associated with the packet.
     */
    static bool reconfigureForBeaconTX(RadioInterface *iface, meshtastic_MeshPacket *p);

    /**
     * Associate target radio settings with an outgoing packet by its ID, returning the entry used
     * or -1 if none could be taken. Pass that value back as shareWith for the second half of a
     * legacy split: the two packets are one target, so they ride one entry.
     */
    static int setTargetRadioSettings(const meshtastic_MeshPacket *p, const MeshBeaconModule_TargetRadioSettings &s,
                                      int shareWith = -1);

    /** The entry for this packet, or nullptr when it is not a beacon-switch packet. */
    static const MeshBeaconModule_TargetRadioSettings *getTargetRadioSettings(const meshtastic_MeshPacket *p);

    /**
     * Returns true if the sidecar table contains an entry for this packet's ID.
     * Used via MeshBeaconTxHook to keep the driver from listening while a beacon is queued.
     */
    static bool hasTargetRadioSettings(const meshtastic_MeshPacket *p);

    /**
     * Remove the sidecar entry for this packet after it has been sent.
     * Called via MeshBeaconTxHook once the driver is done with the packet.
     */
    static void clearTargetRadioSettings(const meshtastic_MeshPacket *p);

    /** Same, by id, for a packet the send path may already have freed. */
    static void clearTargetRadioSettingsById(PacketId id);

    /** Drop every entry. For tests, which reuse the table across cases. */
    static void clearAllTargetRadioSettings();

    /**
     * True if p is tagged for a beacon radio switch whose target config must NOT be transmitted:
     * preset invalid for the target region, or an unlicensed node would key up on a ham-only
     * (licensed-only) region. The radio driver drops such packets rather than sending them on the
     * current config. False for any packet without a sidecar entry (normal traffic is never affected).
     */
    static bool beaconTxConfigInvalid(const meshtastic_MeshPacket *p);

    /**
     * Reject only what can never become valid; sendBeacon() resolves the rest against the settings
     * in force. Called on an admin write, at boot, and when a LoRa change moves what can be run.
     */
    static void sanitiseConfig(meshtastic_ModuleConfig_MeshBeaconConfig &bcfg);

    /** Copy every offer field from config onto an outgoing beacon. */
    static void fillOffer(meshtastic_MeshBeacon &beacon, const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg);

    /** The channel the offer advertises, or nullptr when no slot is named or it is unusable. */
    static const meshtastic_ChannelSettings *offerChannelSettings(const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg);

    // The frequency slot the offer describes, and via derivedOut the one a receiver works out for
    // itself. They differ only where the mesh deliberately pins a slot that derivation would miss.
    static uint32_t offerFrequencySlot(const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg, uint32_t *derivedOut = nullptr);

    /** False only when a pinned offer slot does not exist in the region the offer is advertised for. */
    static bool offerIsPlaceable(const meshtastic_ModuleConfig_MeshBeaconConfig &bcfg);

    // Default a blank name to the TARGET preset's display name - not Channels::getName(), which
    // resolves it against the RUNNING preset. A node joining on the target preset derives the same.
    static meshtastic_ChannelSettings beaconChannelSettings(const meshtastic_ChannelSettings &base,
                                                            meshtastic_Config_LoRaConfig_ModemPreset preset);

    /** Where a target transmits: the channel-table slot, and the name its frequency slot hashes from. */
    struct BeaconChannel {
        ChannelIndex index;                                  // the primary when the target names none
        char name[sizeof(meshtastic_ChannelSettings::name)]; // never empty
        bool usable;                                         // false = the named slot cannot be transmitted on
    };

    // Out of range or disabled means the target is skipped, so admin validation and the TX path
    // cannot disagree about which channel a target runs on, nor which slot it lands on.
    static BeaconChannel resolveBeaconChannel(bool hasIndex, uint32_t index, meshtastic_Config_LoRaConfig_ModemPreset preset);

  protected:
    static meshtastic_Config_LoRaConfig_ModemPreset originalModemPreset;
    static uint16_t originalLoraChannel;
    static meshtastic_Config_LoRaConfig_RegionCode originalRegion;
    static bool originalUsePreset;
};

/**
 * Carries the beacon's radio switching into the radio driver's TX lifecycle, so the driver holds no
 * beacon-specific code. One instance is created with the beacon modules and registers itself.
 */
class MeshBeaconTxHook : public RadioTxHook
{
  public:
    PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p) override;
    bool holdsRadio(const meshtastic_MeshPacket *p) override;
    void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p) override;
};

extern MeshBeaconTxHook *meshBeaconTxHook;

/**
 * Broadcaster: periodically sends MeshBeacon packets on the configured preset/channel.
 * Active only when the FLAG_BROADCAST_ENABLED bit is set in moduleConfig.mesh_beacon.flags.
 * Inherits ProtobufModule to access allocDataProtobuf + setStartDelay.
 *
 * Packet flow:
 *  Normal (combined):  one MESH_BEACON_APP carrying offer + message on the beacon radio config.
 *  Legacy split:       two packets when both text and offer are present and FLAG_LEGACY_SPLIT is set,
 *                      both sent on the same beacon radio settings:
 *                        A) MESH_BEACON_APP with offer only (no text).
 *                        B) TEXT_MESSAGE_APP with the text (for clients that only decode TEXT_MESSAGE_APP).
 */
class MeshBeaconBroadcastModule : private MeshBeaconModule,
                                  public ProtobufModule<meshtastic_MeshBeacon>,
                                  private concurrency::OSThread
{
  public:
    MeshBeaconBroadcastModule();

    // Mark the cached payload dirty (call after config change).
    void invalidateCache() { payloadCacheDirty = true; }

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &, meshtastic_MeshBeacon *) override { return false; }
    virtual int32_t runOnce() override;

  protected:
    void sendBeacon();
    void rebuildCache();

    // Send one beacon packet. p->channel names the target's channel-table slot, so it is
    // encrypted with that channel's key rather than the primary's.
    void sendBeaconPacket(meshtastic_MeshPacket *p);

    bool payloadCacheDirty = true;
    uint8_t payloadCache[meshtastic_MeshBeacon_size] = {};
    pb_size_t payloadCacheSize = 0;
};
extern MeshBeaconBroadcastModule *meshBeaconBroadcastModule;

/**
 * Listener: receives MESH_BEACON_APP packets and caches any offered channel/preset for the client
 * app to retrieve. It does NOT unwrap the text into a separate message - the original beacon packet
 * already reaches the client (handler returns CONTINUE), which reads `message` from it directly.
 * Does NOT auto-apply offered settings - client app must do so explicitly.
 * Active only when the FLAG_LISTEN_ENABLED bit is set in moduleConfig.mesh_beacon.flags.
 */
class MeshBeaconListenerModule : public ProtobufModule<meshtastic_MeshBeacon>, public Observable<const meshtastic_MeshPacket *>
{
  public:
    MeshBeaconListenerModule();

    struct BeaconOffer {
        bool valid;
        NodeNum sender;
        bool has_channel;
        meshtastic_ChannelSettings channel;
        meshtastic_Config_LoRaConfig_RegionCode region;
        meshtastic_Config_LoRaConfig_ModemPreset preset;
        // Present only when the sender could not expect us to derive it; unset means derive.
        bool has_frequency_slot;
        uint32_t frequency_slot;
        uint32_t received_at;
    };

    // Last received offer - accessible to admin/API for client app retrieval.
    static BeaconOffer lastReceivedOffer;

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MeshBeacon *b) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};
extern MeshBeaconListenerModule *meshBeaconListenerModule;
