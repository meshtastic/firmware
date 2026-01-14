#include "MeshtasticOTA.h"
#include "configuration.h"
#include <Preferences.h>
#include <esp_ota_ops.h>

namespace MeshtasticOTA
{

static const char *nvsNamespace = "MeshtasticOTA";
static const char *appProjectName = "MeshtasticOTA";

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
    if (strcmp(app_desc->project_name, appProjectName) != 0) {
        LOG_INFO("app_desc->project_name == 0");
        return false;
    }
    return true;
}

bool trySwitchToOTA()
{
    const esp_partition_t *part = getAppPartition();
    esp_app_desc_t app_desc;
    if (!getAppDesc(part, &app_desc))
        return false;
    if (esp_ota_set_boot_partition(part) != ESP_OK)
        return false;
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
