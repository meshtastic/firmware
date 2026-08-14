#pragma once
#include "MeshRadio.h"
#include "Observer.h"
#include "ProtobufModule.h"
#include "RadioInterface.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh_beacon.pb.h"
#include "mesh/generated/meshtastic/module_config.pb.h"

// Short aliases for the MeshBeaconConfig.flags bitfield (see module_config.proto MeshBeaconConfig.Flags).
#define MESH_BEACON_FLAG_LISTEN_ENABLED meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LISTEN_ENABLED
#define MESH_BEACON_FLAG_BROADCAST_ENABLED meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_BROADCAST_ENABLED
#define MESH_BEACON_FLAG_LEGACY_SPLIT meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LEGACY_SPLIT

// Sidecar entry pairing a packet ID with target radio settings for beacon TX.
typedef struct {
    bool inUse;
    PacketId id;
    meshtastic_Config_LoRaConfig_ModemPreset preset;
    uint16_t slot;
    // When true, reconfigureForBeaconTX sets hop_start=1 so pre-2.7.20 firmware
    // (which drops hop_start==0 packets) accepts the zero-hop beacon.
    bool legacyHopOverride;
    // Per-target radio settings. UNSET region means use current lora.region.
    meshtastic_Config_LoRaConfig_RegionCode region;
    bool has_channel;
    meshtastic_ChannelSettings channel;
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
     * Driven by broadcast_on_preset / broadcast_on_channel from MeshBeaconConfig.
     */
    static bool reconfigureForBeaconTX(RadioInterface *iface, meshtastic_MeshPacket *p);

    /**
     * Associate target radio settings with an outgoing packet by its ID.
     * Sidecar holds 8 entries; evicts slot 0 on overflow.
     */
    static void
    setTargetRadioSettings(const meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset preset, uint16_t slot,
                           bool legacyHopOverride = false,
                           meshtastic_Config_LoRaConfig_RegionCode region = meshtastic_Config_LoRaConfig_RegionCode_UNSET,
                           bool has_channel = false, const meshtastic_ChannelSettings *channel = nullptr);

    /**
     * Returns true if the sidecar table contains an entry for this packet's ID.
     * Used by RadioLibInterface to gate the channel-active check.
     */
    static bool hasTargetRadioSettings(const meshtastic_MeshPacket *p);

    /**
     * Remove the sidecar entry for this packet after it has been sent.
     * Called from RadioLibInterface::completeSending().
     */
    static void clearTargetRadioSettings(const meshtastic_MeshPacket *p);

    /**
     * True if p is tagged for a beacon radio switch whose target config must NOT be transmitted:
     * preset invalid for the target region, or an unlicensed node would key up on a ham-only
     * (licensed-only) region. The radio driver drops such packets rather than sending them on the
     * current config. False for any packet without a sidecar entry (normal traffic is never affected).
     */
    static bool beaconTxConfigInvalid(const meshtastic_MeshPacket *p);

  protected:
    /**
     * Build the ChannelSettings the beacon transmits on: the base (primary) channel overlaid with
     * any broadcast_on_channel overrides, defaulting an empty name to the target preset's display
     * name. Shared by the encrypt-time channel swap and the radio-thread RF swap so the channel
     * key + hash are identical at both points.
     */
    static meshtastic_ChannelSettings beaconChannelSettings(const meshtastic_ChannelSettings &base,
                                                            meshtastic_Config_LoRaConfig_ModemPreset preset,
                                                            const meshtastic_ChannelSettings *overrideChannel = nullptr);

    static meshtastic_Config_LoRaConfig_ModemPreset originalModemPreset;
    static uint16_t originalLoraChannel;
    static meshtastic_Config_LoRaConfig_RegionCode originalRegion;
    static meshtastic_ChannelSettings originalPrimaryChannel;
};

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

    // Send one beacon packet. When overrideChannel is set and has a name/PSK override,
    // the packet is encrypted with that channel's key (not the primary's).
    void sendBeaconPacket(meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset targetPreset,
                          bool has_channel = false, const meshtastic_ChannelSettings *overrideChannel = nullptr);

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
        uint32_t received_at;
    };

    // Last received offer - accessible to admin/API for client app retrieval.
    static BeaconOffer lastReceivedOffer;

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MeshBeacon *b) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};
extern MeshBeaconListenerModule *meshBeaconListenerModule;
