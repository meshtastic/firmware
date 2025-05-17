#include "HostMetrics.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MeshService.h"
#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#include <filesystem>
#endif

int32_t HostMetricsModule::runOnce()
{
#if ARCH_PORTDUINO
    if (settingsMap[hostMetrics_interval] == 0) {
        return disable();
    } else {
        sendMetrics();
        return 60 * 1000 * settingsMap[hostMetrics_interval];
    }
#else
    return disable();
#endif
}

bool HostMetricsModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    // Don't worry about storing telemetry in NodeDB if we're a repeater
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        return false;

    if (t->which_variant == meshtastic_Telemetry_host_metrics_tag) {
#ifdef DEBUG_PORT
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received Host Metrics from %s): uptime=%u, diskfree=%lu, memory free=%lu, load=%04.2f, %04.2f, %04.2f", sender,
                 t->variant.host_metrics.uptime_seconds, t->variant.host_metrics.diskfree1_bytes,
                 t->variant.host_metrics.freemem_bytes, static_cast<float>(t->variant.host_metrics.load1) / 100,
                 static_cast<float>(t->variant.host_metrics.load5) / 100,
                 static_cast<float>(t->variant.host_metrics.load15) / 100);
#endif
    }
    return false; // Let others look at this message also if they want
}

/*
meshtastic_MeshPacket *HostMetricsModule::allocReply()
{
    if (currentRequest) {
        auto req = *currentRequest;
        const auto &p = req.decoded;
        meshtastic_Telemetry scratch;
        meshtastic_Telemetry *decoded = NULL;
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_HostMetrics_msg, &scratch)) {
            decoded = &scratch;
        } else {
            LOG_ERROR("Error decoding HostMetrics module!");
            return NULL;
        }
        // Check for a request for device metrics
        if (decoded->which_variant == meshtastic_Telemetry_host_metrics_tag) {
            LOG_INFO("Device telemetry reply to request");
            return allocDataProtobuf(getHostMetrics());
        }
    }
    return NULL;
}
    */

#if ARCH_PORTDUINO
meshtastic_Telemetry HostMetricsModule::getHostMetrics()
{
    std::string file_line;
    meshtastic_Telemetry t = meshtastic_HostMetrics_init_zero;
    t.which_variant = meshtastic_Telemetry_host_metrics_tag;

    if (access("/proc/uptime", R_OK) == 0) {
        std::ifstream proc_uptime("/proc/uptime");
        if (proc_uptime.is_open()) {
            std::getline(proc_uptime, file_line, ' ');
            proc_uptime.close();
            t.variant.host_metrics.uptime_seconds = stoul(file_line);
        }
    }

    std::filesystem::space_info root = std::filesystem::space("/");
    t.variant.host_metrics.diskfree1_bytes = root.available;

    if (access("/proc/meminfo", R_OK) == 0) {
        std::ifstream proc_meminfo("/proc/meminfo");
        if (proc_meminfo.is_open()) {
            do {
                std::getline(proc_meminfo, file_line);
            } while (file_line.find("MemAvailable") == std::string::npos);
            proc_meminfo.close();
            t.variant.host_metrics.freemem_bytes = stoull(file_line.substr(file_line.find_first_of("0123456789"))) * 1024;
        }
    }
    if (access("/proc/loadavg", R_OK) == 0) {
        std::ifstream proc_loadavg("/proc/loadavg");
        if (proc_loadavg.is_open()) {
            std::getline(proc_loadavg, file_line, ' ');
            t.variant.host_metrics.load1 = stof(file_line) * 100;
            std::getline(proc_loadavg, file_line, ' ');
            t.variant.host_metrics.load5 = stof(file_line) * 100;
            std::getline(proc_loadavg, file_line, ' ');
            t.variant.host_metrics.load15 = stof(file_line) * 100;
            proc_loadavg.close();
        }
    }

    return t;
}

bool HostMetricsModule::sendMetrics()
{
    meshtastic_Telemetry telemetry = getHostMetrics();
    LOG_INFO("Send: uptime=%u, diskfree=%lu, memory free=%lu, load=%04.2f, %04.2f, %04.2f",
             telemetry.variant.host_metrics.uptime_seconds, telemetry.variant.host_metrics.diskfree1_bytes,
             telemetry.variant.host_metrics.freemem_bytes, static_cast<float>(telemetry.variant.host_metrics.load1) / 100,
             static_cast<float>(telemetry.variant.host_metrics.load5) / 100,
             static_cast<float>(telemetry.variant.host_metrics.load15) / 100);

    meshtastic_MeshPacket *p = allocDataProtobuf(telemetry);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->channel = settingsMap[hostMetrics_channel];
    LOG_INFO("Send packet to mesh");
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    return true;
}
#endif