#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "BluetoothCommon.h"
#include "NimbleBluetooth.h"
#include "PowerFSM.h"
#include "StaticPointerQueue.h"

#include "concurrency/OSThread.h"
#include "main.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"
#include "sleep.h"
#include <NimBLEDevice.h>
#include <atomic>
#include <mutex>

#ifdef NIMBLE_TWO
#include "NimBLEAdvertising.h"
#include "NimBLEExtAdvertising.h"
#include "PowerStatus.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "host/ble_gap.h"
#else
#include "nimble/nimble/host/include/host/ble_gap.h"
#endif

#define DEBUG_NIMBLE_ON_READ_TIMING // uncomment to time onRead duration

#define NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE 3
#define NIMBLE_BLUETOOTH_FROM_PHONE_QUEUE_SIZE 3

namespace
{
constexpr uint16_t kPreferredBleMtu = 517;
constexpr uint16_t kPreferredBleTxOctets = 251;
constexpr uint16_t kPreferredBleTxTimeUs = (kPreferredBleTxOctets + 14) * 8;
} // namespace
#endif

NimBLECharacteristic *fromNumCharacteristic;
NimBLECharacteristic *BatteryCharacteristic;
NimBLECharacteristic *logRadioCharacteristic;
NimBLEServer *bleServer;

static bool passkeyShowing;

class BluetoothPhoneAPI : public PhoneAPI, public concurrency::OSThread
{
  public:
    BluetoothPhoneAPI() : concurrency::OSThread("NimbleBluetooth") {}

    /* Packets from phone (BLE onWrite callback) */
    std::mutex fromPhoneMutex;
    std::atomic<size_t> fromPhoneQueueSize{0};
    // We use array here (and pay the cost of memcpy) to avoid dynamic memory allocations and frees across FreeRTOS tasks.
    std::array<NimBLEAttValue, NIMBLE_BLUETOOTH_FROM_PHONE_QUEUE_SIZE> fromPhoneQueue;

    /* Packets to phone (BLE onRead callback) */
    std::mutex toPhoneMutex;
    std::atomic<size_t> toPhoneQueueSize{0};
    // We use array here (and pay the cost of memcpy) to avoid dynamic memory allocations and frees across FreeRTOS tasks.
    std::array<std::array<uint8_t, meshtastic_FromRadio_size>, NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE> toPhoneQueue;
    std::array<size_t, NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE> toPhoneQueueByteSizes;
    // The onReadCallbackIsWaitingForData flag provides synchronization between the NimBLE task's onRead callback and our main
    // task's runOnce. It's only set by onRead, and only cleared by runOnce.
    std::atomic<bool> onReadCallbackIsWaitingForData{false};

    /* Statistics/logging helpers */
    std::atomic<int32_t> readCount{0};
    std::atomic<int32_t> notifyCount{0};

  protected:
    bool runOnceHasWorkToDo()
    {
        // return true if the onRead callback is waiting for us, or if we have packets from the phone to handle.
        return onReadCallbackIsWaitingForData || fromPhoneQueueSize > 0;
    }

    virtual int32_t runOnce() override
    {
        // Stack buffer for getFromRadio packet
        uint8_t fromRadioBytes[meshtastic_FromRadio_size] = {0};
        size_t numBytes = 0;

        while (runOnceHasWorkToDo()) {
            // Service onRead first, because the onRead callback blocks NimBLE until we clear onReadCallbackIsWaitingForData.
            if (onReadCallbackIsWaitingForData) {
                numBytes = getFromRadio(fromRadioBytes);

                if (numBytes == 0) {
                    // Client expected a read, but we have nothing to send.
                    // This is 100% OK, as we expect clients to do this regularly to make sure they have nothing else to read.
                    // LOG_INFO("BLE getFromRadio returned numBytes=0");
                }

                // Push to toPhoneQueue, protected by toPhoneMutex. Hold the mutex as briefly as possible.
                if (toPhoneQueueSize < NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE) {
                    // Note: the comparison above is safe without a mutex because we are the only method that *increases*
                    // toPhoneQueueSize. (It's okay if toPhoneQueueSize *decreases* in the NimBLE task meanwhile.)

                    { // scope for toPhoneMutex mutex
                        std::lock_guard<std::mutex> guard(toPhoneMutex);
                        size_t storeAtIndex = toPhoneQueueSize.load();
                        memcpy(toPhoneQueue[storeAtIndex].data(), fromRadioBytes, numBytes);
                        toPhoneQueueByteSizes[storeAtIndex] = numBytes;
                        toPhoneQueueSize++;
                    }
                } else {
                    // Shouldn't happen because the onRead callback shouldn't be waiting if the queue is full!
                    LOG_ERROR("Shouldn't happen! Drop FromRadio packet, toPhoneQueue full (%u bytes)", numBytes);
                }

                onReadCallbackIsWaitingForData = false; // only clear this flag AFTER the push

                // Return immediately after clearing onReadCallbackIsWaitingForData so that our onRead callback can proceed.
                if (runOnceHasWorkToDo()) {
                    // Allow a minimal delay so the NimBLE task's onRead callback can pick up this packet, and then come back here
                    // ASAP to handle whatever work is next!
                    return 0;
                } else {
                    // Nothing queued. We can wait for the next callback.
                    return INT32_MAX;
                }
            }

            // Handle packets we received from onWrite from the phone.
            if (fromPhoneQueueSize > 0) {
                // Note: the comparison above is safe without a mutex because we are the only method that *decreases*
                // fromPhoneQueueSize. (It's okay if fromPhoneQueueSize *increases* in the NimBLE task meanwhile.)

                LOG_DEBUG("NimbleBluetooth: handling ToRadio packet, fromPhoneQueueSize=%u", fromPhoneQueueSize.load());

                // Pop the front of fromPhoneQueue, holding the mutex only briefly while we pop.
                NimBLEAttValue val;
                { // scope for fromPhoneMutex mutex
                    std::lock_guard<std::mutex> guard(fromPhoneMutex);
                    val = fromPhoneQueue[0];

                    // Shift the rest of the queue down
                    for (uint8_t i = 1; i < fromPhoneQueueSize; i++) {
                        fromPhoneQueue[i - 1] = fromPhoneQueue[i];
                    }
                    fromPhoneQueueSize--;
                }

                handleToRadio(val.data(), val.length());
            }
        }

        // the run is triggered via NimbleBluetoothToRadioCallback and NimbleBluetoothFromRadioCallback
        return INT32_MAX;
    }

    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum)
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        int currentNotifyCount = notifyCount.fetch_add(1);

        uint8_t cc = bleServer->getConnectedCount();

        // This logging slows things down when there are lots of packets going to the phone, like initial connection:
        // LOG_DEBUG("BLE notify(%d) fromNum: %d connections: %d", currentNotifyCount, fromRadioNum, cc);

        uint8_t val[4];
        put_le32(val, fromRadioNum);

        fromNumCharacteristic->setValue(val, sizeof(val));
#ifdef NIMBLE_TWO
        fromNumCharacteristic->notify(val, sizeof(val), BLE_HS_CONN_HANDLE_NONE);
#else
        fromNumCharacteristic->notify();
#endif
    }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() { return bleServer && bleServer->getConnectedCount() > 0; }
};

static BluetoothPhoneAPI *bluetoothPhoneAPI;
/**
 * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
 */

// Last ToRadio value received from the phone
static uint8_t lastToRadio[MAX_TO_FROM_RADIO_SIZE];

class NimbleBluetoothToRadioCallback : public NimBLECharacteristicCallbacks
{
#ifdef NIMBLE_TWO
    virtual void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
#else
    virtual void onWrite(NimBLECharacteristic *pCharacteristic)

#endif
    {
        // CAUTION: This callback runs in the NimBLE task!!! Don't do anything except communicate with the main task's runOnce.
        // Assumption: onWrite is serialized by NimBLE, so we don't need to lock here against multiple concurrent onWrite calls.

        auto val = pCharacteristic->getValue();

        if (memcmp(lastToRadio, val.data(), val.length()) != 0) {
            if (bluetoothPhoneAPI->fromPhoneQueueSize < NIMBLE_BLUETOOTH_FROM_PHONE_QUEUE_SIZE) {
                // Note: the comparison above is safe without a mutex because we are the only method that *increases*
                // fromPhoneQueueSize. (It's okay if fromPhoneQueueSize *decreases* in the main task meanwhile.)
                memcpy(lastToRadio, val.data(), val.length());

                { // scope for fromPhoneMutex mutex
                    // Append to fromPhoneQueue, protected by fromPhoneMutex. Hold the mutex as briefly as possible.
                    std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->fromPhoneMutex);
                    bluetoothPhoneAPI->fromPhoneQueue.at(bluetoothPhoneAPI->fromPhoneQueueSize) = val;
                    bluetoothPhoneAPI->fromPhoneQueueSize++;
                }

                // After releasing the mutex, schedule immediate processing of the new packet.
                bluetoothPhoneAPI->setIntervalFromNow(0);
                concurrency::mainDelay.interrupt(); // wake up main loop if sleeping
            } else {
                LOG_WARN("Drop ToRadio packet, fromPhoneQueue full (%u bytes)", val.length());
            }
        } else {
            LOG_DEBUG("Drop duplicate ToRadio packet (%u bytes)", val.length());
        }
    }
};

class NimbleBluetoothFromRadioCallback : public NimBLECharacteristicCallbacks
{
#ifdef NIMBLE_TWO
    virtual void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
#else
    virtual void onRead(NimBLECharacteristic *pCharacteristic)
#endif
    {
        // CAUTION: This callback runs in the NimBLE task!!! Don't do anything except communicate with the main task's runOnce.

        int currentReadCount = bluetoothPhoneAPI->readCount.fetch_add(1);
        int tries = 0;

#ifdef DEBUG_NIMBLE_ON_READ_TIMING
        int startMillis = millis();
        // LOG_DEBUG("BLE onRead(%d): start millis=%d", currentReadCount, startMillis);
#endif

        // Tell the main task that we'd like a packet.
        bluetoothPhoneAPI->onReadCallbackIsWaitingForData = true;

        while (bluetoothPhoneAPI->onReadCallbackIsWaitingForData && tries < 400) {
            // Schedule the main task runOnce to run ASAP.
            bluetoothPhoneAPI->setIntervalFromNow(0);
            concurrency::mainDelay.interrupt(); // wake up main loop if sleeping

            if (!bluetoothPhoneAPI->onReadCallbackIsWaitingForData) {
                // we may be able to break even before a delay, if the call to interrupt woke up the main loop and it ran already
#ifdef DEBUG_NIMBLE_ON_READ_TIMING
                LOG_DEBUG("BLE onRead(%d): broke before delay after %u ms, %d tries", currentReadCount, millis() - startMillis,
                          tries);
#endif
                break;
            }

            delay(tries < 10 ? 2 : 5);
            tries++;
        }

        // Pop from toPhoneQueue, protected by toPhoneMutex. Hold the mutex as briefly as possible.
        uint8_t fromRadioBytes[meshtastic_FromRadio_size] = {0}; // Stack buffer for getFromRadio packet
        size_t numBytes = 0;
        { // scope for toPhoneMutex mutex
            std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->toPhoneMutex);
            size_t toPhoneQueueSize = bluetoothPhoneAPI->toPhoneQueueSize.load();
            if (toPhoneQueueSize > 0) {
                // Copy from the front of the toPhoneQueue
                memcpy(fromRadioBytes, bluetoothPhoneAPI->toPhoneQueue[0].data(), bluetoothPhoneAPI->toPhoneQueueByteSizes[0]);
                numBytes = bluetoothPhoneAPI->toPhoneQueueByteSizes[0];

                // Shift the rest of the queue down
                for (uint8_t i = 1; i < toPhoneQueueSize; i++) {
                    memcpy(bluetoothPhoneAPI->toPhoneQueue[i - 1].data(), bluetoothPhoneAPI->toPhoneQueue[i].data(),
                           bluetoothPhoneAPI->toPhoneQueueByteSizes[i]);
                    // The above line is similar to:
                    //   bluetoothPhoneAPI->toPhoneQueue[i - 1] = bluetoothPhoneAPI->toPhoneQueue[i]
                    // but is usually faster because it doesn't have to copy all the trailing bytes beyond
                    // toPhoneQueueByteSizes[i].
                    //
                    // We deliberately use an array here (and pay the CPU cost of some memcpy) to avoid synchronizing dynamic
                    // memory allocations and frees across FreeRTOS tasks.

                    bluetoothPhoneAPI->toPhoneQueueByteSizes[i - 1] = bluetoothPhoneAPI->toPhoneQueueByteSizes[i];
                }
                bluetoothPhoneAPI->toPhoneQueueSize--;
            } else {
                // nothing in the toPhoneQueue; that's fine, and we'll just have numBytes=0.
            }
        }

#ifdef DEBUG_NIMBLE_ON_READ_TIMING
        int finishMillis = millis();
        LOG_DEBUG("BLE onRead(%d): onReadCallbackIsWaitingForData took %u ms. numBytes=%d", currentReadCount,
                  finishMillis - startMillis, numBytes);
#endif

        pCharacteristic->setValue(fromRadioBytes, numBytes);

        bool sentSomething = false;
        if (numBytes != 0)
            sentSomething = true;

        // If we did send something, wake up the main loop if it's sleeping in case there are more packets ready to send.
        if (sentSomething) {
            bluetoothPhoneAPI->setIntervalFromNow(0);
            concurrency::mainDelay.interrupt(); // wake up main loop if sleeping
        }
    }
};

class NimbleBluetoothServerCallback : public NimBLEServerCallbacks
{
#ifdef NIMBLE_TWO
  public:
    NimbleBluetoothServerCallback(NimbleBluetooth *ble) { this->ble = ble; }

  private:
    NimbleBluetooth *ble;

    virtual uint32_t onPassKeyDisplay()
#else
    virtual uint32_t onPassKeyRequest()
#endif
    {
        uint32_t passkey = config.bluetooth.fixed_pin;

        if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
            LOG_INFO("Use random passkey");
            // This is the passkey to be entered on peer - we pick a number >100,000 to ensure 6 digits
            passkey = random(100000, 999999);
        }
        LOG_INFO("*** Enter passkey %d on the peer side ***", passkey);

        powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
        meshtastic::BluetoothStatus newStatus(std::to_string(passkey));
        bluetoothStatus->updateStatus(&newStatus);

#if HAS_SCREEN // Todo: migrate this display code back into Screen class, and observe bluetoothStatus
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

        return passkey;
    }

#ifdef NIMBLE_TWO
    virtual void onAuthenticationComplete(NimBLEConnInfo &connInfo)
#else
    virtual void onAuthenticationComplete(ble_gap_conn_desc *desc)
#endif
    {
        LOG_INFO("BLE authentication complete");

        meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
        bluetoothStatus->updateStatus(&newStatus);

        // Todo: migrate this display code back into Screen class, and observe bluetoothStatus
        if (passkeyShowing) {
            passkeyShowing = false;
            if (screen)
                screen->endAlert();
        }

        // Request high-throughput connection parameters for faster setup
#ifdef NIMBLE_TWO
        requestHighThroughputConnection(connInfo);
#else
        requestHighThroughputConnection(desc);
#endif
    }

#ifdef NIMBLE_TWO
    virtual void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
    {
        LOG_INFO("BLE incoming connection %s", connInfo.getAddress().toString().c_str());

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6)
        const uint16_t connHandle = connInfo.getConnHandle();
        int phyResult =
            ble_gap_set_prefered_le_phy(connHandle, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_CODED_ANY);
        if (phyResult == 0) {
            LOG_INFO("BLE conn %u requested 2M PHY", connHandle);
        } else {
            LOG_WARN("Failed to prefer 2M PHY for conn %u, rc=%d", connHandle, phyResult);
        }

        int dataLenResult = ble_gap_set_data_len(connHandle, kPreferredBleTxOctets, kPreferredBleTxTimeUs);
        if (dataLenResult == 0) {
            LOG_INFO("BLE conn %u requested data length %u bytes", connHandle, kPreferredBleTxOctets);
        } else {
            LOG_WARN("Failed to raise data length for conn %u, rc=%d", connHandle, dataLenResult);
        }

        LOG_INFO("BLE conn %u initial MTU %u (target %u)", connHandle, connInfo.getMTU(), kPreferredBleMtu);
        pServer->updateConnParams(connHandle, 6, 12, 0, 200);
#endif
    }

    virtual void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
    {
        LOG_INFO("BLE disconnect reason: %d", reason);
#else
    virtual void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc)
    {
        LOG_INFO("BLE disconnect");
#endif
#ifdef NIMBLE_TWO
        if (ble->isDeInit)
            return;
#endif

        meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
        bluetoothStatus->updateStatus(&newStatus);

        if (bluetoothPhoneAPI) {
            bluetoothPhoneAPI->close();

            bluetoothPhoneAPI->fromPhoneQueueSize = 0;

            bluetoothPhoneAPI->toPhoneQueueSize = 0;
            bluetoothPhoneAPI->onReadCallbackIsWaitingForData = false;

            bluetoothPhoneAPI->readCount = 0;
            bluetoothPhoneAPI->notifyCount = 0;
        }

        // Clear the last ToRadio packet buffer to avoid rejecting first packet from new connection
        memset(lastToRadio, 0, sizeof(lastToRadio));
#ifdef NIMBLE_TWO
        // Restart Advertising
        ble->startAdvertising();
#else
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        if (!pAdvertising->start(0)) {
            if (pAdvertising->isAdvertising()) {
                LOG_DEBUG("BLE advertising already running");
            } else {
                LOG_ERROR("BLE failed to restart advertising");
            }
        }
#endif
    }

#ifdef NIMBLE_TWO
    void requestHighThroughputConnection(NimBLEConnInfo &connInfo)
#else
    void requestHighThroughputConnection(ble_gap_conn_desc *desc)
#endif
    {
        /* Request a lower-latency, higher-throughput BLE connection.

        This comes at the cost of higher power consumption, so we may want to only use this for initial setup, and then switch to
        a slower mode.

        See https://developer.apple.com/library/archive/qa/qa1931/_index.html for formulas to calculate values, iOS/macOS
        constraints, and recommendations. (Android doesn't have specific constraints, but seems to be compatible with the Apple
        recommendations.)

        minInterval (units of 1.25ms): 7.5ms = 6 (lower than the Apple recommended minimum, but allows faster when the client
        supports it.) maxInterval (units of 1.25ms): 15ms = 12 latency: 0 (don't allow peripheral to skip any connection events)
        timeout (units of 10ms): 6 seconds = 600 (supervision timeout)
        */
        LOG_INFO("BLE requestHighThroughputConnection");
#ifdef NIMBLE_TWO
        bleServer->updateConnParams(connInfo.getConnHandle(), 6, 12, 0, 600);
#else
        bleServer->updateConnParams(desc->conn_handle, 6, 12, 0, 600);
#endif
    }
};

static NimbleBluetoothToRadioCallback *toRadioCallbacks;
static NimbleBluetoothFromRadioCallback *fromRadioCallbacks;

void NimbleBluetooth::shutdown()
{
    // No measurable power saving for ESP32 during light-sleep(?)
#ifndef ARCH_ESP32
    // Shutdown bluetooth for minimum power draw
    LOG_INFO("Disable bluetooth");
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->reset();
    pAdvertising->stop();
#endif
}

// Proper shutdown for ESP32. Needs reboot to reverse.
void NimbleBluetooth::deinit()
{
#ifdef ARCH_ESP32
    LOG_INFO("Disable bluetooth until reboot");
    isDeInit = true;

#ifdef BLE_LED
#ifdef BLE_LED_INVERTED
    digitalWrite(BLE_LED, HIGH);
#else
    digitalWrite(BLE_LED, LOW);
#endif
#endif
#ifndef NIMBLE_TWO
    NimBLEDevice::deinit();
#endif
#endif
}

// Has initial setup been completed
bool NimbleBluetooth::isActive()
{
    return bleServer;
}

bool NimbleBluetooth::isConnected()
{
    return bleServer->getConnectedCount() > 0;
}

int NimbleBluetooth::getRssi()
{
    if (bleServer && isConnected()) {
        auto service = bleServer->getServiceByUUID(MESH_SERVICE_UUID);
        uint16_t handle = service->getHandle();
#ifdef NIMBLE_TWO
        return NimBLEDevice::getClientByHandle(handle)->getRssi();
#else
        return NimBLEDevice::getClientByID(handle)->getRssi();
#endif
    }
    return 0; // FIXME figure out where to source this
}

void NimbleBluetooth::setup()
{
    // Uncomment for testing
    // NimbleBluetooth::clearBonds();

    LOG_INFO("Init the NimBLE bluetooth module");

    NimBLEDevice::init(getDeviceName());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    int mtuResult = NimBLEDevice::setMTU(kPreferredBleMtu);
    if (mtuResult == 0) {
        LOG_INFO("BLE MTU request set to %u", kPreferredBleMtu);
    } else {
        LOG_WARN("Unable to request MTU %u, rc=%d", kPreferredBleMtu, mtuResult);
    }

    int phyResult = ble_gap_set_prefered_default_le_phy(BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK);
    if (phyResult == 0) {
        LOG_INFO("BLE default PHY preference set to 2M");
    } else {
        LOG_WARN("Failed to prefer 2M PHY by default, rc=%d", phyResult);
    }

    int dataLenResult = ble_gap_write_sugg_def_data_len(kPreferredBleTxOctets, kPreferredBleTxTimeUs);
    if (dataLenResult == 0) {
        LOG_INFO("BLE suggested data length set to %u bytes", kPreferredBleTxOctets);
    } else {
        LOG_WARN("Failed to raise suggested data length (%u/%u), rc=%d", kPreferredBleTxOctets, kPreferredBleTxTimeUs,
                 dataLenResult);
    }
#endif

    if (config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC);
        NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
        NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    }
    bleServer = NimBLEDevice::createServer();
#ifdef NIMBLE_TWO
    NimbleBluetoothServerCallback *serverCallbacks = new NimbleBluetoothServerCallback(this);
#else
    NimbleBluetoothServerCallback *serverCallbacks = new NimbleBluetoothServerCallback();
#endif
    bleServer->setCallbacks(serverCallbacks, true);
    setupService();
    startAdvertising();
}

void NimbleBluetooth::setupService()
{
    NimBLEService *bleService = bleServer->createService(MESH_SERVICE_UUID);
    NimBLECharacteristic *ToRadioCharacteristic;
    NimBLECharacteristic *FromRadioCharacteristic;
    // Define the characteristics that the app is looking for
    if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        ToRadioCharacteristic = bleService->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE);
        // Allow notifications so phones can stream FromRadio without polling.
        FromRadioCharacteristic = bleService->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
        fromNumCharacteristic = bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
        logRadioCharacteristic =
            bleService->createCharacteristic(LOGRADIO_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ, 512U);
    } else {
        ToRadioCharacteristic = bleService->createCharacteristic(
            TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC);
        FromRadioCharacteristic = bleService->createCharacteristic(
            FROMRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
        fromNumCharacteristic =
            bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ |
                                                               NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
        logRadioCharacteristic = bleService->createCharacteristic(
            LOGRADIO_UUID,
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC, 512U);
    }
    bluetoothPhoneAPI = new BluetoothPhoneAPI();

    toRadioCallbacks = new NimbleBluetoothToRadioCallback();
    ToRadioCharacteristic->setCallbacks(toRadioCallbacks);

    fromRadioCallbacks = new NimbleBluetoothFromRadioCallback();
    FromRadioCharacteristic->setCallbacks(fromRadioCallbacks);

    bleService->start();

    // Setup the battery service
    NimBLEService *batteryService = bleServer->createService(NimBLEUUID((uint16_t)0x180f)); // 0x180F is the Battery Service
    BatteryCharacteristic = batteryService->createCharacteristic( // 0x2A19 is the Battery Level characteristic)
        (uint16_t)0x2a19, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 1);
#ifdef NIMBLE_TWO
    NimBLE2904 *batteryLevelDescriptor = BatteryCharacteristic->create2904();
#else
    NimBLE2904 *batteryLevelDescriptor = (NimBLE2904 *)BatteryCharacteristic->createDescriptor((uint16_t)0x2904);
#endif
    batteryLevelDescriptor->setFormat(NimBLE2904::FORMAT_UINT8);
    batteryLevelDescriptor->setNamespace(1);
    batteryLevelDescriptor->setUnit(0x27ad);

    batteryService->start();
}

void NimbleBluetooth::startAdvertising()
{
#ifdef NIMBLE_TWO
    NimBLEExtAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    NimBLEExtAdvertisement legacyAdvertising;

    legacyAdvertising.setLegacyAdvertising(true);
    legacyAdvertising.setScannable(true);
    legacyAdvertising.setConnectable(true);
    legacyAdvertising.setFlags(BLE_HS_ADV_F_DISC_GEN);
    if (powerStatus->getHasBattery() == 1) {
        legacyAdvertising.setCompleteServices(NimBLEUUID((uint16_t)0x180f));
    }
    legacyAdvertising.setCompleteServices(NimBLEUUID(MESH_SERVICE_UUID));
    legacyAdvertising.setMinInterval(500);
    legacyAdvertising.setMaxInterval(1000);

    NimBLEExtAdvertisement legacyScanResponse;
    legacyScanResponse.setLegacyAdvertising(true);
    legacyScanResponse.setConnectable(true);
    legacyScanResponse.setName(getDeviceName());

    if (!pAdvertising->setInstanceData(0, legacyAdvertising)) {
        LOG_ERROR("BLE failed to set legacyAdvertising");
    } else if (!pAdvertising->setScanResponseData(0, legacyScanResponse)) {
        LOG_ERROR("BLE failed to set legacyScanResponse");
    } else if (!pAdvertising->start(0, 0, 0)) {
        LOG_ERROR("BLE failed to start legacyAdvertising");
    }
#else
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->reset();
    pAdvertising->addServiceUUID(MESH_SERVICE_UUID);
    pAdvertising->addServiceUUID(NimBLEUUID((uint16_t)0x180f)); // 0x180F is the Battery Service
    pAdvertising->start(0);
#endif
}

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level)
{
    if ((config.bluetooth.enabled == true) && bleServer && nimbleBluetooth->isConnected()) {
        BatteryCharacteristic->setValue(&level, 1);
#ifdef NIMBLE_TWO
        BatteryCharacteristic->notify(&level, 1, BLE_HS_CONN_HANDLE_NONE);
#else
        BatteryCharacteristic->notify();
#endif
    }
}

void NimbleBluetooth::clearBonds()
{
    LOG_INFO("Clearing bluetooth bonds!");
    NimBLEDevice::deleteAllBonds();
}

void NimbleBluetooth::sendLog(const uint8_t *logMessage, size_t length)
{
    if (!bleServer || !isConnected() || length > 512) {
        return;
    }
#ifdef NIMBLE_TWO
    logRadioCharacteristic->notify(logMessage, length, BLE_HS_CONN_HANDLE_NONE);
#else
    logRadioCharacteristic->notify(logMessage, length, true);
#endif
}

void clearNVS()
{
    NimBLEDevice::deleteAllBonds();
#ifdef ARCH_ESP32
    ESP.restart();
#endif
}
#endif
