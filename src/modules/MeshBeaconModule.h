#pragma once
#include "Observer.h"
#include "ProtobufModule.h"
#include "RadioInterface.h"
#include "SinglePortModule.h"

// TODO(beacon): Replace with config-driven approach - beacon_on_preset / beacon_on_channel
// The radio settings for a single outgoing beacon packet
typedef struct {
    meshtastic_Config_LoRaConfig_ModemPreset preset;
    uint16_t slot;
} MeshBeaconModule_TXSettings;

/**
 * Base class for the beacon module.
 * Holds the radio-switching sidecar table and static helpers shared by
 * MeshBeaconNodeInfoModule and MeshBeaconMessageModule.
 *
 * TODO(beacon): Remove virtual-node / NODENUM_BEACON approach entirely.
 *   Beacons originate from the real local node. The NodeInfo sub-module
 *   and the fixed NODENUM are carryover from MeshTips and will be removed.
 *
 * TODO(beacon): Replace stripTargetRadioSettings() text-prefix parsing (#PPNN)
 *   with structured config fields (broadcast_on_preset, broadcast_on_channel).
 *
 * TODO(beacon): Replace TEXT_MESSAGE_APP portnum filter with MESH_BEACON_APP portnum
 *   once that portnum is added to portnums.proto and generated code is regenerated.
 *
 * TODO(beacon): Add MeshBeaconConfig to module_config.proto (see plan in session memory).
 *
 * TODO(beacon): Add AdminMessage handling so broadcast_message, intervals, and
 *   offer_channel / offer_preset can be set remotely via admin.
 */
class MeshBeaconModule
{
  public:
    MeshBeaconModule();

    /**
     * Configure the radio to send the target packet, or restore to default config if p is NULL.
     * Returns true if the radio was reconfigured (caller should insert a new transmit delay
     * to let the new channel clear before sending).
     *
     * TODO(beacon): Drive target settings from broadcast_on_preset / broadcast_on_channel in
     *   MeshBeaconConfig rather than a per-packet sidecar table lookup.
     */
    static bool configureRadioForPacket(RadioInterface *iface, meshtastic_MeshPacket *p);

    /**
     * Associate target radio settings with an outgoing packet by its ID.
     * The sidecar table holds up to 8 entries; the oldest is evicted on overflow.
     */
    static void setTargetRadioSettings(const meshtastic_MeshPacket *p, MeshBeaconModule_TXSettings settings);

    /**
     * Returns true if the sidecar table contains an entry for this packet's ID.
     * Used by RadioLibInterface to gate the channel-active check on non-default-preset packets.
     */
    static bool hasTargetRadioSettings(const meshtastic_MeshPacket *p);

    /**
     * Remove the sidecar entry for this packet after it has been sent.
     * Called from RadioLibInterface::completeSending().
     */
    static void clearTargetRadioSettings(const meshtastic_MeshPacket *p);

    /**
     * Parse and strip the #PPNN command prefix from an incoming text payload.
     * Returns target radio settings derived from the prefix (or defaults if no valid prefix).
     * Mutates p->decoded.payload in-place: strips the prefix, leaving only the message body.
     *
     * TODO(beacon): Remove entirely - beacon payloads will be structured MeshBeacon protobufs,
     *   not freeform text with embedded routing directives. Radio target comes from config, not payload.
     */
    MeshBeaconModule_TXSettings stripTargetRadioSettings(meshtastic_MeshPacket *p);
};

/**
 * Periodic NodeInfo broadcaster for the beacon virtual node.
 *
 * TODO(beacon): Remove this class entirely. Beacons originate from the real node; there is
 *   no virtual nodenum. The periodic broadcast is replaced by MeshBeaconBroadcastModule (OSThread).
 */
class MeshBeaconNodeInfoModule : private MeshBeaconModule, public ProtobufModule<meshtastic_User>, private concurrency::OSThread
{
  public:
    MeshBeaconNodeInfoModule();

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *p) override;

    void sendBeaconNodeInfo();

    virtual int32_t runOnce() override;
};
extern MeshBeaconNodeInfoModule *meshBeaconNodeInfoModule;

/**
 * Text message relay / listener for the beacon module.
 *
 * TODO(beacon): Split into two separate classes:
 *   - MeshBeaconBroadcastModule (OSThread) — periodically sends MeshBeacon protobuf packets
 *     on broadcast_on_preset / broadcast_on_channel with the configured broadcast_message,
 *     broadcast_offer_channel, and broadcast_offer_preset. Only active when beacon_broadcast=true.
 *   - MeshBeaconListenerModule (SinglePortModule on MESH_BEACON_APP) — receives MeshBeacon
 *     packets, delivers text to the text message inbox, and stores the offered channel/preset
 *     for retrieval by a client app. Only active when beacons_listen=true.
 *     Does NOT auto-apply offered channel or preset — client app must do this explicitly.
 *
 * TODO(beacon): wantPacket() currently filters on a "Beacon" named SECONDARY channel +
 *   TEXT_MESSAGE_APP. Replace with MESH_BEACON_APP portnum filter (no special channel needed).
 *
 * TODO(beacon): handleReceived() currently relays incoming messages from another node.
 *   Replace with originating logic: read broadcast_message from config, build MeshBeacon
 *   proto, set target radio settings from broadcast_on_preset, send periodically.
 */
class MeshBeaconMessageModule : private MeshBeaconModule,
                                public SinglePortModule,
                                public Observable<const meshtastic_MeshPacket *>
{
  public:
    MeshBeaconMessageModule() : MeshBeaconModule(), SinglePortModule("beacon", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};
extern MeshBeaconMessageModule *meshBeaconMessageModule;
