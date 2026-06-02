#pragma once
#include "MeshRadio.h"
#include "Observer.h"
#include "ProtobufModule.h"
#include "RadioInterface.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh_beacon.pb.h"
#include "mesh/generated/meshtastic/module_config.pb.h"

// Sidecar entry pairing a packet ID with target radio settings for beacon TX.
typedef struct {
    bool inUse;
    PacketId id;
    meshtastic_Config_LoRaConfig_ModemPreset preset;
    uint16_t slot;
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
    static void setTargetRadioSettings(const meshtastic_MeshPacket *p, meshtastic_Config_LoRaConfig_ModemPreset preset,
                                       uint16_t slot);

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

  protected:
    static meshtastic_Config_LoRaConfig_ModemPreset originalModemPreset;
    static uint16_t originalLoraChannel;
    static char originalChannelName[12]; // matches ChannelSettings.name max_size
};

/**
 * Broadcaster: periodically sends MeshBeacon packets on the configured preset/channel.
 * Active only when moduleConfig.mesh_beacon.broadcast_enabled is true.
 * Inherits ProtobufModule to access allocDataProtobuf + setStartDelay.
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

  private:
    void sendBeacon();
    void rebuildCache();

    bool payloadCacheDirty = true;
    uint8_t payloadCache[meshtastic_MeshBeacon_size];
    pb_size_t payloadCacheSize = 0;
};
extern MeshBeaconBroadcastModule *meshBeaconBroadcastModule;

/**
 * Listener: receives MESH_BEACON_APP packets, delivers text to the local inbox,
 * and caches any offered channel/preset for the client app to retrieve.
 * Does NOT auto-apply offered settings — client app must do so explicitly.
 * Active only when moduleConfig.mesh_beacon.listen_enabled is true.
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

    // Last received offer — accessible to admin/API for client app retrieval.
    static BeaconOffer lastReceivedOffer;

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MeshBeacon *b) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};
extern MeshBeaconListenerModule *meshBeaconListenerModule;
