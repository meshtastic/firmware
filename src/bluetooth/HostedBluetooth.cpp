#include "bluetooth/HostedBluetooth.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_ESP_HOSTED_ENABLED) && !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "esp_err.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_event.h"
#include "esp_hosted_misc.h"
#include "esp_log.h"

namespace
{
constexpr const char *kTag = "HostedBluetooth";

// Custom message IDs exchanged with co-processor firmware.
constexpr uint32_t kMsgLogRecord = 0x4D53484C;   // "MSHL"
constexpr uint32_t kMsgClearBonds = 0x4D534842;  // "MSHB"
constexpr uint32_t kMsgRssiRequest = 0x4D534852; // "MSHR"
constexpr uint32_t kMsgRssiUpdate = 0x4D535252;  // "MSRR"

HostedBluetooth *sHostedInstance = nullptr;

void hostedCustomDataCallback(uint32_t msg_id, const uint8_t *data, size_t data_len)
{
    if (!sHostedInstance || !data) {
        return;
    }

    if (msg_id == kMsgRssiUpdate && data_len >= sizeof(int32_t)) {
        int32_t rssi = 0;
        memcpy(&rssi, data, sizeof(int32_t));
        sHostedInstance->setRssi(static_cast<int>(rssi));
    }
}

void hostedEventHandler(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData)
{
    (void)eventData;

    auto *self = static_cast<HostedBluetooth *>(arg);
    if (!self || eventBase != ESP_HOSTED_EVENT) {
        return;
    }

    switch (eventId) {
    case ESP_HOSTED_EVENT_TRANSPORT_UP:
        ESP_LOGI(kTag, "ESP-Hosted transport is up");
        self->setConnected(true);
        break;
    case ESP_HOSTED_EVENT_TRANSPORT_DOWN:
        ESP_LOGW(kTag, "ESP-Hosted transport is down");
    case ESP_HOSTED_EVENT_TRANSPORT_FAILURE:
        if (eventId == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
            ESP_LOGE(kTag, "ESP-Hosted transport failure");
        }
        self->setConnected(false);
        break;
    case ESP_HOSTED_EVENT_CP_INIT:
    case ESP_HOSTED_EVENT_CP_HEARTBEAT:
    default:
        break;
    }
}
} // namespace

HostedBluetooth::HostedBluetooth() {}

HostedBluetooth::~HostedBluetooth()
{
    deinit();
}

bool HostedBluetooth::registerCallbacks()
{
    if (callbacksRegistered) {
        return true;
    }

    // Defensive cleanup in case setup() is called again after an incomplete/early previous init.
    // esp_event_handler_unregister can safely fail if the handler wasn't present.
    esp_event_handler_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, hostedEventHandler);

    esp_err_t err = esp_event_handler_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, hostedEventHandler, this);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "Failed to register hosted event handler: %s", esp_err_to_name(err));
        return false;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "Hosted event handler already registered, continuing");
    }

    err = esp_hosted_register_custom_callback(kMsgRssiUpdate, hostedCustomDataCallback);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "Failed to register RSSI callback: %s", esp_err_to_name(err));
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "RSSI callback already registered, continuing");
    }

    sHostedInstance = this;
    callbacksRegistered = true;
    return true;
}

void HostedBluetooth::unregisterCallbacks()
{
    if (!callbacksRegistered) {
        return;
    }

    esp_hosted_register_custom_callback(kMsgRssiUpdate, nullptr);
    esp_event_handler_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, hostedEventHandler);

    if (sHostedInstance == this) {
        sHostedInstance = nullptr;
    }
    callbacksRegistered = false;
}

void HostedBluetooth::setup()
{
    if (active) {
        return;
    }

    if (!registerCallbacks()) {
        return;
    }

    int rc = esp_hosted_init();
    if (rc != ESP_OK) {
        ESP_LOGE(kTag, "esp_hosted_init failed: %d", rc);
        unregisterCallbacks();
        return;
    }

    rc = esp_hosted_connect_to_slave();
    if (rc != ESP_OK) {
        ESP_LOGE(kTag, "esp_hosted_connect_to_slave failed: %d", rc);
        esp_hosted_deinit();
        unregisterCallbacks();
        return;
    }

    rc = esp_hosted_bt_controller_init();
    if (rc != ESP_OK) {
        ESP_LOGE(kTag, "esp_hosted_bt_controller_init failed: %d", rc);
        esp_hosted_deinit();
        unregisterCallbacks();
        return;
    }

    rc = esp_hosted_bt_controller_enable();
    if (rc != ESP_OK) {
        ESP_LOGE(kTag, "esp_hosted_bt_controller_enable failed: %d", rc);
        esp_hosted_bt_controller_deinit(false);
        esp_hosted_deinit();
        unregisterCallbacks();
        return;
    }

    active = true;
    firstRssiLogged.store(false);
    ESP_LOGI(kTag, "ESP-Hosted Bluetooth ready");
}

void HostedBluetooth::shutdown()
{
    deinit();
}

void HostedBluetooth::deinit()
{
    if (!active && !callbacksRegistered) {
        return;
    }

    esp_err_t err = esp_hosted_bt_controller_disable();
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_hosted_bt_controller_disable returned %s", esp_err_to_name(err));
    }

    err = esp_hosted_bt_controller_deinit(false);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_hosted_bt_controller_deinit returned %s", esp_err_to_name(err));
    }

    int rc = esp_hosted_deinit();
    if (rc != ESP_OK) {
        ESP_LOGW(kTag, "esp_hosted_deinit returned %d", rc);
    }

    unregisterCallbacks();

    connected.store(false);
    active = false;
    rssi.store(0);
    firstRssiLogged.store(false);
}

void HostedBluetooth::clearBonds()
{
    if (!active) {
        return;
    }

    const esp_err_t err = esp_hosted_send_custom_data(kMsgClearBonds, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to request hosted bond clear: %s", esp_err_to_name(err));
    }
}

bool HostedBluetooth::isActive()
{
    return active;
}

bool HostedBluetooth::isConnected()
{
    return connected.load();
}

int HostedBluetooth::getRssi()
{
    if (active) {
        const esp_err_t err = esp_hosted_send_custom_data(kMsgRssiRequest, nullptr, 0);
        if (err != ESP_OK) {
            ESP_LOGD(kTag, "Hosted RSSI request failed: %s", esp_err_to_name(err));
        }
    }
    return rssi.load();
}

void HostedBluetooth::sendLog(const uint8_t *logMessage, size_t length)
{
    if (!active || !isConnected() || !logMessage || length == 0) {
        return;
    }

    const esp_err_t err = esp_hosted_send_custom_data(kMsgLogRecord, logMessage, length);
    if (err != ESP_OK) {
        ESP_LOGD(kTag, "Hosted BLE log send failed: %s", esp_err_to_name(err));
    }
}

void HostedBluetooth::setConnected(bool value)
{
    connected.store(value);
}

void HostedBluetooth::setRssi(int value)
{
    rssi.store(value);
    maybeLogFirstRssi(value);
}

void HostedBluetooth::maybeLogFirstRssi(int value)
{
    bool expected = false;
    if (firstRssiLogged.compare_exchange_strong(expected, true)) {
        ESP_LOGI("HostedBluetooth", "ESP-Hosted first RSSI update: %d dBm", value);
    }
}

#else

HostedBluetooth::HostedBluetooth() {}

HostedBluetooth::~HostedBluetooth()
{
    deinit();
}

bool HostedBluetooth::registerCallbacks()
{
    return false;
}

void HostedBluetooth::unregisterCallbacks() {}

void HostedBluetooth::setup()
{
    active = false;
    connected.store(false);
    rssi.store(0);
    firstRssiLogged.store(false);
}

void HostedBluetooth::shutdown()
{
    deinit();
}

void HostedBluetooth::deinit()
{
    connected.store(false);
    active = false;
    rssi.store(0);
    firstRssiLogged.store(false);
}

void HostedBluetooth::clearBonds() {}

bool HostedBluetooth::isActive()
{
    return active;
}

bool HostedBluetooth::isConnected()
{
    return connected.load();
}

int HostedBluetooth::getRssi()
{
    return rssi.load();
}

void HostedBluetooth::sendLog(const uint8_t *logMessage, size_t length)
{
    (void)logMessage;
    (void)length;
}

void HostedBluetooth::setConnected(bool value)
{
    connected.store(value);
}

void HostedBluetooth::setRssi(int value)
{
    rssi.store(value);
    maybeLogFirstRssi(value);
}

void HostedBluetooth::maybeLogFirstRssi(int value)
{
    (void)value;
}

#endif
