#pragma once
#include <NodeDB.h>
#include <cstdint>
#include <meshUtils.h>
#define ONE_DAY 24 * 60 * 60
#define ONE_MINUTE_MS 60 * 1000
#define THIRTY_SECONDS_MS 30 * 1000
#define TWO_SECONDS_MS 2 * 1000
#define FIVE_SECONDS_MS 5 * 1000
#define TEN_SECONDS_MS 10 * 1000
#define MAX_INTERVAL INT32_MAX // FIXME: INT32_MAX to avoid overflow issues with Apple clients but should be UINT32_MAX

#define min_default_telemetry_interval_secs 30 * 60
#define default_gps_update_interval IF_ROUTER(ONE_DAY, 2 * 60)
#define default_telemetry_broadcast_interval_secs IF_ROUTER(ONE_DAY / 2, 60 * 60)
#define default_broadcast_interval_secs IF_ROUTER(ONE_DAY / 2, 15 * 60)
#define default_wait_bluetooth_secs IF_ROUTER(1, 60)
#define default_sds_secs IF_ROUTER(ONE_DAY, UINT32_MAX) // Default to forever super deep sleep
#define default_ls_secs IF_ROUTER(ONE_DAY, 5 * 60)
#define default_min_wake_secs 10
#define default_screen_on_secs IF_ROUTER(1, 60 * 10)
#define default_node_info_broadcast_secs 3 * 60 * 60
#define default_neighbor_info_broadcast_secs 6 * 60 * 60
#define min_node_info_broadcast_secs 60 * 60 // No regular broadcasts of more than once an hour
#define min_neighbor_info_broadcast_secs 4 * 60 * 60
#define default_map_publish_interval_secs 60 * 60
#ifdef USERPREFS_RINGTONE_NAG_SECS
#define default_ringtone_nag_secs USERPREFS_RINGTONE_NAG_SECS
#else
#define default_ringtone_nag_secs 15
#endif
#define default_network_ipv6_enabled false

#define default_mqtt_address "mqtt.meshtastic.org"
#define default_mqtt_username "meshdev"
#define default_mqtt_password "large4cats"
#define default_mqtt_root "msh"
#define default_mqtt_encryption_enabled true
#define default_mqtt_tls_enabled false

#define IF_ROUTER(routerVal, normalVal)                                                                                          \
    ((config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER) ? (routerVal) : (normalVal))

class Default
{
  public:
    static uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval);
    static uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval);
    static uint32_t getConfiguredOrDefault(uint32_t configured, uint32_t defaultValue);
    // Note: numOnlineNodes uses uint32_t to match the public API and allow flexibility,
    // even though internal node counts use uint16_t (max 65535 nodes)
    static uint32_t getConfiguredOrDefaultMsScaled(uint32_t configured, uint32_t defaultValue, uint32_t numOnlineNodes);
    static uint8_t getConfiguredOrDefaultHopLimit(uint8_t configured);
    static uint32_t getConfiguredOrMinimumValue(uint32_t configured, uint32_t minValue);

  private:
    // Note: Kept as uint32_t to match the public API parameter type
    static float congestionScalingCoefficient(uint32_t numOnlineNodes)
    {
        // Increase frequency of broadcasts for small networks regardless of preset
        if (numOnlineNodes <= 10) {
            return 0.6;
        } else if (numOnlineNodes <= 20) {
            return 0.7;
        } else if (numOnlineNodes <= 30) {
            return 0.8;
        } else if (numOnlineNodes <= 40) {
            return 1.0;
        } else {
            float throttlingFactor = 0.075;
            if (config.lora.use_preset && config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW)
                throttlingFactor = 0.04;
            else if (config.lora.use_preset && config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST)
                throttlingFactor = 0.02;
            else if (config.lora.use_preset &&
                     IS_ONE_OF(config.lora.modem_preset, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
                               meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
                               meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW))
                throttlingFactor = 0.01;

#if USERPREFS_EVENT_MODE
            // If we are in event mode, scale down the throttling factor
            throttlingFactor = 0.04;
#endif

            // Scaling up traffic based on number of nodes over 40
            int nodesOverForty = (numOnlineNodes - 40);
            return 1.0 + (nodesOverForty * throttlingFactor); // Each number of online node scales by 0.075 (default)
        }
    }
};
