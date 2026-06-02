#include "MeshBeaconModule.h"
#include "Default.h"
#include "MeshService.h"
#include "RTC.h"
#include "RadioInterface.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>
#include <stdlib.h>

// TODO(beacon): Remove virtual nodenum. Beacon packets will originate from the real local node.
#define NODENUM_BEACON 0x00000005

static meshtastic_Config_LoRaConfig_ModemPreset originalModemPreset;                   // original modem preset
static uint16_t originalLoraChannel;                                                   // original frequency slot
char originalBeaconChannelName[sizeof(((meshtastic_ChannelSettings *)nullptr)->name)]; // original channel name

typedef struct {
    bool inUse;
    PacketId id;
    MeshBeaconModule_TXSettings settings;
} MeshBeaconModule_TargetRadioSettings;

static MeshBeaconModule_TargetRadioSettings targetRadioSettings[8];

// Internal: look up sidecar entry by packet ID.
static bool getTargetRadioSettings(const meshtastic_MeshPacket *p, MeshBeaconModule_TXSettings *settings)
{
    if (!p)
        return false;

    for (auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == p->id) {
            if (settings)
                *settings = entry.settings;
            return true;
        }
    }
    return false;
}

MeshBeaconModule::MeshBeaconModule()
{
    originalModemPreset = config.lora.modem_preset;
    originalLoraChannel = config.lora.channel_num;
    strncpy(originalBeaconChannelName, channels.getPrimary().name, sizeof(originalBeaconChannelName));
}

void MeshBeaconModule::setTargetRadioSettings(const meshtastic_MeshPacket *p, MeshBeaconModule_TXSettings settings)
{
    if (!p)
        return;

    MeshBeaconModule_TargetRadioSettings *slot = nullptr;
    for (auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == p->id) {
            slot = &entry;
            break;
        }
        if (!slot && !entry.inUse)
            slot = &entry;
    }

    // All 8 slots full: evict slot 0 (silent overwrite)
    if (!slot)
        slot = &targetRadioSettings[0];

    slot->inUse = true;
    slot->id = p->id;
    slot->settings = settings;
}

bool MeshBeaconModule::hasTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    return getTargetRadioSettings(p, nullptr);
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

bool MeshBeaconModule::configureRadioForPacket(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    // TODO(beacon): Drive from broadcast_on_preset / broadcast_on_channel in MeshBeaconConfig
    //   rather than the per-packet sidecar table.
    meshtastic_ChannelSettings *c = (meshtastic_ChannelSettings *)&channels.getPrimary();
    MeshBeaconModule_TXSettings target;
    if (p && p->from == NODENUM_BEACON && getTargetRadioSettings(p, &target) &&
        (target.preset != config.lora.modem_preset || target.slot != config.lora.channel_num)) {
        LOG_INFO("Beacon: reconfiguring radio for TX of packet %#08lx (from=%#08lx size=%lu)", p->id, p->from,
                 p->decoded.payload.size);

        config.lora.modem_preset = target.preset;
        config.lora.channel_num = target.slot;
        memset(c->name, 0, sizeof(c->name));

        switch (target.preset) {
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
            strncpy(c->name, "ShortTurbo", sizeof(c->name));
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
            strncpy(c->name, "ShortFast", sizeof(c->name));
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
            strncpy(c->name, "ShortSlow", sizeof(c->name));
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
            strncpy(c->name, "MediumFast", sizeof(c->name));
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
            strncpy(c->name, "MediumSlow", sizeof(c->name));
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
            strncpy(c->name, "LongFast", sizeof(c->name));
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
            strncpy(c->name, "LongMod", sizeof(c->name));
            break;
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
            strncpy(c->name, "LongSlow", sizeof(c->name));
            break;
        default:
            break;
        }

        channels.fixupChannel(channels.getPrimaryIndex());
        p->channel = channels.getHash(channels.getPrimaryIndex());
        iface->reconfigure();
        return true;

    } else if ((!p || !getTargetRadioSettings(p, nullptr)) &&
               (config.lora.modem_preset != originalModemPreset || config.lora.channel_num != originalLoraChannel)) {
        if (p)
            LOG_INFO("Beacon: reconfiguring radio for TX of packet %#08lx (from=%#08lx size=%lu)", p->id, p->from,
                     p->decoded.payload.size);
        else
            LOG_INFO("Beacon: restoring radio config after beacon TX");

        config.lora.modem_preset = originalModemPreset;
        config.lora.channel_num = originalLoraChannel;
        memset(c->name, 0, sizeof(c->name));
        strncpy(c->name, originalBeaconChannelName, sizeof(c->name));

        channels.fixupChannel(channels.getPrimaryIndex());
        iface->reconfigure();
        return true;
    }
    return false;
}

MeshBeaconModule_TXSettings MeshBeaconModule::stripTargetRadioSettings(meshtastic_MeshPacket *p)
{
    // TODO(beacon): Remove entirely. Beacon payloads are structured MeshBeacon protobufs,
    //   not freeform text with embedded #PPNN routing directives.
    MeshBeaconModule_TXSettings s = {
        .preset = originalModemPreset,
        .slot = originalLoraChannel,
    };

    if (!p || p->decoded.payload.size < 4 || p->decoded.payload.bytes[0] != '#')
        return s;

    if (p->decoded.payload.size < sizeof(p->decoded.payload.bytes))
        p->decoded.payload.bytes[p->decoded.payload.size] = 0;
    else
        p->decoded.payload.bytes[sizeof(p->decoded.payload.bytes) - 1] = 0;

    char *msg = strchr((char *)p->decoded.payload.bytes, ' ');
    if (!msg || !*msg)
        return s;
    *msg++ = 0;

    const char presetString[3] = {p->decoded.payload.bytes[1], p->decoded.payload.bytes[2], 0};
    char *slotString = (char *)&p->decoded.payload.bytes[3];
    if (!*slotString)
        return s;

    for (char *c = slotString; *c; c++) {
        if (!strchr("1234567890", *c))
            return s;
    }

    if (!strncmp(presetString, "ST", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    else if (!strncmp(presetString, "SF", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    else if (!strncmp(presetString, "SS", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW;
    else if (!strncmp(presetString, "MF", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    else if (!strncmp(presetString, "MS", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW;
    else if (!strncmp(presetString, "LF", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    else if (!strncmp(presetString, "LM", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE;
    else if (!strncmp(presetString, "LS", 2))
        s.preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    else
        return s;

    s.slot = strtoul(slotString, nullptr, 10);

    p->decoded.payload.size = 1;
    for (char *a = msg, *b = (char *)p->decoded.payload.bytes; (*b++ = *a++);)
        p->decoded.payload.size++;

    return s;
}

// ---------------------------------------------------------------------------
// MeshBeaconNodeInfoModule
// TODO(beacon): Remove this class. Replaced by real-node broadcasts.
// ---------------------------------------------------------------------------

MeshBeaconNodeInfoModule *meshBeaconNodeInfoModule;

bool MeshBeaconNodeInfoModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *pptr)
{
    return true;
}

void MeshBeaconNodeInfoModule::sendBeaconNodeInfo()
{
    // TODO(beacon): Remove - replaced by MeshBeaconBroadcastModule sending structured MeshBeacon packets.
    LOG_INFO("Beacon: send NodeInfo for mesh beacon");
    static meshtastic_User u = {
        .hw_model = meshtastic_HardwareModel_PRIVATE_HW,
        .is_licensed = false,
        .role = meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE,
        .public_key =
            {
                .size = 32,
                .bytes = {0x39, 0x37, 0x58, 0xe4, 0x05, 0x34, 0x7d, 0xe0, 0x49, 0x73, 0xec, 0xaf, 0xbc, 0x8e, 0x07, 0xe8,
                          0x66, 0x57, 0xe4, 0xa1, 0x2d, 0x53, 0x0e, 0x26, 0x51, 0x1f, 0x1a, 0x6c, 0xbf, 0xe8, 0x5e, 0x04},
            },
        .has_is_unmessagable = true,
        .is_unmessagable = true,
    };
    // TODO(beacon): long_name, short_name, id should come from config (broadcast_node_id / device name)
    strncpy(u.id, "!mesh_bcn", sizeof(u.id));
    strncpy(u.long_name, "Mesh Beacon Node", sizeof(u.long_name));
    strncpy(u.short_name, "BCN", sizeof(u.short_name));
    meshtastic_MeshPacket *p = allocDataProtobuf(u);
    p->to = NODENUM_BROADCAST;
    p->from = NODENUM_BEACON;
    p->hop_limit = 0;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    // TODO(beacon): The second copy on a different preset comes from broadcast_on_preset config.
    meshtastic_MeshPacket *p_LF20 = packetPool.allocCopy(*p);
    service->sendToMesh(p, RX_SRC_LOCAL, false);

    setTargetRadioSettings(p_LF20, {
                                       .preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                                       .slot = 20,
                                   });
    service->sendToMesh(p_LF20, RX_SRC_LOCAL, false);
}

MeshBeaconNodeInfoModule::MeshBeaconNodeInfoModule()
    : ProtobufModule("nodeinfo_beacon", meshtastic_PortNum_NODEINFO_APP, &meshtastic_User_msg),
      concurrency::OSThread("MeshBeaconNodeInfo")
{
    MeshBeaconModule();
    setIntervalFromNow(setStartDelay());
}

int32_t MeshBeaconNodeInfoModule::runOnce()
{
    if (airTime->isTxAllowedAirUtil() && config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        sendBeaconNodeInfo();
    }
    // TODO(beacon): Use broadcast_interval_secs from MeshBeaconConfig (min 3600 s, max 259200 s).
    return Default::getConfiguredOrDefaultMs(config.device.node_info_broadcast_secs, default_node_info_broadcast_secs);
}

// ---------------------------------------------------------------------------
// MeshBeaconMessageModule
// TODO(beacon): Split into MeshBeaconBroadcastModule + MeshBeaconListenerModule (see header).
// ---------------------------------------------------------------------------

MeshBeaconMessageModule *meshBeaconMessageModule;

ProcessMessage MeshBeaconMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // TODO(beacon): On the broadcaster side: read broadcast_message / broadcast_offer_channel /
    //   broadcast_offer_preset from config, build a MeshBeacon protobuf, and send it periodically
    //   via the OSThread. This relay-incoming-message logic is replaced entirely.
    //
    // TODO(beacon): On the listener side: decode MeshBeacon protobuf, deliver text portion as a
    //   TEXT_MESSAGE_APP packet to the inbox, store offered channel / preset in a local cache for
    //   client app retrieval. Do NOT apply offered settings automatically.
#ifdef DEBUG_PORT
    auto &d = mp.decoded;
#endif
    meshtastic_MeshPacket *p = packetPool.allocCopy(mp);
    MeshBeaconModule_TXSettings s = stripTargetRadioSettings(p);
    if (!p->decoded.payload.size || p->decoded.payload.bytes[0] == '#')
        return ProcessMessage::STOP;
    p->to = NODENUM_BROADCAST;
    p->decoded.source = p->from;
    p->from = NODENUM_BEACON;
    p->channel = channels.getPrimaryIndex();
    p->hop_limit = 0;
    p->hop_start = 0;
    p->rx_rssi = 0;
    p->rx_snr = 0;
    p->priority = meshtastic_MeshPacket_Priority_HIGH;
    p->want_ack = false;
    if (s.preset != originalModemPreset || s.slot != originalLoraChannel) {
        setTargetRadioSettings(p, s);
    }
    p->rx_time = getValidTime(RTCQualityFromNet);
    service->sendToMesh(p, RX_SRC_LOCAL, false);

    powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE;
}

bool MeshBeaconMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // TODO(beacon): Replace with: p->decoded.portnum == meshtastic_PortNum_MESH_BEACON_APP
    //   No special named channel needed — any node with beacons_listen=true receives beacon packets.
    meshtastic_Channel *c = &channels.getByIndex(p->channel);
    return c->role == meshtastic_Channel_Role_SECONDARY && strlen(c->settings.name) == strlen("Beacon") &&
           !strcmp(c->settings.name, "Beacon") && p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP;
}
