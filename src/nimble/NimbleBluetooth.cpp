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

namespace
{
constexpr uint16_t kPreferredBleMtu = 517;
constexpr uint16_t kPreferredBleTxOctets = 251;
constexpr uint16_t kPreferredBleTxTimeUs = (kPreferredBleTxOctets + 14) * 8;
} // namespace
#endif

// Debugging options: careful, they slow things down quite a bit!
// #define DEBUG_NIMBLE_ON_READ_TIMING  // uncomment to time onRead duration
// #define DEBUG_NIMBLE_ON_WRITE_TIMING // uncomment to time onWrite duration
// #define DEBUG_NIMBLE_NOTIFY          // uncomment to enable notify logging

#define NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE 3
#define NIMBLE_BLUETOOTH_FROM_PHONE_QUEUE_SIZE 3

NimBLECharacteristic *fromNumCharacteristic;
NimBLECharacteristic *BatteryCharacteristic;
NimBLECharacteristic *logRadioCharacteristic;
NimBLEServer *bleServer;

static bool passkeyShowing;
static std::atomic<uint16_t> nimbleBluetoothConnHandle{BLE_HS_CONN_HANDLE_NONE}; // BLE_HS_CONN_HANDLE_NONE means "no connection"

class BluetoothPhoneAPI : public PhoneAPI, public concurrency::OSThread
{
    /*
      CAUTION: There's a lot going on here and lots of room to break things.

      This NimbleBluetooth.cpp file does some tricky synchronization between the NimBLE FreeRTOS task (which runs the onRead and
      onWrite callbacks) and the main task (which runs runOnce and the rest of PhoneAPI).

      The main idea is to add a little bit of synchronization here to make it so that the rest of the codebase doesn't have to
      know about concurrency and mutexes, and can just run happily ever after as a cooperative multitasking OSThread system, where
      locking isn't something that anyone has to worry about too much! :)

      We achieve this by having some queues and mutexes in this file only, and ensuring that all calls to getFromRadio and
      handleToRadio are only made from the main FreeRTOS task. This way, the rest of the codebase doesn't have to worry about
      being run concurrently, which would make everything else much much much more complicated.

      PHONE -> RADIO:
        - [NimBLE FreeRTOS task:] onWrite callback holds fromPhoneMutex and pushes received packets into fromPhoneQueue.
        - [Main task:] runOnceHandleFromPhoneQueue in main task holds fromPhoneMutex, pulls packets from fromPhoneQueue, and calls
      handleToRadio **in main task**.

      RADIO -> PHONE:
        - [NimBLE FreeRTOS task:] onRead callback sets onReadCallbackIsWaitingForData flag and polls in a busy loop. (unless
      there's already a packet waiting in toPhoneQueue)
        - [Main task:] runOnceHandleToPhoneQueue sees onReadCallbackIsWaitingForData flag, calls getFromRadio **in main task** to
      get packets from radio, holds toPhoneMutex, pushes the packet into toPhoneQueue, and clears the
      onReadCallbackIsWaitingForData flag.
        - [NimBLE FreeRTOS task:] onRead callback sees that the onReadCallbackIsWaitingForData flag cleared, holds toPhoneMutex,
      pops the packet from toPhoneQueue, and returns it to NimBLE.

      MUTEXES:
        - fromPhoneMutex protects fromPhoneQueue and fromPhoneQueueSize
        - toPhoneMutex protects toPhoneQueue, toPhoneQueueByteSizes, and toPhoneQueueSize

      ATOMICS:
        - fromPhoneQueueSize is only increased by onWrite, and only decreased by runOnceHandleFromPhoneQueue (or onDisconnect).
        - toPhoneQueueSize is only increased by runOnceHandleToPhoneQueue, and only decreased by onRead (or onDisconnect).
        - onReadCallbackIsWaitingForData is a flag. It's only set by onRead, and only cleared by runOnceHandleToPhoneQueue (or
      onDisconnect).

      PRELOADING: see comments in runOnceToPhoneCanPreloadNextPacket about when it's safe to preload packets from getFromRadio.

      BLE CONNECTION PARAMS:
        - During config, we request a high-throughput, low-latency BLE connection for speed.
        - After config, we switch to a lower-power BLE connection for steady-state use to extend battery life.

      MEMORY MANAGEMENT:
        - We keep packets on the stack and do not allocate heap.
        - We use std::array for fromPhoneQueue and toPhoneQueue to avoid mallocs and frees across FreeRTOS tasks.
        - Yes, we have to do some copy operations on pop because of this, but it's worth it to avoid cross-task memory management.

      NOTIFY IS BROKEN:
        - Adding NIMBLE_PROPERTY::NOTIFY to FromRadioCharacteristic appears to break things. It is NOT backwards compatible.

      ZERO-SIZE READS:
        - Returning a zero-size read from onRead breaks some clients during the config phase. So we have to block onRead until we
      have data.
        - During the STATE_SEND_PACKETS phase, it's totally OK to return zero-size reads, as clients are expected to do reads
      until they get a 0-byte response.

      CROSS-TASK WAKEUP:
        - If you call: bluetoothPhoneAPI->setIntervalFromNow(0); to schedule immediate processing of new data,
        - Then you should also call: concurrency::mainDelay.interrupt(); to wake up the main loop if it's sleeping.
        - Otherwise, you're going to wait ~100ms or so until the main loop wakes up from some other cause.
    */

  public:
    BluetoothPhoneAPI() : concurrency::OSThread("NimbleBluetooth") {}

    /* Packets from phone (BLE onWrite callback) */
    std::mutex fromPhoneMutex;
    std::atomic<size_t> fromPhoneQueueSize{0};
    // We use array here (and pay the cost of memcpy) to avoid dynamic memory allocations and frees across FreeRTOS tasks.
    std::array<NimBLEAttValue, NIMBLE_BLUETOOTH_FROM_PHONE_QUEUE_SIZE> fromPhoneQueue{};

    /* Packets to phone (BLE onRead callback) */
    std::mutex toPhoneMutex;
    std::atomic<size_t> toPhoneQueueSize{0};
    // We use array here (and pay the cost of memcpy) to avoid dynamic memory allocations and frees across FreeRTOS tasks.
    std::array<std::array<uint8_t, meshtastic_FromRadio_size>, NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE> toPhoneQueue{};
    std::array<size_t, NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE> toPhoneQueueByteSizes{};
    // The onReadCallbackIsWaitingForData flag provides synchronization between the NimBLE task's onRead callback and our main
    // task's runOnce. It's only set by onRead, and only cleared by runOnce.
    std::atomic<bool> onReadCallbackIsWaitingForData{false};

    /* Statistics/logging helpers */
    std::atomic<int32_t> readCount{0};
    std::atomic<int32_t> notifyCount{0};
    std::atomic<int32_t> writeCount{0};

  protected:
    virtual int32_t runOnce() override
    {
        while (runOnceHasWorkToDo()) {
            /*
              PROCESS fromPhoneQueue BEFORE toPhoneQueue:

              In normal STATE_SEND_PACKETS operation, it's unlikely that we'll have both writes and reads to process at the same
              time, because either onWrite or onRead will trigger this runOnce. And in STATE_SEND_PACKETS, it's generally ok to
              service either the reads or writes first.

              However, during the initial setup wantConfig packet, the clients send a write and immediately send a read, and they
              expect the read will respond to the write. (This also happens when a client goes from STATE_SEND_PACKETS back to
              another wantConfig, like the iOS client does when requesting the nodedb after requesting the main config only.)

              So it's safest to always service writes (fromPhoneQueue) before reads (toPhoneQueue), so that any "synchronous"
              write-then-read sequences from the client work as expected, even if this means we block onRead for a while: this is
              what the client wants!
            */

            // PHONE -> RADIO:
            runOnceHandleFromPhoneQueue(); // pull data from onWrite to handleToRadio

            // RADIO -> PHONE:
            runOnceHandleToPhoneQueue(); // push data from getFromRadio to onRead
        }

        // the run is triggered via NimbleBluetoothToRadioCallback and NimbleBluetoothFromRadioCallback
        return INT32_MAX;
    }

    virtual void onConfigStart() override
    {
        LOG_INFO("BLE onConfigStart");

        // Prefer high throughput during config/setup, at the cost of high power consumption (for a few seconds)
        if (bleServer && isConnected()) {
            uint16_t conn_handle = nimbleBluetoothConnHandle.load();
            if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                requestHighThroughputConnection(conn_handle);
            }
        }
    }

    virtual void onConfigComplete() override
    {
        LOG_INFO("BLE onConfigComplete");

        // Switch to lower power consumption BLE connection params for steady-state use after config/setup is complete
        if (bleServer && isConnected()) {
            uint16_t conn_handle = nimbleBluetoothConnHandle.load();
            if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                requestLowerPowerConnection(conn_handle);
            }
        }
    }

    bool runOnceHasWorkToDo() { return runOnceHasWorkToPhone() || runOnceHasWorkFromPhone(); }

    bool runOnceHasWorkToPhone() { return onReadCallbackIsWaitingForData || runOnceToPhoneCanPreloadNextPacket(); }

    bool runOnceToPhoneCanPreloadNextPacket()
    {
        /*
         * PRELOADING getFromRadio RESPONSES:
         *
         * It's not safe to preload packets if we're in STATE_SEND_PACKETS, because there may be a while between the time we call
         * getFromRadio and when the client actually reads it. If the connection drops in that time, we might lose that packet
         * forever. In STATE_SEND_PACKETS, if we wait for onRead before we call getFromRadio, we minimize the time window where
         * the client might disconnect before completing the read.
         *
         * However, if we're in the setup states (sending config, nodeinfo, etc), it's safe and beneficial to preload packets into
         * toPhoneQueue because the client will just reconnect after a disconnect, losing nothing.
         */

        if (!isConnected()) {
            return false;
        } else if (isSendingPackets()) {
            // If we're in STATE_SEND_PACKETS, we must wait for onRead before calling getFromRadio.
            return false;
        } else {
            // In other states, we can preload as long as there's space in the toPhoneQueue.
            return toPhoneQueueSize < NIMBLE_BLUETOOTH_TO_PHONE_QUEUE_SIZE;
        }
    }

    void runOnceHandleToPhoneQueue()
    {
        // Stack buffer for getFromRadio packet
        uint8_t fromRadioBytes[meshtastic_FromRadio_size] = {0};
        size_t numBytes = 0;

        if (onReadCallbackIsWaitingForData || runOnceToPhoneCanPreloadNextPacket()) {
            numBytes = getFromRadio(fromRadioBytes);

            if (numBytes == 0) {
                /*
                  Client expected a read, but we have nothing to send.

                  In STATE_SEND_PACKETS, it is 100% OK to return a 0-byte response, as we expect clients to do read beyond
                  notifies regularly, to make sure they have nothing else to read.

                  In other states, this is fine **so long as we've already processed pending onWrites first**, because the client
                  may requesting wantConfig and immediately doing a read.
                */
            } else {
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
#ifdef DEBUG_NIMBLE_ON_READ_TIMING
                    LOG_DEBUG("BLE getFromRadio returned numBytes=%u, pushed toPhoneQueueSize=%u", numBytes,
                              toPhoneQueueSize.load());
#endif
                } else {
                    // Shouldn't happen because the onRead callback shouldn't be waiting if the queue is full!
                    LOG_ERROR("Shouldn't happen! Drop FromRadio packet, toPhoneQueue full (%u bytes)", numBytes);
                }
            }

            // Clear the onReadCallbackIsWaitingForData flag so onRead knows it can proceed.
            onReadCallbackIsWaitingForData = false; // only clear this flag AFTER the push
        }
    }

    bool runOnceHasWorkFromPhone() { return fromPhoneQueueSize > 0; }

    void runOnceHandleFromPhoneQueue()
    {
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

                // Safe decrement due to onDisconnect
                if (fromPhoneQueueSize > 0)
                    fromPhoneQueueSize--;
            }

            handleToRadio(val.data(), val.length());
        }
    }

    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum)
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        int currentNotifyCount = notifyCount.fetch_add(1);

        uint8_t cc = bleServer->getConnectedCount();

#ifdef DEBUG_NIMBLE_NOTIFY
        // This logging slows things down when there are lots of packets going to the phone, like initial connection:
        LOG_DEBUG("BLE notify(%d) fromNum: %d connections: %d", currentNotifyCount, fromRadioNum, cc);
#endif

        uint8_t val[4];
        put_le32(val, fromRadioNum);

        fromNumCharacteristic->setValue(val, sizeof(val));
#ifdef NIMBLE_TWO
        // NOTE: I don't have any NIMBLE_TWO devices, but this line makes me suspicious, and I suspect it needs to just be
        // notify().
        fromNumCharacteristic->notify(val, sizeof(val), BLE_HS_CONN_HANDLE_NONE);
#else
        fromNumCharacteristic->notify();
#endif
    }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() { return bleServer && bleServer->getConnectedCount() > 0; }

    void requestHighThroughputConnection(uint16_t conn_handle)
    {
        /* Request a lower-latency, higher-throughput BLE connection.

        This comes at the cost of higher power consumption, so we may want to only use this for initial setup, and then switch to
        a slower mode.

        See https://developer.apple.com/library/archive/qa/qa1931/_index.html for formulas to calculate values, iOS/macOS
        constraints, and recommendations. (Android doesn't have specific constraints, but seems to be compatible with the Apple
        recommendations.)

        Selected settings:
            minInterval (units of 1.25ms): 7.5ms = 6 (lower than the Apple recommended minimum, but allows faster when the client
        supports it.)
            maxInterval (units of 1.25ms): 15ms = 12
            latency: 0 (don't allow peripheral to skip any connection events)
            timeout (units of 10ms): 6 seconds = 600 (supervision timeout)

        These are intentionally aggressive to prioritize speed over power consumption, but are only used for a few seconds at
        setup. Not worth adjusting much.
        */
        LOG_INFO("BLE requestHighThroughputConnection");
        bleServer->updateConnParams(conn_handle, 6, 12, 0, 600);
    }

    void requestLowerPowerConnection(uint16_t conn_handle)
    {
        /* Request a lower power consumption (but higher latency, lower throughput) BLE connection.

        This is suitable for steady-state operation after initial setup is complete.

        See https://developer.apple.com/library/archive/qa/qa1931/_index.html for formulas to calculate values, iOS/macOS
        constraints, and recommendations. (Android doesn't have specific constraints, but seems to be compatible with the Apple
        recommendations.)

        Selected settings:
            minInterval (units of 1.25ms): 30ms = 24
            maxInterval (units of 1.25ms): 50ms = 40
            latency: 2 (allow peripheral to skip up to 2 consecutive connection events to save power)
            timeout (units of 10ms): 6 seconds = 600 (supervision timeout)

        There's an opportunity for tuning here if anyone wants to do some power measurements, but these should allow 10-20 packets
        per second.
        */
        LOG_INFO("BLE requestLowerPowerConnection");
        bleServer->updateConnParams(conn_handle, 24, 40, 2, 600);
    }
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

        int currentWriteCount = bluetoothPhoneAPI->writeCount.fetch_add(1);

#ifdef DEBUG_NIMBLE_ON_WRITE_TIMING
        int startMillis = millis();
        LOG_DEBUG("BLE onWrite(%d): start millis=%d", currentWriteCount, startMillis);
#endif

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

#ifdef DEBUG_NIMBLE_ON_WRITE_TIMING
                int finishMillis = millis();
                LOG_DEBUG("BLE onWrite(%d): append to fromPhoneQueue took %u ms. numBytes=%d", currentWriteCount,
                          finishMillis - startMillis, val.length());
#endif
            } else {
                LOG_WARN("BLE onWrite(%d): Drop ToRadio packet, fromPhoneQueue full (%u bytes)", currentWriteCount, val.length());
            }
        } else {
            LOG_DEBUG("BLE onWrite(%d): Drop duplicate ToRadio packet (%u bytes)", currentWriteCount, val.length());
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
        int startMillis = millis();

#ifdef DEBUG_NIMBLE_ON_READ_TIMING
        LOG_DEBUG("BLE onRead(%d): start millis=%d", currentReadCount, startMillis);
#endif

        // Is there a packet ready to go, or do we have to ask the main task to get one for us?
        if (bluetoothPhoneAPI->toPhoneQueueSize > 0) {
            // Note: the comparison above is safe without a mutex because we are the only method that *decreases*
            // toPhoneQueueSize. (It's okay if toPhoneQueueSize *increases* in the main task meanwhile.)

            // There's already a packet queued. Great! We don't need to wait for onReadCallbackIsWaitingForData.
#ifdef DEBUG_NIMBLE_ON_READ_TIMING
            LOG_DEBUG("BLE onRead(%d): packet already waiting, no need to set onReadCallbackIsWaitingForData", currentReadCount);
#endif
        } else {
            // Tell the main task that we'd like a packet.
            bluetoothPhoneAPI->onReadCallbackIsWaitingForData = true;

            // Wait for the main task to produce a packet for us, up to about 20 seconds.
            // It normally takes just a few milliseconds, but at initial startup, etc, the main task can get blocked for longer
            // doing various setup tasks.
            while (bluetoothPhoneAPI->onReadCallbackIsWaitingForData && tries < 4000) {
                // Schedule the main task runOnce to run ASAP.
                bluetoothPhoneAPI->setIntervalFromNow(0);
                concurrency::mainDelay.interrupt(); // wake up main loop if sleeping

                if (!bluetoothPhoneAPI->onReadCallbackIsWaitingForData) {
                    // we may be able to break even before a delay, if the call to interrupt woke up the main loop and it ran
                    // already
#ifdef DEBUG_NIMBLE_ON_READ_TIMING
                    LOG_DEBUG("BLE onRead(%d): broke before delay after %u ms, %d tries", currentReadCount,
                              millis() - startMillis, tries);
#endif
                    break;
                }

                // This delay happens in the NimBLE FreeRTOS task, which really can't do anything until we get a value back.
                // No harm in polling pretty frequently.
                delay(tries < 20 ? 1 : 5);
                tries++;

                if (tries == 4000) {
                    LOG_WARN(
                        "BLE onRead(%d): timeout waiting for data after %u ms, %d tries, giving up and returning 0-size response",
                        currentReadCount, millis() - startMillis, tries);
                }
            }
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

                // Safe decrement due to onDisconnect
                if (bluetoothPhoneAPI->toPhoneQueueSize > 0)
                    bluetoothPhoneAPI->toPhoneQueueSize--;
            } else {
                // nothing in the toPhoneQueue; that's fine, and we'll just have numBytes=0.
            }
        }

#ifdef DEBUG_NIMBLE_ON_READ_TIMING
        int finishMillis = millis();
        LOG_DEBUG("BLE onRead(%d): onReadCallbackIsWaitingForData took %u ms, %d tries. numBytes=%d", currentReadCount,
                  finishMillis - startMillis, tries, numBytes);
#endif

        pCharacteristic->setValue(fromRadioBytes, numBytes);

        // If we sent something, wake up the main loop if it's sleeping in case there are more packets ready to enqueue.
        if (numBytes != 0) {
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

        // Store the connection handle for future use
#ifdef NIMBLE_TWO
        nimbleBluetoothConnHandle = connInfo.getConnHandle();
#else
        nimbleBluetoothConnHandle = desc->conn_handle;
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

            { // scope for fromPhoneMutex mutex
                std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->fromPhoneMutex);
                bluetoothPhoneAPI->fromPhoneQueueSize = 0;
            }

            bluetoothPhoneAPI->onReadCallbackIsWaitingForData = false;
            { // scope for toPhoneMutex mutex
                std::lock_guard<std::mutex> guard(bluetoothPhoneAPI->toPhoneMutex);
                bluetoothPhoneAPI->toPhoneQueueSize = 0;
            }

            bluetoothPhoneAPI->readCount = 0;
            bluetoothPhoneAPI->notifyCount = 0;
            bluetoothPhoneAPI->writeCount = 0;
        }

        // Clear the last ToRadio packet buffer to avoid rejecting first packet from new connection
        memset(lastToRadio, 0, sizeof(lastToRadio));

        nimbleBluetoothConnHandle = BLE_HS_CONN_HANDLE_NONE; // BLE_HS_CONN_HANDLE_NONE means "no connection"

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
