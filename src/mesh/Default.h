#pragma once
#include <NodeDB.h>
#include <cmath>
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
#define default_broadcast_interval_secs IF_ROUTER(ONE_DAY / 2, 60 * 60)
#define default_broadcast_smart_minimum_interval_secs 5 * 60
#define min_default_broadcast_interval_secs 60 * 60
#define min_default_broadcast_smart_minimum_interval_secs 5 * 60
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
    /**
     * Calculates a congestion scaling coefficient based on the number of online nodes.
     *
     * Uses power-law scaling (exponent 1.2) which provides a soft start that accelerates
     * as node count increases - matching the superlinear growth of flood routing traffic.
     *
     * Scaling starts at 20 nodes (simulator shows congestion problems emerging early).
     * Different modem presets have different channel capacities based on airtime per packet.
     *
     * Examples for LongFast (capacityMultiplier = 1.0):
     *   20 nodes: 1.0x, 50 nodes: ~3.0x, 100 nodes: ~6.9x, 200 nodes: ~15.8x
     * Examples for ShortFast (capacityMultiplier = 0.5):
     *   20 nodes: 1.0x, 50 nodes: ~2.0x, 100 nodes: ~4.0x, 200 nodes: ~8.4x
     */
    static float congestionScalingCoefficient(uint32_t numOnlineNodes)
    {
        // Start scaling at 20 nodes - meshes show congestion problems earlier than 40
        if (numOnlineNodes <= 20) {
            return 1.0f;
        }

        // Use power-law scaling (p=1.2) - soft start that accelerates with node count,
        // matching the superlinear growth of flood routing traffic
        float baseScale = powf(static_cast<float>(numOnlineNodes) / 20.0f, 1.2f);

        // Apply modem-specific capacity multiplier based on relative channel capacity.
        // Capacity is inversely proportional to airtime - faster modems can handle more
        // traffic before congestion, so we scale their intervals less aggressively.
        // Airtime values are for a typical 237-byte packet (max payload).
        float capacityMultiplier = 1.0f;
        if (config.lora.use_preset) {
            switch (config.lora.modem_preset) {
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
                capacityMultiplier = 0.3f; // ~28ms airtime, BW500 SF5
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
                capacityMultiplier = 0.5f; // ~50ms airtime, BW500 SF7
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
                capacityMultiplier = 0.7f; // ~100ms airtime, BW500 SF8
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
                capacityMultiplier = 0.7f; // ~100ms airtime, BW250 SF7
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
                capacityMultiplier = 0.85f; // ~200ms airtime, BW250 SF8
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
                capacityMultiplier = 0.85f; // ~150ms airtime, BW250 SF9
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
                capacityMultiplier = 1.0f; // ~300ms airtime, BW250 SF10 (baseline)
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
                capacityMultiplier = 1.0f; // ~350ms airtime, BW125 SF9
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
                capacityMultiplier = 1.3f; // ~700ms airtime, BW125 SF10
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
                capacityMultiplier = 1.3f; // ~1400ms airtime, BW62.5 SF11
                break;
            default:
                capacityMultiplier = 1.0f;
                break;
            }
        }

#if USERPREFS_EVENT_MODE
        // Event mode: more aggressive throttling for dense temporary meshes
        capacityMultiplier *= 1.5f;
#endif

        return 1.0f + (baseScale - 1.0f) * capacityMultiplier;
    }
};
