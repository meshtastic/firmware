#include "WiFiOTA.h"
#include "configuration.h"
#include <Preferences.h>
#include <esp_ota_ops.h>

namespace WiFiOTA
{

static const char *nvsNamespace = "ota-wifi";
static const char *appProjectName = "OTA-WiFi";

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

void saveConfig(meshtastic_Config_NetworkConfig *network)
{
    LOG_INFO("Saving WiFi settings for upcoming OTA update");

    Preferences prefs;
    prefs.begin(nvsNamespace);
    prefs.putString("ssid", network->wifi_ssid);
    prefs.putString("psk", network->wifi_psk);
    prefs.putBool("updated", false);
    prefs.end();
}

bool trySwitchToOTA()
{
    esp_app_desc_t app_desc;
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (!part)
        return false;
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc)) != ESP_OK)
        return false;
    if (strcmp(app_desc.project_name, appProjectName) != 0)
        return false;
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_set_boot_partition(part)) != ESP_OK)
        return false;
    return true;
}

String getVersion()
{
    String version;
    esp_app_desc_t app_desc;
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (!part)
        return version;
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc)) != ESP_OK)
        return version;
    version = app_desc.version;
    return version;
}

} // namespace WiFiOTA
