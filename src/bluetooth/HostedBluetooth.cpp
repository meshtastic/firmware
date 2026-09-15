#include "configuration.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_ESP_HOSTED_ENABLED) && !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "BluetoothStatus.h"
#include "PowerFSM.h"
#include "bluetooth/HostedBluetooth.h"
#include "concurrency/OSThread.h"
#include "esp32-hal-hosted.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_event.h"
#include "main.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <array>
#include <atomic>
#include <climits>
#include <cstring>
#include <driver/gpio.h>
#include <mutex>

#include "host/ble_gap.h"
#include "host/ble_store.h"

namespace
{
/*
 * Maintainer note: HostedBluetooth intentionally stays close to NimbleBluetooth
 * but is not a strict drop-in equivalent.
 *
 * Intentional differences from NimbleBluetooth include:
 * - ESP-Hosted transport lifecycle handling (event callbacks + CP reset GPIO control).
 * - Data-length update behavior (Hosted currently requests ble_gap_set_data_len on connect).
 * - No Battery Service exposure here.
 *
 * If you modify common BLE flow in one class, review and likely mirror in both:
 * - PhoneAPI queueing/synchronization between BLE callbacks and runOnce().
 * - Security/pairing config and passkey UX/status updates.
 * - Mesh GATT characteristics, permissions, and advertising setup.
 * - Connection parameter tuning and reconnect/disconnect cleanup paths.
 */
constexpr uint16_t kPreferredBleMtu = 517;
constexpr uint16_t kPreferredBleTxOctets = 251;
constexpr uint16_t kPreferredBleTxTimeUs = (kPreferredBleTxOctets + 14) * 8;
constexpr size_t kBluetoothToPhoneQueueSize = 3;
constexpr size_t kBluetoothFromPhoneQueueSize = 3;

BLECharacteristic *fromNumCharacteristic = nullptr;
BLECharacteristic *logRadioCharacteristic = nullptr;
BLEServer *bleServer = nullptr;

static bool passkeyShowing = false;
std::atomic<uint16_t> hostedConnHandle{BLE_HS_CONN_HANDLE_NONE};

void clearPairingDisplay()
{
    if (!passkeyShowing) {
        return;
    }

    passkeyShowing = false;
#if HAS_SCREEN
    if (screen) {
        screen->endAlert();
    }
#endif
}

class HostedBluetoothPhoneAPI : public PhoneAPI, public concurrency::OSThread
{
  public:
    HostedBluetoothPhoneAPI() : concurrency::OSThread("HostedBluetooth") { api_type = TYPE_BLE; }

    std::mutex fromPhoneMutex;
    std::atomic<size_t> fromPhoneQueueSize{0};
    std::array<BLEValue, kBluetoothFromPhoneQueueSize> fromPhoneQueue{};

    std::mutex toPhoneMutex;
    std::atomic<size_t> toPhoneQueueSize{0};
    std::array<std::array<uint8_t, meshtastic_FromRadio_size>, kBluetoothToPhoneQueueSize> toPhoneQueue{};
    std::array<size_t, kBluetoothToPhoneQueueSize> toPhoneQueueByteSizes{};
    std::atomic<bool> onReadCallbackIsWaitingForData{false};

  protected:
    int32_t runOnce() override
    {
        while (fromPhoneQueueSize > 0 || onReadCallbackIsWaitingForData) {
            runOnceHandleFromPhoneQueue();
            runOnceHandleToPhoneQueue();
        }
        return INT32_MAX;
    }

    void onNowHasData(uint32_t fromRadioNum) override
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        if (!fromNumCharacteristic || !bleServer || bleServer->getConnectedCount() == 0) {
            return;
        }

        uint8_t val[4];
        put_le32(val, fromRadioNum);
        fromNumCharacteristic->setValue(val, sizeof(val));
        fromNumCharacteristic->notify();
    }

    bool checkIsConnected() override { return bleServer && bleServer->getConnectedCount() > 0; }

  private:
    void runOnceHandleToPhoneQueue()
    {
        if (!onReadCallbackIsWaitingForData) {
            return;
        }

        uint8_t fromRadioBytes[meshtastic_FromRadio_size] = {0};
        size_t numBytes = getFromRadio(fromRadioBytes);

        if (numBytes > 0 && toPhoneQueueSize < kBluetoothToPhoneQueueSize) {
            std::lock_guard<std::mutex> guard(toPhoneMutex);
            const size_t storeAtIndex = toPhoneQueueSize.load();
            memcpy(toPhoneQueue[storeAtIndex].data(), fromRadioBytes, numBytes);
            toPhoneQueueByteSizes[storeAtIndex] = numBytes;
            toPhoneQueueSize++;
        }

        onReadCallbackIsWaitingForData = false;
    }

    void runOnceHandleFromPhoneQueue()
    {
        if (fromPhoneQueueSize == 0) {
            return;
        }

        BLEValue val;
        {
            std::lock_guard<std::mutex> guard(fromPhoneMutex);
            val = fromPhoneQueue[0];
            for (size_t i = 1; i < fromPhoneQueueSize; ++i) {
                fromPhoneQueue[i - 1] = fromPhoneQueue[i];
            }
            fromPhoneQueueSize--;
        }

        handleToRadio(val.getData(), val.getLength());
    }
};

static HostedBluetoothPhoneAPI *bluetoothPhoneAPI = nullptr;
uint8_t lastToRadio[MAX_TO_FROM_RADIO_SIZE] = {0};

class HostedBluetoothToRadioCallback : public BLECharacteristicCallbacks
{
  public:
    void onWrite(BLECharacteristic *pCharacteristic) override
    {
        if (!bluetoothPhoneAPI) {
            return;
        }

        BLEValue val;
        val.setValue(pCharacteristic->getData(), pCharacteristic->getLength());

        if (memcmp(lastToRadio, val.getData(), val.getLength()) == 0) {
            return;
        }

        if (bluetoothPhoneAPI->fromPhoneQueueSize >= kBluetoothFromPhoneQueueSize) {
            LOG_WARN("Hosted BLE onWrite drop: queue full (%u bytes)", val.getLength());
            return;
        }

        memcpy(lastToRadio, val.getData(), val.getLength());

        {
            std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->fromPhoneMutex);
            bluetoothPhoneAPI->fromPhoneQueue.at(bluetoothPhoneAPI->fromPhoneQueueSize) = val;
            bluetoothPhoneAPI->fromPhoneQueueSize++;
        }

        bluetoothPhoneAPI->setIntervalFromNow(0);
        concurrency::mainDelay.interrupt();
    }
};

class HostedBluetoothFromRadioCallback : public BLECharacteristicCallbacks
{
  public:
    void onRead(BLECharacteristic *pCharacteristic) override
    {
        if (!bluetoothPhoneAPI) {
            return;
        }

        if (bluetoothPhoneAPI->toPhoneQueueSize == 0) {
            bluetoothPhoneAPI->onReadCallbackIsWaitingForData = true;
            bluetoothPhoneAPI->setIntervalFromNow(0);
            concurrency::mainDelay.interrupt();

            int tries = 0;
            while (bluetoothPhoneAPI->onReadCallbackIsWaitingForData && tries < 4000) {
                delay(tries < 20 ? 1 : 5);
                tries++;
                if (tries == 4000) {
                    LOG_WARN("BLE onRead: timeout waiting for data after %d tries, giving up and returning 0-size response",
                             tries);
                }
            }
        }

        uint8_t fromRadioBytes[meshtastic_FromRadio_size] = {0};
        size_t numBytes = 0;

        {
            std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->toPhoneMutex);
            size_t pending = bluetoothPhoneAPI->toPhoneQueueSize.load();
            if (pending > 0) {
                numBytes = bluetoothPhoneAPI->toPhoneQueueByteSizes[0];
                memcpy(fromRadioBytes, bluetoothPhoneAPI->toPhoneQueue[0].data(), numBytes);

                for (size_t i = 1; i < pending; ++i) {
                    memcpy(bluetoothPhoneAPI->toPhoneQueue[i - 1].data(), bluetoothPhoneAPI->toPhoneQueue[i].data(),
                           bluetoothPhoneAPI->toPhoneQueueByteSizes[i]);
                    bluetoothPhoneAPI->toPhoneQueueByteSizes[i - 1] = bluetoothPhoneAPI->toPhoneQueueByteSizes[i];
                }

                bluetoothPhoneAPI->toPhoneQueueSize--;
            }
        }

        pCharacteristic->setValue(fromRadioBytes, numBytes);
    }
};

class HostedBluetoothServerCallback : public BLEServerCallbacks
{
  public:
    explicit HostedBluetoothServerCallback(HostedBluetooth *owner) : owner(owner) {}

  private:
    HostedBluetooth *owner;

    void onConnect(BLEServer *pServer, struct ble_gap_conn_desc *desc) override
    {
        BLEAddress peerAddr(desc->peer_id_addr);
        LOG_INFO("Hosted BLE incoming connection %s", peerAddr.toString().c_str());
        owner->setConnected(true);
        hostedConnHandle = desc->conn_handle;

        const int dataLenResult = ble_gap_set_data_len(desc->conn_handle, kPreferredBleTxOctets, kPreferredBleTxTimeUs);
        if (dataLenResult != 0) {
            LOG_WARN("Hosted BLE failed to raise data length rc=%d", dataLenResult);
        }

        pServer->updateConnParams(desc->conn_handle, 6, 12, 0, 200);
    }

    void onDisconnect(BLEServer *pServer, struct ble_gap_conn_desc *desc) override
    {
        (void)pServer;
        (void)desc;
        LOG_INFO("Hosted BLE disconnected");
        owner->setConnected(false);
        meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
        bluetoothStatus->updateStatus(&newStatus);
        clearPairingDisplay();
        hostedConnHandle = BLE_HS_CONN_HANDLE_NONE;
        memset(lastToRadio, 0, sizeof(lastToRadio));

        if (bluetoothPhoneAPI) {
            bluetoothPhoneAPI->close();
            {
                std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->fromPhoneMutex);
                bluetoothPhoneAPI->fromPhoneQueueSize = 0;
            }
            {
                std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->toPhoneMutex);
                bluetoothPhoneAPI->toPhoneQueueSize = 0;
            }
            bluetoothPhoneAPI->onReadCallbackIsWaitingForData = false;
        }

        owner->startAdvertising();
    }
};

class HostedBluetoothSecurityCallback : public BLESecurityCallbacks
{
  public:
    void onPassKeyNotify(uint32_t passkey) override
    {
        LOG_INFO("*** Enter passkey %06u on the peer side ***", passkey);
        powerFSM.trigger(EVENT_BLUETOOTH_PAIR);

        meshtastic::BluetoothStatus newStatus(std::to_string(passkey));
        bluetoothStatus->updateStatus(&newStatus);

#if HAS_SCREEN
        if (screen) {
            screen->startAlert([passkey](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) -> void {
                char btPIN[16] = "888888";
                snprintf(btPIN, sizeof(btPIN), "%06u", passkey);
                int x_offset = display->width() / 2;
                int y_offset = display->height() <= 80 ? 0 : 12;
                display->setTextAlignment(TEXT_ALIGN_CENTER);
                display->setFont(FONT_MEDIUM);
                display->drawString(x_offset + x, y_offset + y, "Bluetooth");
#if !defined(M5STACK_UNITC6L)
                display->setFont(FONT_SMALL);
                y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_MEDIUM - 4 : y_offset + FONT_HEIGHT_MEDIUM + 5;
                display->drawString(x_offset + x, y_offset + y, "Enter this code");
#endif
                display->setFont(FONT_LARGE);
                char pin[8];
                snprintf(pin, sizeof(pin), "%.3s %.3s", btPIN, btPIN + 3);
                y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_SMALL - 5 : y_offset + FONT_HEIGHT_SMALL + 5;
                display->drawString(x_offset + x, y_offset + y, pin);

                display->setFont(FONT_SMALL);
                char deviceName[64];
                snprintf(deviceName, sizeof(deviceName), "Name: %s", getDeviceName());
                y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_LARGE - 6 : y_offset + FONT_HEIGHT_LARGE + 5;
                display->drawString(x_offset + x, y_offset + y, deviceName);
            });
        }
#endif

        passkeyShowing = true;
    }

    void onAuthenticationComplete(ble_gap_conn_desc *desc) override
    {
        (void)desc;
        LOG_INFO("Hosted BLE authentication complete");

        meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
        bluetoothStatus->updateStatus(&newStatus);
        clearPairingDisplay();
    }
};

HostedBluetoothToRadioCallback *toRadioCallbacks = nullptr;
HostedBluetoothFromRadioCallback *fromRadioCallbacks = nullptr;
HostedBluetoothSecurityCallback *securityCallbacks = nullptr;

gpio_num_t getSlaveResetGpio()
{
#if defined(CONFIG_ESP_HOSTED_GPIO_SLAVE_RESET_SLAVE)
    return static_cast<gpio_num_t>(CONFIG_ESP_HOSTED_GPIO_SLAVE_RESET_SLAVE);
#elif defined(CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE)
    return static_cast<gpio_num_t>(CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE);
#else
    return GPIO_NUM_NC;
#endif
}

void setSlaveResetLine(bool assertReset)
{
    const gpio_num_t gpioNum = getSlaveResetGpio();
    if (gpioNum == GPIO_NUM_NC || gpioNum < 0) {
        return;
    }

    if (gpio_reset_pin(gpioNum) != ESP_OK) {
        return;
    }
    if (gpio_set_direction(gpioNum, GPIO_MODE_OUTPUT) != ESP_OK) {
        return;
    }

    const int activeLevel =
#if defined(CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH)
        1;
#else
        0;
#endif
    const int inactiveLevel = activeLevel ? 0 : 1;

    gpio_set_level(gpioNum, assertReset ? activeLevel : inactiveLevel);
    LOG_DEBUG("[HostedBluetooth] setSlaveResetLine: GPIO[%d] -> %d (assertReset=%d, activeLevel=%d)", gpioNum,
              assertReset ? activeLevel : inactiveLevel, assertReset, activeLevel);
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
        LOG_INFO("ESP-Hosted transport is up");
        self->setConnected(true);
        break;
    case ESP_HOSTED_EVENT_TRANSPORT_DOWN:
        LOG_WARN("ESP-Hosted transport is down");
        [[fallthrough]];
    case ESP_HOSTED_EVENT_TRANSPORT_FAILURE:
        if (eventId == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
            LOG_ERROR("ESP-Hosted transport failure");
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
        LOG_ERROR("Failed to register hosted event handler: %s", esp_err_to_name(err));
        return false;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        LOG_WARN("Hosted event handler already registered, continuing");
    }

    callbacksRegistered = true;
    return true;
}

void HostedBluetooth::unregisterCallbacks()
{
    if (!callbacksRegistered) {
        return;
    }

    esp_event_handler_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, hostedEventHandler);

    callbacksRegistered = false;
}

void HostedBluetooth::setup()
{
    if (active) {
        return;
    }

    // Ensure co-processor is released from reset before bringing transport back up.
    setSlaveResetLine(false);
    LOG_DEBUG("[HostedBluetooth] setup(): Released co-processor from reset");

    if (!registerCallbacks()) {
        return;
    }

    active = setupGatt();
    firstRssiLogged.store(false);
    if (active) {
        LOG_INFO("ESP-Hosted Bluetooth ready");
    } else {
        LOG_ERROR("Hosted BLE setup failed in hosted mode");
        deinit();
    }
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

    shutdownGatt();
    if (BLEDevice::getInitialized()) {
        BLEDevice::deinit(false);
    } else {
        hostedDeinitBLE();
    }

    // Hold co-processor in reset when hosted transport is disabled to reduce idle draw.
    setSlaveResetLine(true);

    unregisterCallbacks();

    connected.store(false);
    active = false;
    rssi.store(0);
    firstRssiLogged.store(false);
}

void HostedBluetooth::clearBonds()
{
    ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_SEC, nullptr);
    ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_CCCD, nullptr);
}

bool HostedBluetooth::isActive()
{
    return active;
}

bool HostedBluetooth::isConnected()
{
    if (!connected.load()) {
        return false;
    }
    return bleServer && bleServer->getConnectedCount() > 0;
}

int HostedBluetooth::getRssi()
{
    if (!bleServer || !isConnected()) {
        return 0;
    }

    uint16_t connHandle = hostedConnHandle.load();
    if (connHandle == BLE_HS_CONN_HANDLE_NONE) {
        const auto peers = bleServer->getPeerDevices(true);
        if (!peers.empty()) {
            connHandle = peers.begin()->first;
            hostedConnHandle = connHandle;
        }
    }
    if (connHandle == BLE_HS_CONN_HANDLE_NONE) {
        return 0;
    }

    int8_t currentRssi = 0;
    const int rc = ble_gap_conn_rssi(connHandle, &currentRssi);
    if (rc == 0) {
        setRssi(currentRssi);
        return currentRssi;
    }

    return rssi.load();
}

void HostedBluetooth::sendLog(const uint8_t *logMessage, size_t length)
{
    if (!logRadioCharacteristic || !isConnected() || length > 512) {
        return;
    }

    logRadioCharacteristic->setValue(logMessage, length);
    logRadioCharacteristic->notify();
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
        LOG_INFO("ESP-Hosted first RSSI update: %d dBm", value);
    }
}

bool HostedBluetooth::setupGatt()
{
    memset(lastToRadio, 0, sizeof(lastToRadio));
    hostedConnHandle = BLE_HS_CONN_HANDLE_NONE;

    if (!BLEDevice::init(getDeviceName())) {
        LOG_ERROR("Hosted BLE init failed");
        return false;
    }
#ifdef ESP_PWR_LVL_P9
    BLEDevice::setPower(ESP_PWR_LVL_P9);
#endif

    BLESecurity *security = new BLESecurity();
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    if (config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        security->setCapability(ESP_IO_CAP_OUT);
        if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
            LOG_INFO("Hosted BLE using random passkey");
            security->setPassKey(false);
        } else {
            LOG_INFO("Hosted BLE using fixed passkey");
            security->setPassKey(true, config.bluetooth.fixed_pin);
        }
        // Enable authorization requirements:
        // - bonding: true (for persistent storage of the keys)
        // - MITM: true (enables Man-In-The-Middle protection for password prompts)
        // - secure connection: true (enables secure connection for encryption)
        security->setAuthenticationMode(true, true, true);
    } else {
        security->setCapability(ESP_IO_CAP_NONE);
        security->setAuthenticationMode(true, false, false);
    }

    if (!securityCallbacks) {
        securityCallbacks = new HostedBluetoothSecurityCallback();
    }
    BLEDevice::setSecurityCallbacks(securityCallbacks);

    const int mtuResult = BLEDevice::setMTU(kPreferredBleMtu);
    if (mtuResult == 0) {
        LOG_INFO("Hosted BLE MTU request set to %u", kPreferredBleMtu);
    } else {
        LOG_WARN("Hosted BLE unable to request MTU %u, rc=%d", kPreferredBleMtu, mtuResult);
    }

    bleServer = BLEDevice::createServer();
    if (!bleServer) {
        LOG_ERROR("Hosted BLE createServer failed");
        return false;
    }

    const int nameRc = ble_svc_gap_device_name_set(BLEDevice::getDeviceName().c_str());
    if (nameRc != 0) {
        LOG_WARN("Hosted BLE device_name_set rc=%d %s", nameRc, BLEUtils::returnCodeToString(nameRc));
    }

    bleServer->setCallbacks(new HostedBluetoothServerCallback(this));

    BLEService *meshService = bleServer->createService(MESH_SERVICE_UUID);
    if (!meshService) {
        LOG_ERROR("Hosted BLE mesh service creation failed");
        return false;
    }

    BLECharacteristic *toRadioCharacteristic = nullptr;
    BLECharacteristic *fromRadioCharacteristic = nullptr;
    if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        toRadioCharacteristic = meshService->createCharacteristic(TORADIO_UUID, BLECharacteristic::PROPERTY_WRITE);
        fromRadioCharacteristic = meshService->createCharacteristic(FROMRADIO_UUID, BLECharacteristic::PROPERTY_READ);
        fromNumCharacteristic = meshService->createCharacteristic(FROMNUM_UUID, BLECharacteristic::PROPERTY_NOTIFY |
                                                                                    BLECharacteristic::PROPERTY_READ);
        logRadioCharacteristic = meshService->createCharacteristic(LOGRADIO_UUID, BLECharacteristic::PROPERTY_NOTIFY |
                                                                                      BLECharacteristic::PROPERTY_READ);
    } else {
        toRadioCharacteristic = meshService->createCharacteristic(TORADIO_UUID, BLECharacteristic::PROPERTY_WRITE |
                                                                                    BLECharacteristic::PROPERTY_WRITE_AUTHEN |
                                                                                    BLECharacteristic::PROPERTY_WRITE_ENC);
        fromRadioCharacteristic = meshService->createCharacteristic(FROMRADIO_UUID, BLECharacteristic::PROPERTY_READ |
                                                                                        BLECharacteristic::PROPERTY_READ_AUTHEN |
                                                                                        BLECharacteristic::PROPERTY_READ_ENC);
        fromNumCharacteristic = meshService->createCharacteristic(
            FROMNUM_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ |
                              BLECharacteristic::PROPERTY_READ_AUTHEN | BLECharacteristic::PROPERTY_READ_ENC);
        logRadioCharacteristic = meshService->createCharacteristic(
            LOGRADIO_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_READ_AUTHEN | BLECharacteristic::PROPERTY_READ_ENC);
    }

    if (!toRadioCharacteristic || !fromRadioCharacteristic || !fromNumCharacteristic || !logRadioCharacteristic) {
        LOG_ERROR("Hosted BLE characteristic creation failed");
        return false;
    }

    if (!bluetoothPhoneAPI) {
        bluetoothPhoneAPI = new HostedBluetoothPhoneAPI();
    }

    if (!toRadioCallbacks) {
        toRadioCallbacks = new HostedBluetoothToRadioCallback();
    }
    if (!fromRadioCallbacks) {
        fromRadioCallbacks = new HostedBluetoothFromRadioCallback();
    }

    toRadioCharacteristic->setCallbacks(toRadioCallbacks);
    fromRadioCharacteristic->setCallbacks(fromRadioCallbacks);
    meshService->start();

    startAdvertising();
    return true;
}

void HostedBluetooth::shutdownGatt()
{
    if (bluetoothPhoneAPI) {
        bluetoothPhoneAPI->close();
        delete bluetoothPhoneAPI;
        bluetoothPhoneAPI = nullptr;
    }

    delete toRadioCallbacks;
    toRadioCallbacks = nullptr;

    delete fromRadioCallbacks;
    fromRadioCallbacks = nullptr;

    delete securityCallbacks;
    securityCallbacks = nullptr;

    if (bleServer) {
        BLEAdvertising *advertising = BLEDevice::getAdvertising();
        if (advertising) {
            advertising->stop();
        }
    }

    fromNumCharacteristic = nullptr;
    logRadioCharacteristic = nullptr;
    bleServer = nullptr;
    hostedConnHandle = BLE_HS_CONN_HANDLE_NONE;
}

void HostedBluetooth::startAdvertising()
{
    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    if (!advertising) {
        LOG_ERROR("Hosted BLE getAdvertising failed");
        return;
    }

    advertising->stop();
    advertising->reset();
    advertising->addServiceUUID(MESH_SERVICE_UUID);

    BLEAdvertisementData scan;
    scan.setName(getDeviceName());
    advertising->setScanResponseData(scan);
    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);

    if (!advertising->start(0)) {
        LOG_ERROR("Hosted BLE failed to start advertising");
    } else {
        LOG_INFO("Hosted BLE advertising started");
    }
}

#endif
