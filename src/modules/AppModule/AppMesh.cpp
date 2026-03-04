#include "configuration.h"

#include "modules/AppModule/AppMesh.h"
#include "FSCommon.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#include <pb_decode.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef FSCom

// --- Port name resolution ---

struct PortNameEntry {
    const char *name;
    int portnum;
};

static const PortNameEntry portNameTable[] = {
    {"text", meshtastic_PortNum_TEXT_MESSAGE_APP},
    {"position", meshtastic_PortNum_POSITION_APP},
    {"nodeinfo", meshtastic_PortNum_NODEINFO_APP},
    {"telemetry", meshtastic_PortNum_TELEMETRY_APP},
    {"neighborinfo", meshtastic_PortNum_NEIGHBORINFO_APP},
};

// Ports that apps must never observe
static const int blockedPorts[] = {
    meshtastic_PortNum_ROUTING_APP, // 5
    meshtastic_PortNum_ADMIN_APP,   // 6
};

int resolveReceivePortnum(const std::string &suffix)
{
    // Check friendly name table first
    for (const auto &entry : portNameTable) {
        if (suffix == entry.name)
            return entry.portnum;
    }

    // Fall back to numeric parse
    char *end = nullptr;
    long val = strtol(suffix.c_str(), &end, 10);
    if (end == suffix.c_str() || *end != '\0' || val < 0 || val > 65535)
        return -1;

    // Check blocked ports
    for (int blocked : blockedPorts) {
        if ((int)val == blocked)
            return -1;
    }

    return (int)val;
}

// --- JSON helpers ---

std::string jsonEscapeString(const char *s, size_t len)
{
    std::string out;
    out.reserve(len + 16);
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

// Compute hops from hop_start and hop_limit
static int computeHops(const meshtastic_MeshPacket &mp)
{
    if (mp.hop_start == 0 || mp.hop_start < mp.hop_limit)
        return 0;
    return mp.hop_start - mp.hop_limit;
}

// --- Serialization functions ---

static std::string serializeTextMessage(const meshtastic_MeshPacket &mp)
{
    const meshtastic_Data &d = mp.decoded;
    std::string escaped = jsonEscapeString((const char *)d.payload.bytes, d.payload.size);

    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"text\",\"from\":%u,\"to\":%u,\"channel\":%u,"
                     "\"text\":\"%s\",\"rx_time\":%u,\"rx_snr\":%.1f,\"rx_rssi\":%d,\"hops\":%d}",
                     (unsigned)mp.from, (unsigned)mp.to, (unsigned)mp.channel, escaped.c_str(),
                     (unsigned)mp.rx_time, (double)mp.rx_snr, (int)mp.rx_rssi, computeHops(mp));

    if (n < 0 || n >= (int)sizeof(buf))
        return "";
    return std::string(buf, n);
}

static std::string serializePosition(const meshtastic_MeshPacket &mp)
{
    meshtastic_Position pos = meshtastic_Position_init_default;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &pos))
        return "";

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"position\",\"from\":%u,\"latitude\":%d,\"longitude\":%d,"
                     "\"altitude\":%d,\"sats\":%u,\"rx_time\":%u,\"hops\":%d}",
                     (unsigned)mp.from, (int)pos.latitude_i, (int)pos.longitude_i, (int)pos.altitude,
                     (unsigned)pos.sats_in_view, (unsigned)mp.rx_time, computeHops(mp));

    if (n < 0 || n >= (int)sizeof(buf))
        return "";
    return std::string(buf, n);
}

static std::string serializeTelemetry(const meshtastic_MeshPacket &mp)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_default;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Telemetry_msg, &telemetry))
        return "";

    // Only serialize device metrics variant
    if (telemetry.which_variant != meshtastic_Telemetry_device_metrics_tag)
        return "";

    const meshtastic_DeviceMetrics &dm = telemetry.variant.device_metrics;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"telemetry\",\"from\":%u,\"battery_level\":%u,\"voltage\":%u,"
                     "\"channel_utilization\":%.1f,\"air_util_tx\":%.1f,\"rx_time\":%u,\"hops\":%d}",
                     (unsigned)mp.from, (unsigned)dm.battery_level, (unsigned)(dm.voltage * 1000),
                     (double)dm.channel_utilization, (double)dm.air_util_tx, (unsigned)mp.rx_time, computeHops(mp));

    if (n < 0 || n >= (int)sizeof(buf))
        return "";
    return std::string(buf, n);
}

static std::string serializeNodeInfo(const meshtastic_MeshPacket &mp)
{
    meshtastic_User user = meshtastic_User_init_default;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &user))
        return "";

    std::string longNameEsc = jsonEscapeString(user.long_name, strlen(user.long_name));
    std::string shortNameEsc = jsonEscapeString(user.short_name, strlen(user.short_name));

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"nodeinfo\",\"from\":%u,\"long_name\":\"%s\",\"short_name\":\"%s\","
                     "\"hw_model\":%d,\"rx_time\":%u,\"hops\":%d}",
                     (unsigned)mp.from, longNameEsc.c_str(), shortNameEsc.c_str(), (int)user.hw_model,
                     (unsigned)mp.rx_time, computeHops(mp));

    if (n < 0 || n >= (int)sizeof(buf))
        return "";
    return std::string(buf, n);
}

// --- Public dispatcher ---

std::string serializeMeshEvent(const meshtastic_MeshPacket &mp)
{
    switch (mp.decoded.portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        return serializeTextMessage(mp);
    case meshtastic_PortNum_POSITION_APP:
        return serializePosition(mp);
    case meshtastic_PortNum_TELEMETRY_APP:
        return serializeTelemetry(mp);
    case meshtastic_PortNum_NODEINFO_APP:
        return serializeNodeInfo(mp);
    default:
        return "";
    }
}

#endif // FSCom
