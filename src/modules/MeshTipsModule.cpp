#include "MeshTipsModule.h"
#include "Default.h"
#include "MeshService.h"
#include "RTC.h"
#include "RadioInterface.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>
#include <stdlib.h>

#define NODENUM_TIPS 0x00000004

static meshtastic_Config_LoRaConfig_ModemPreset originalModemPreset;             // original modem preset
static uint16_t originalLoraChannel;                                             // original frequency slot
char originalChannelName[sizeof(((meshtastic_ChannelSettings *)nullptr)->name)]; // original channel name

typedef struct {
    bool inUse;
    PacketId id;
    MeshTipsModule_TXSettings settings;
} MeshTipsModule_TargetRadioSettings;

static MeshTipsModule_TargetRadioSettings targetRadioSettings[8];

static bool getTargetRadioSettings(const meshtastic_MeshPacket *p, MeshTipsModule_TXSettings *settings)
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

MeshTipsModule::MeshTipsModule()
{
    originalModemPreset = config.lora.modem_preset;
    originalLoraChannel = config.lora.channel_num;
    strncpy(originalChannelName, channels.getPrimary().name, sizeof(originalChannelName));
}

void MeshTipsModule::setTargetRadioSettings(const meshtastic_MeshPacket *p, MeshTipsModule_TXSettings settings)
{
    if (!p)
        return;

    MeshTipsModule_TargetRadioSettings *slot = nullptr;
    for (auto &entry : targetRadioSettings) {
        if (entry.inUse && entry.id == p->id) {
            slot = &entry;
            break;
        }
        if (!slot && !entry.inUse)
            slot = &entry;
    }

    if (!slot)
        slot = &targetRadioSettings[0];

    slot->inUse = true;
    slot->id = p->id;
    slot->settings = settings;
}

bool MeshTipsModule::hasTargetRadioSettings(const meshtastic_MeshPacket *p)
{
    return getTargetRadioSettings(p, nullptr);
}

void MeshTipsModule::clearTargetRadioSettings(const meshtastic_MeshPacket *p)
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

bool MeshTipsModule::configureRadioForPacket(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    meshtastic_ChannelSettings *c = (meshtastic_ChannelSettings *)&channels.getPrimary();
    MeshTipsModule_TXSettings target;
    if (p && p->from == NODENUM_TIPS && getTargetRadioSettings(p, &target) &&
        (target.preset != config.lora.modem_preset || target.slot != config.lora.channel_num)) {
        LOG_INFO("Reconfiguring for TX of packet %#08lx (from=%#08lx size=%lu)", p->id, p->from, p->decoded.payload.size);

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
        }

        channels.fixupChannel(channels.getPrimaryIndex());
        p->channel = channels.getHash(channels.getPrimaryIndex());
        iface->reconfigure();

        return true;
    } else if ((!p || !getTargetRadioSettings(p, nullptr)) &&
               (config.lora.modem_preset != originalModemPreset || config.lora.channel_num != originalLoraChannel)) {
        if (p)
            LOG_INFO("Reconfiguring for TX of packet %#08lx (from=%#08lx size=%lu)", p->id, p->from, p->decoded.payload.size);
        else
            LOG_INFO("Restoring radio config after tips TX");

        config.lora.modem_preset = originalModemPreset;
        config.lora.channel_num = originalLoraChannel;
        memset(c->name, 0, sizeof(c->name));
        strncpy(c->name, originalChannelName, sizeof(c->name));

        channels.fixupChannel(channels.getPrimaryIndex());
        iface->reconfigure();
        return true;
    }
    return false;
}

MeshTipsModule_TXSettings MeshTipsModule::stripTargetRadioSettings(meshtastic_MeshPacket *p)
{
    MeshTipsModule_TXSettings s = {
        .preset = originalModemPreset,
        .slot = originalLoraChannel,
    };

    if (!p || p->decoded.payload.size < 4 || p->decoded.payload.bytes[0] != '#')
        return s;

    // Clamp final byte of payload while parsing the command prefix.
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
    // don't use strcpy, because strcpy has undefined behaviour if src & dst overlap
    for (char *a = msg, *b = (char *)p->decoded.payload.bytes; (*b++ = *a++);)
        p->decoded.payload.size++;

    return s;
}

MeshTipsNodeInfoModule *meshTipsNodeInfoModule;

bool MeshTipsNodeInfoModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *pptr)
{
    // do nothing if we receive nodeinfo, because we only care about sending our own
    return true;
}

void MeshTipsNodeInfoModule::sendTipsNodeInfo()
{
    LOG_INFO("Send NodeInfo for mesh tips");
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
    strncpy(u.id, "!mesh_tips", sizeof(u.id));
    strncpy(u.long_name, "WLG Mesh Tips Robot", sizeof(u.long_name));
    strncpy(u.short_name, "TIPS", sizeof(u.short_name));
    meshtastic_MeshPacket *p = allocDataProtobuf(u);
    p->to = NODENUM_BROADCAST;
    p->from = NODENUM_TIPS;
    p->hop_limit = 0;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    meshtastic_MeshPacket *p_LF20 = packetPool.allocCopy(*p);
    service->sendToMesh(p, RX_SRC_LOCAL, false);

    setTargetRadioSettings(p_LF20, {
                                       .preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                                       .slot = 20,
                                   });
    service->sendToMesh(p_LF20, RX_SRC_LOCAL, false);
}

MeshTipsNodeInfoModule::MeshTipsNodeInfoModule()
    : ProtobufModule("nodeinfo_tips", meshtastic_PortNum_NODEINFO_APP, &meshtastic_User_msg),
      concurrency::OSThread("MeshTipsNodeInfo")
{
    MeshTipsModule();

    setIntervalFromNow(setStartDelay()); // Send our initial owner announcement 30 seconds
                                         // after we start (to give network time to setup)
}

int32_t MeshTipsNodeInfoModule::runOnce()
{
    if (airTime->isTxAllowedAirUtil() && config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        sendTipsNodeInfo();
    }
    return Default::getConfiguredOrDefaultMs(config.device.node_info_broadcast_secs, default_node_info_broadcast_secs);
}

MeshTipsMessageModule *meshTipsMessageModule;

ProcessMessage MeshTipsMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef DEBUG_PORT
    auto &d = mp.decoded;
#endif

    meshtastic_MeshPacket *p = packetPool.allocCopy(mp);
    MeshTipsModule_TXSettings s = stripTargetRadioSettings(p);
    if (!p->decoded.payload.size || p->decoded.payload.bytes[0] == '#')
        return ProcessMessage::STOP;
    p->to = NODENUM_BROADCAST;
    p->decoded.source = p->from;
    p->from = NODENUM_TIPS;
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

    return ProcessMessage::CONTINUE; // No other module should be caring about this message
}

bool MeshTipsMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    meshtastic_Channel *c = &channels.getByIndex(p->channel);
    return c->role == meshtastic_Channel_Role_SECONDARY && strlen(c->settings.name) == strlen("Tips") &&
           !strcmp(c->settings.name, "Tips") && p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP;
}
