#include "MeshtasticOTA.h"
#include "configuration.h"
#ifdef ESP_PLATFORM
#include <Preferences.h>
#include <esp_ota_ops.h>
#endif

namespace MeshtasticOTA
{

static const char *nvsNamespace = "MeshtasticOTA";
static const char *combinedAppProjectName = "MeshtasticOTA";
static const char *bleOnlyAppProjectName = "MeshtasticOTA-BLE";
static const char *wifiOnlyAppProjectName = "MeshtasticOTA-WiFi";

static bool updated = false;

bool isUpdated()
{
    return updated;
}

void initialize()
{
    Preferences prefs;
    prefs.begin(nvsNamespace);
    if (prefs.getBool("updated")) {
        LOG_INFO("First boot after OTA update");
        updated = true;
        prefs.putBool("updated", false);
    }
    prefs.end();
}

void recoverConfig(meshtastic_Config_NetworkConfig *network)
{
    LOG_INFO("Recovering WiFi settings after OTA update");

    Preferences prefs;
    prefs.begin(nvsNamespace, true);
    String ssid = prefs.getString("ssid");
    String psk = prefs.getString("psk");
    prefs.end();

    network->wifi_enabled = true;
    strncpy(network->wifi_ssid, ssid.c_str(), sizeof(network->wifi_ssid));
    strncpy(network->wifi_psk, psk.c_str(), sizeof(network->wifi_psk));
}

void saveConfig(meshtastic_Config_NetworkConfig *network, meshtastic_OTAMode method, uint8_t *ota_hash)
{
    LOG_INFO("Saving WiFi settings for upcoming OTA update");

    Preferences prefs;
    prefs.begin(nvsNamespace);
    prefs.putUChar("method", method);
    prefs.putBytes("ota_hash", ota_hash, 32);
    prefs.putString("ssid", network->wifi_ssid);
    prefs.putString("psk", network->wifi_psk);
    prefs.putBool("updated", false);
    prefs.end();
}

const esp_partition_t *getAppPartition()
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
}

bool getAppDesc(const esp_partition_t *part, esp_app_desc_t *app_desc)
{
    if (esp_ota_get_partition_description(part, app_desc) != ESP_OK) {
        LOG_INFO("esp_ota_get_partition_description failed");
        return false;
    }
    return true;
}

bool checkOTACapability(esp_app_desc_t *app_desc, uint8_t method)
{
    // Combined loader supports all (both) transports, BLE and WiFi
    if (strcmp(app_desc->project_name, combinedAppProjectName) == 0) {
        LOG_INFO("OTA partition contains combined BLE/WiFi OTA Loader");
        return true;
    }
    if (method == METHOD_OTA_BLE && strcmp(app_desc->project_name, bleOnlyAppProjectName) == 0) {
        LOG_INFO("OTA partition contains BLE-only OTA Loader");
        return true;
    }
    if (method == METHOD_OTA_WIFI && strcmp(app_desc->project_name, wifiOnlyAppProjectName) == 0) {
        LOG_INFO("OTA partition contains WiFi-only OTA Loader");
        return true;
    }
    LOG_INFO("OTA partition does not contain a known OTA loader");
    return false;
}

bool trySwitchToOTA()
{
    const esp_partition_t *part = getAppPartition();

    if (part == NULL) {
        LOG_WARN("Unable to get app partition in preparation of OTA reboot");
        return false;
    }

    uint8_t result = esp_ota_set_boot_partition(part);
    // Partition and app checks should now be done in the AdminModule before this is called
    if (result != ESP_OK) {
        LOG_WARN("Unable to switch to OTA partiton.  (Reason %d)", result);
        return false;
    }

    return true;
}

const char *getVersion()
{
    const esp_partition_t *part = getAppPartition();
    static esp_app_desc_t app_desc;
    if (!getAppDesc(part, &app_desc))
        return "";
    return app_desc.version;
}

} // namespace MeshtasticOTA
