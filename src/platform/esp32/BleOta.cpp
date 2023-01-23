#include "BleOta.h"
#include "Arduino.h"
#include <esp_ota_ops.h>

static const String MESHTASTIC_OTA_APP_PROJECT_NAME("Meshtastic-OTA");

const esp_partition_t *BleOta::findEspOtaAppPartition()
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);

    esp_app_desc_t app_desc;
    esp_err_t ret = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc));

    if (ret != ESP_OK || MESHTASTIC_OTA_APP_PROJECT_NAME != app_desc.project_name) {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
        ret = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(part, &app_desc));
    }

    if (ret == ESP_OK && MESHTASTIC_OTA_APP_PROJECT_NAME == app_desc.project_name) {
        return part;
    } else {
        return nullptr;
    }
}

String BleOta::getOtaAppVersion()
{
    const esp_partition_t *part = findEspOtaAppPartition();
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