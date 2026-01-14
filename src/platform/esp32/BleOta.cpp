#include "BleOta.h"
#include "Arduino.h"
#include <cctype>
#include <esp_ota_ops.h>
#include <string>

static bool isMeshtasticOtaProject(const esp_app_desc_t &desc)
{
    std::string name(desc.project_name);
    return name.find("Meshtastic") != std::string::npos && name.find("OTA") != std::string::npos;
}

const esp_partition_t *BleOta::findEspOtaAppPartition()
{
    esp_app_desc_t app_desc;
    esp_err_t ret = ESP_ERR_INVALID_ARG;

    // Try standard OTA slots first (app0 / app1)
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (part) {
        ret = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc));
    }

    if (!part || ret != ESP_OK || !isMeshtasticOtaProject(app_desc)) {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
        if (part) {
            ret = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc));
        }
    }

    // Fallback: look by partition label "app1" in case table uses custom labels
    if ((!part || ret != ESP_OK || !isMeshtasticOtaProject(app_desc))) {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "app1");
        if (part) {
            ret = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc));
        }
    }

    if (part && ret == ESP_OK && isMeshtasticOtaProject(app_desc)) {
        return part;
    }
    return nullptr;
}

String BleOta::getOtaAppVersion()
{
    const esp_partition_t *part = findEspOtaAppPartition();
    if (!part) {
        return String();
    }
    esp_app_desc_t app_desc;
    esp_err_t ret = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc));
    String version;
    if (ret == ESP_OK) {
        version = app_desc.version;
    }
    return version;
}

bool BleOta::switchToOtaApp()
{
    bool success = false;
    const esp_partition_t *part = findEspOtaAppPartition();
    if (part) {
        success = (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_set_boot_partition(part)) == ESP_OK);
    }
    return success;
}