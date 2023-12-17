#include "configuration.h"
#if defined(ARCH_ESP32)
#include "MeshService.h"
#include "PaxcounterModule.h"

#include <assert.h>

PaxcounterModule *paxcounterModule;

void NullFunc(){};

// paxcounterModule->sendInfo(NODENUM_BROADCAST);

PaxcounterModule::PaxcounterModule()
    : concurrency::OSThread("PaxcounterModule"),
      ProtobufModule("paxcounter", meshtastic_PortNum_PAXCOUNTER_APP, &meshtastic_Paxcount_msg)
{
}

bool PaxcounterModule::sendInfo(NodeNum dest)
{
    libpax_counter_count(&count_from_libpax);
    LOG_INFO("(Sending): pax: wifi=%d; ble=%d; uptime=%d\n", count_from_libpax.wifi_count, count_from_libpax.ble_count,
             millis() / 1000);

    meshtastic_Paxcount pl = meshtastic_Paxcount_init_default;
    pl.wifi = count_from_libpax.wifi_count;
    pl.ble = count_from_libpax.ble_count;
    pl.uptime = millis() / 1000;

    meshtastic_MeshPacket *p = allocDataProtobuf(pl);
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_MIN;

    service.sendToMesh(p, RX_SRC_LOCAL, true);
    return true;
}

bool PaxcounterModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Paxcount *p)
{
    return false; // Let others look at this message also if they want. We don't do anything with received packets.
}

meshtastic_MeshPacket *PaxcounterModule::allocReply()
{
    if (ignoreRequest) {
        return NULL;
    }

    meshtastic_Paxcount pl = meshtastic_Paxcount_init_default;
    pl.wifi = count_from_libpax.wifi_count;
    pl.ble = count_from_libpax.ble_count;
    pl.uptime = millis() / 1000;
    return allocDataProtobuf(pl);
}

int32_t PaxcounterModule::runOnce()
{
    if (moduleConfig.paxcounter.enabled && !config.bluetooth.enabled && !config.network.wifi_enabled) {
        if (firstTime) {
            firstTime = false;
            LOG_DEBUG(
                "Paxcounter starting up with interval of %d seconds\n",
                getConfiguredOrDefault(moduleConfig.paxcounter.paxcounter_update_interval, default_broadcast_interval_secs));
            struct libpax_config_t configuration;
            libpax_default_config(&configuration);

            configuration.blecounter = 1;
            configuration.blescantime = 0; // infinit
            configuration.wificounter = 1;
            configuration.wifi_channel_map = WIFI_CHANNEL_ALL;
            configuration.wifi_channel_switch_interval = 50;
            configuration.wifi_rssi_threshold = -80;
            configuration.ble_rssi_threshold = -80;
            libpax_update_config(&configuration);

            // internal processing initialization
            libpax_counter_init(NullFunc, &count_from_libpax, UINT16_MAX, 1);
            libpax_counter_start();
        } else {
            sendInfo(NODENUM_BROADCAST);
        }
        return getConfiguredOrDefaultMs(moduleConfig.paxcounter.paxcounter_update_interval, default_broadcast_interval_secs);
    } else {
        return disable();
    }
}

#endif