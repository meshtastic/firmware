#include "LinuxBluetooth.h"

#ifdef MESHTASTIC_LINUX_BLE

#include "BluetoothCommon.h"
#include "BluetoothStatus.h"
#include "PortduinoGlue.h"
#include "PowerFSM.h"
#include "SdbusCompat.h"
#include "concurrency/OSThread.h"
#include "main.h"
#include "mesh/NodeDB.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

/*
  BLE peripheral via bluetoothd, over the org.bluez D-Bus APIs.

  Threading model (a simplification of the one documented at length in
  src/nimble/NimbleBluetooth.cpp): the sdbus-c++ event loop runs in its own
  thread and executes every GATT/agent/signal callback, while ALL PhoneAPI calls
  happen on the main thread inside runOnce(), so the rest of the codebase stays
  effectively single-threaded.

  PHONE -> RADIO: WriteValue (event-loop thread) pushes into fromPhoneQueue and
  wakes the main loop; runOnce() pops and calls handleToRadio().

  RADIO -> PHONE: ReadValue (event-loop thread) parks the D-Bus reply in
  readResult and wakes the main loop; runOnce() first drains fromPhoneQueue
  (clients send a write and immediately read its answer, so writes must land
  first), then calls getFromRadio() and completes the parked reply from the main
  thread. Unlike NimBLE there is no busy-wait: a parked D-Bus method reply is
  exactly the deferred-response primitive that NimBLE's
  onReadCallbackIsWaitingForData flag emulates.

  fromNum/logRadio notifications are BlueZ PropertiesChanged("Value") signals,
  emitted from the main thread; sdbus-c++ serializes access to the connection
  internally.
*/

namespace
{

constexpr const char *kBluezService = "org.bluez";
constexpr const char *kBluezRootPath = "/";
constexpr const char *kBluezManagerPath = "/org/bluez";
constexpr const char *kGattAppPath = "/org/meshtastic/gatt";
constexpr const char *kServicePath = "/org/meshtastic/gatt/service0";
constexpr const char *kToRadioPath = "/org/meshtastic/gatt/service0/char0";
constexpr const char *kFromRadioPath = "/org/meshtastic/gatt/service0/char1";
constexpr const char *kFromNumPath = "/org/meshtastic/gatt/service0/char2";
constexpr const char *kLogRadioPath = "/org/meshtastic/gatt/service0/char3";
constexpr const char *kAdvertPath = "/org/meshtastic/advertisement0";
constexpr const char *kAgentPath = "/org/meshtastic/agent";

constexpr const char *kIfaceAdapter = "org.bluez.Adapter1";
constexpr const char *kIfaceDevice = "org.bluez.Device1";
constexpr const char *kIfaceGattManager = "org.bluez.GattManager1";
constexpr const char *kIfaceGattService = "org.bluez.GattService1";
constexpr const char *kIfaceGattChar = "org.bluez.GattCharacteristic1";
constexpr const char *kIfaceAdvManager = "org.bluez.LEAdvertisingManager1";
constexpr const char *kIfaceAdvert = "org.bluez.LEAdvertisement1";
constexpr const char *kIfaceAgentManager = "org.bluez.AgentManager1";
constexpr const char *kIfaceAgent = "org.bluez.Agent1";
constexpr const char *kIfaceObjectManager = "org.freedesktop.DBus.ObjectManager";
constexpr const char *kIfaceProperties = "org.freedesktop.DBus.Properties";

constexpr size_t kFromPhoneQueueDepth = 3;

using PropertyMap = std::map<std::string, sdbus::Variant>;
using InterfaceMap = std::map<std::string, PropertyMap>;
using ManagedObjects = std::map<sdbus::ObjectPath, InterfaceMap>;

uint16_t offsetOption(const PropertyMap &options)
{
    auto it = options.find("offset");
    return it == options.end() ? 0 : it->second.get<uint16_t>();
}

bool pinPairing()
{
    return config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
}

void publishStatus(meshtastic::BluetoothStatus::ConnectionState state)
{
    if (!bluetoothStatus)
        return;
    meshtastic::BluetoothStatus newStatus(state);
    bluetoothStatus->updateStatus(&newStatus);
}

} // namespace

struct LinuxBluetooth::Impl final : public PhoneAPI, public concurrency::OSThread {
    explicit Impl(std::string adapter)
        : concurrency::OSThread("LinuxBluetooth"), adapterPath("/org/bluez/" + adapter), adapterId(std::move(adapter))
    {
        api_type = TYPE_BLE;
    }

    const std::string adapterPath;
    const std::string adapterId;

    // D-Bus plumbing. Objects/proxies are created on the main thread in setup()
    // and only destroyed in doDeinit() after the event loop has been stopped.
    std::unique_ptr<sdbus::IConnection> conn;
    std::unique_ptr<sdbus::IObject> gattRoot, service, toRadioChar, fromRadioChar, fromNumChar, logRadioChar, advert, agent;
    std::unique_ptr<sdbus::IProxy> adapterProxy, bluezRootProxy, agentManagerProxy;

    std::atomic<bool> enabled{false};     // setup() finished successfully
    std::atomic<bool> advertising{false}; // advertisement currently registered
    std::atomic<bool> agentRegistered{false};
    std::atomic<bool> appRegistered{false};
    std::atomic<bool> draining{false}; // deinit in progress: fail reads fast

    // Connected/known devices, mutated on the event-loop thread, read from the
    // main thread.
    std::mutex devMutex;
    std::set<std::string> connectedDevices;
    std::map<std::string, std::unique_ptr<sdbus::IProxy>> deviceProxies;

    // PHONE -> RADIO queue (WriteValue -> handleToRadio)
    std::mutex fromPhoneMutex;
    std::atomic<size_t> fromPhoneQueueSize{0};
    std::array<std::array<uint8_t, MAX_TO_FROM_RADIO_SIZE>, kFromPhoneQueueDepth> fromPhoneQueue{};
    std::array<size_t, kFromPhoneQueueDepth> fromPhoneQueueLens{};
    // Duplicate-write suppression, event-loop thread only (see NimbleBluetooth's
    // lastToRadio)
    uint8_t lastToRadio[MAX_TO_FROM_RADIO_SIZE] = {0};
    size_t lastToRadioLen = 0;

    // RADIO -> PHONE parked read (ReadValue reply completed from the main thread)
    std::mutex readMutex;
    bool readPending = false;
    sdbus::Result<std::vector<uint8_t>> readResult;
    std::vector<uint8_t> lastFromRadio; // last packet served at offset 0, for blob-read tails

    // Notify state for fromNum and logRadio
    std::atomic<bool> fromNumNotifying{false};
    std::atomic<bool> logNotifying{false};
    std::mutex valueMutex; // guards the two cached Value buffers below
    std::vector<uint8_t> fromNumValue{0, 0, 0, 0};
    std::vector<uint8_t> logValue;

    std::atomic<bool> disconnectCleanupPending{false};
    std::atomic<bool> fixedPinWarned{false};

    // ---------------------------------------------------------------- PhoneAPI
    // glue

    bool checkIsConnected() override
    {
        std::lock_guard<std::mutex> guard(devMutex);
        return !connectedDevices.empty();
    }

    void onNowHasData(uint32_t fromRadioNum) override
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        {
            std::lock_guard<std::mutex> guard(valueMutex);
            fromNumValue = {static_cast<uint8_t>(fromRadioNum & 0xff), static_cast<uint8_t>((fromRadioNum >> 8) & 0xff),
                            static_cast<uint8_t>((fromRadioNum >> 16) & 0xff), static_cast<uint8_t>((fromRadioNum >> 24) & 0xff)};
        }
        if (fromNumNotifying && fromNumChar)
            sdbuscompat::emitPropertiesChanged(*fromNumChar, kIfaceGattChar, "Value");
    }

    int32_t runOnce() override
    {
        if (disconnectCleanupPending.exchange(false)) {
            close(); // reset the PhoneAPI session state on the main thread
            std::lock_guard<std::mutex> guard(fromPhoneMutex);
            fromPhoneQueueSize = 0;
        }

        // Writes before reads: clients send a ToRadio write and immediately read
        // the response, so the parked read must observe the write's effect.
        drainFromPhoneQueue();
        completeParkedRead();

        return INT32_MAX; // woken explicitly by the event-loop thread
    }

    void drainFromPhoneQueue()
    {
        while (fromPhoneQueueSize > 0) {
            uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
            size_t len;
            {
                std::lock_guard<std::mutex> guard(fromPhoneMutex);
                len = fromPhoneQueueLens[0];
                memcpy(buf, fromPhoneQueue[0].data(), len);
                size_t queued = fromPhoneQueueSize.load();
                for (size_t i = 1; i < queued; i++) {
                    memcpy(fromPhoneQueue[i - 1].data(), fromPhoneQueue[i].data(), fromPhoneQueueLens[i]);
                    fromPhoneQueueLens[i - 1] = fromPhoneQueueLens[i];
                }
                if (fromPhoneQueueSize > 0)
                    fromPhoneQueueSize--;
            }
            handleToRadio(buf, len);
        }
    }

    void completeParkedRead()
    {
        {
            std::lock_guard<std::mutex> lk(readMutex);
            if (!readPending)
                return;
        }

        // LOCK ORDER: getFromRadio() can emit a fromNum notify (a D-Bus call), and
        // the event-loop thread takes readMutex inside its dispatch lock, so
        // calling into sdbus while holding readMutex would deadlock. Fetch the
        // packet unlocked, then re-take the lock only to hand it to the parked
        // reply.
        uint8_t buf[meshtastic_FromRadio_size] = {0};
        size_t numBytes = getFromRadio(buf);

        std::unique_lock<std::mutex> lk(readMutex);
        if (!readPending) {
            // A disconnect raced us and already failed the read; the packet is lost,
            // which a disconnect implies anyway (PhoneAPI session state gets reset).
            return;
        }
        lastFromRadio.assign(buf, buf + numBytes);
        auto result = std::move(readResult);
        readPending = false;
        lk.unlock();

        // A zero-length reply is correct here: any pending write has already been
        // handled, and in STATE_SEND_PACKETS clients poll-read until they get 0
        // bytes.
        result.returnResults(std::vector<uint8_t>(buf, buf + numBytes));
    }

    // ------------------------------------------------- event-loop thread
    // callbacks

    void onToRadioWrite(std::vector<uint8_t> value, const PropertyMap &options)
    {
        if (offsetOption(options) != 0)
            throw sdbuscompat::dbusError("org.bluez.Error.NotSupported", "offset writes not supported");
        if (value.empty() || value.size() > MAX_TO_FROM_RADIO_SIZE)
            throw sdbuscompat::dbusError("org.bluez.Error.InvalidValueLength", "bad ToRadio length");

        if (value.size() == lastToRadioLen && memcmp(lastToRadio, value.data(), value.size()) == 0) {
            LOG_DEBUG("BLE drop duplicate ToRadio packet (%u bytes)", (unsigned)value.size());
            return;
        }
        if (fromPhoneQueueSize >= kFromPhoneQueueDepth) {
            // Push back on the client rather than dropping silently; it will retry.
            throw sdbuscompat::dbusError("org.bluez.Error.InProgress", "ToRadio queue full");
        }
        memcpy(lastToRadio, value.data(), value.size());
        lastToRadioLen = value.size();
        {
            std::lock_guard<std::mutex> guard(fromPhoneMutex);
            size_t at = fromPhoneQueueSize.load();
            memcpy(fromPhoneQueue[at].data(), value.data(), value.size());
            fromPhoneQueueLens[at] = value.size();
            fromPhoneQueueSize++;
        }
        wakeMainLoop();
    }

    void onFromRadioRead(sdbus::Result<std::vector<uint8_t>> &&result, const PropertyMap &options)
    {
        uint16_t offset = offsetOption(options);

        if (draining) {
            result.returnResults(std::vector<uint8_t>());
            return;
        }

        std::unique_lock<std::mutex> lk(readMutex);
        if (offset > 0) {
            // Blob-read continuation of the packet we served at offset 0; do not
            // consume a new packet from PhoneAPI.
            std::vector<uint8_t> tail;
            if (offset < lastFromRadio.size())
                tail.assign(lastFromRadio.begin() + offset, lastFromRadio.end());
            lk.unlock();
            result.returnResults(tail);
            return;
        }
        if (readPending) {
            // Shouldn't happen (ATT serializes reads), but never strand a reply.
            auto stale = std::move(readResult);
            readPending = false;
            stale.returnError(sdbuscompat::dbusError("org.bluez.Error.Failed", "superseded by a newer read"));
        }
        readResult = std::move(result);
        readPending = true;
        lk.unlock();
        wakeMainLoop();
    }

    std::vector<uint8_t> onFromNumRead(const PropertyMap &)
    {
        std::lock_guard<std::mutex> guard(valueMutex);
        return fromNumValue;
    }

    std::vector<uint8_t> onLogRadioRead(const PropertyMap &)
    {
        std::lock_guard<std::mutex> guard(valueMutex);
        return logValue;
    }

    void wakeMainLoop()
    {
        setIntervalFromNow(0);
        concurrency::mainDelay.interrupt();
    }

    // ------------------------------------------------------------ device
    // tracking

    void trackDevice(const std::string &path, bool connectedNow)
    {
        // LOCK ORDER: devMutex must stay a leaf on the main thread (the event-loop
        // thread takes it inside its dispatch lock), so the proxy - a D-Bus
        // operation - is created outside the lock.
        {
            std::lock_guard<std::mutex> guard(devMutex);
            if (connectedNow)
                connectedDevices.insert(path);
            if (deviceProxies.count(path) != 0)
                return;
        }
        auto proxy = sdbuscompat::makeProxy(*conn, kBluezService, path);
        proxy->uponSignal("PropertiesChanged")
            .onInterface(kIfaceProperties)
            .call([this, path](const std::string &iface, const PropertyMap &changed, const std::vector<std::string> &) {
                if (iface != kIfaceDevice)
                    return;
                auto it = changed.find("Connected");
                if (it != changed.end())
                    onDeviceConnectedChanged(path, it->second.get<bool>());
            });
        sdbuscompat::finishProxy(*proxy);
        std::lock_guard<std::mutex> guard(devMutex);
        if (deviceProxies.count(path) == 0)
            deviceProxies[path] = std::move(proxy);
    }

    void onDeviceConnectedChanged(const std::string &path, bool connected)
    {
        bool lastGone = false;
        {
            std::lock_guard<std::mutex> guard(devMutex);
            if (connected)
                connectedDevices.insert(path);
            else
                connectedDevices.erase(path);
            lastGone = connectedDevices.empty();
        }
        LOG_INFO("BLE %s %s", connected ? "connect" : "disconnect", path.c_str());

        if (connected) {
            publishStatus(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
        } else if (lastGone) {
            publishStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
            lastToRadioLen = 0; // event-loop thread owns this buffer
            failParkedRead();
            disconnectCleanupPending = true;
            wakeMainLoop();
        }
    }

    void failParkedRead()
    {
        std::unique_lock<std::mutex> lk(readMutex);
        if (!readPending)
            return;
        auto result = std::move(readResult);
        readPending = false;
        lk.unlock();
        result.returnResults(std::vector<uint8_t>());
    }

    void onInterfacesAdded(const sdbus::ObjectPath &path, const InterfaceMap &interfaces)
    {
        auto it = interfaces.find(kIfaceDevice);
        if (it == interfaces.end() || !belongsToAdapter(path))
            return;
        bool connectedNow = false;
        auto prop = it->second.find("Connected");
        if (prop != it->second.end())
            connectedNow = prop->second.get<bool>();
        trackDevice(path, connectedNow);
        if (connectedNow)
            publishStatus(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
    }

    void onInterfacesRemoved(const sdbus::ObjectPath &path, const std::vector<std::string> &interfaces)
    {
        if (std::find(interfaces.begin(), interfaces.end(), kIfaceDevice) == interfaces.end())
            return;
        onDeviceConnectedChanged(path, false);
        std::lock_guard<std::mutex> guard(devMutex);
        deviceProxies.erase(path);
    }

    bool belongsToAdapter(const std::string &path) const { return path.rfind(adapterPath + "/", 0) == 0; }

    // --------------------------------------------------------------------
    // pairing

    void onDisplayPasskey(const sdbus::ObjectPath &device, uint32_t passkey, uint16_t entered)
    {
        if (entered > 0)
            return; // progress updates while the peer types; the code is already
                    // showing
        char formatted[8];
        snprintf(formatted, sizeof(formatted), "%06u", passkey);
        LOG_INFO("BLE pairing request from %s: enter passkey %s", device.c_str(), formatted);
        powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
        if (bluetoothStatus) {
            meshtastic::BluetoothStatus newStatus{std::string(formatted)};
            bluetoothStatus->updateStatus(&newStatus);
        }
    }

    // ----------------------------------------------------------------- lifecycle

    void doSetup()
    {
        if (enabled)
            return;
        try {
            conn = sdbus::createSystemBusConnection();
            conn->enterEventLoopAsync();

            bluezRootProxy = sdbuscompat::makeProxy(*conn, kBluezService, kBluezRootPath);
            bluezRootProxy->uponSignal("InterfacesAdded")
                .onInterface(kIfaceObjectManager)
                .call([this](const sdbus::ObjectPath &path, const InterfaceMap &ifaces) { onInterfacesAdded(path, ifaces); });
            bluezRootProxy->uponSignal("InterfacesRemoved")
                .onInterface(kIfaceObjectManager)
                .call([this](const sdbus::ObjectPath &path, const std::vector<std::string> &ifaces) {
                    onInterfacesRemoved(path, ifaces);
                });
            sdbuscompat::finishProxy(*bluezRootProxy);

            ManagedObjects objects;
            bluezRootProxy->callMethod("GetManagedObjects").onInterface(kIfaceObjectManager).storeResultsTo(objects);
            if (objects.find(sdbus::ObjectPath{adapterPath}) == objects.end()) {
                LOG_ERROR("BLE adapter %s not found in BlueZ; Bluetooth stays off", adapterId.c_str());
                teardownBus();
                return;
            }

            adapterProxy = sdbuscompat::makeProxy(*conn, kBluezService, adapterPath);
            sdbuscompat::finishProxy(*adapterProxy);
            adapterProxy->setProperty("Powered").onInterface(kIfaceAdapter).toValue(true);
            try {
                adapterProxy->setProperty("Alias").onInterface(kIfaceAdapter).toValue(std::string(getDeviceName()));
                adapterProxy->setProperty("Pairable").onInterface(kIfaceAdapter).toValue(true);
            } catch (const sdbus::Error &e) {
                LOG_WARN("BLE could not set adapter alias/pairable: %s", e.what());
            }

            if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN &&
                !fixedPinWarned.exchange(true))
                LOG_WARN("BLE fixed PIN is not supported by BlueZ; a random passkey "
                         "will be shown instead");

            exportGattTree();
            exportAgent();
            exportAdvertisement();

            registerApplication();
            registerAgent();

            // Track already-known devices (and any live connection) before
            // advertising.
            for (const auto &entry : objects) {
                auto dev = entry.second.find(kIfaceDevice);
                if (dev == entry.second.end() || !belongsToAdapter(entry.first))
                    continue;
                bool connectedNow = false;
                auto prop = dev->second.find("Connected");
                if (prop != dev->second.end())
                    connectedNow = prop->second.get<bool>();
                trackDevice(entry.first, connectedNow);
            }

            enabled = true;
            registerAdvertisement();
            LOG_INFO("BLE ready on %s as '%s' (%s pairing)", adapterId.c_str(), getDeviceName(),
                     pinPairing() ? "passkey" : "just-works");
        } catch (const sdbus::Error &e) {
            LOG_ERROR("BLE setup failed (%s: %s); Bluetooth stays off", e.getName().c_str(), e.getMessage().c_str());
            teardownBus();
        }
    }

    void exportGattTree()
    {
        gattRoot = sdbuscompat::makeObject(*conn, kGattAppPath);
        gattRoot->addObjectManager();

        service = sdbuscompat::makeObject(*conn, kServicePath);
        sdbuscompat::addVTable(*service, kIfaceGattService,
                               sdbuscompat::property("UUID", [] { return std::string(MESH_SERVICE_UUID); }),
                               sdbuscompat::property("Primary", [] { return true; }));

        const bool pin = pinPairing();
        const std::vector<std::string> readFlags{pin ? "encrypt-authenticated-read" : "read"};
        const std::vector<std::string> writeFlags{pin ? "encrypt-authenticated-write" : "write"};
        std::vector<std::string> notifyFlags = readFlags;
        notifyFlags.push_back("notify");

        toRadioChar = sdbuscompat::makeObject(*conn, kToRadioPath);
        sdbuscompat::addVTable(
            *toRadioChar, kIfaceGattChar,
            sdbuscompat::method("WriteValue", [this](std::vector<uint8_t> value,
                                                     PropertyMap options) { onToRadioWrite(std::move(value), options); }),
            sdbuscompat::property("UUID", [] { return std::string(TORADIO_UUID); }),
            sdbuscompat::property("Service", [] { return sdbus::ObjectPath{kServicePath}; }),
            sdbuscompat::property("Flags", [writeFlags] { return writeFlags; }));

        fromRadioChar = sdbuscompat::makeObject(*conn, kFromRadioPath);
        sdbuscompat::addVTable(
            *fromRadioChar, kIfaceGattChar,
            sdbuscompat::method("ReadValue", [this](sdbus::Result<std::vector<uint8_t>> result,
                                                    PropertyMap options) { onFromRadioRead(std::move(result), options); }),
            sdbuscompat::property("UUID", [] { return std::string(FROMRADIO_UUID); }),
            sdbuscompat::property("Service", [] { return sdbus::ObjectPath{kServicePath}; }),
            sdbuscompat::property("Flags", [readFlags] { return readFlags; }));

        fromNumChar = sdbuscompat::makeObject(*conn, kFromNumPath);
        sdbuscompat::addVTable(*fromNumChar, kIfaceGattChar,
                               sdbuscompat::method("ReadValue", [this](PropertyMap options) { return onFromNumRead(options); }),
                               sdbuscompat::method("StartNotify", [this] { fromNumNotifying = true; }),
                               sdbuscompat::method("StopNotify", [this] { fromNumNotifying = false; }),
                               sdbuscompat::property("UUID", [] { return std::string(FROMNUM_UUID); }),
                               sdbuscompat::property("Service", [] { return sdbus::ObjectPath{kServicePath}; }),
                               sdbuscompat::property("Flags", [notifyFlags] { return notifyFlags; }),
                               sdbuscompat::property("Notifying", [this] { return fromNumNotifying.load(); }),
                               sdbuscompat::property("Value", [this] {
                                   std::lock_guard<std::mutex> guard(valueMutex);
                                   return fromNumValue;
                               }));

        logRadioChar = sdbuscompat::makeObject(*conn, kLogRadioPath);
        sdbuscompat::addVTable(*logRadioChar, kIfaceGattChar,
                               sdbuscompat::method("ReadValue", [this](PropertyMap options) { return onLogRadioRead(options); }),
                               sdbuscompat::method("StartNotify", [this] { logNotifying = true; }),
                               sdbuscompat::method("StopNotify", [this] { logNotifying = false; }),
                               sdbuscompat::property("UUID", [] { return std::string(LOGRADIO_UUID); }),
                               sdbuscompat::property("Service", [] { return sdbus::ObjectPath{kServicePath}; }),
                               sdbuscompat::property("Flags", [notifyFlags] { return notifyFlags; }),
                               sdbuscompat::property("Notifying", [this] { return logNotifying.load(); }),
                               sdbuscompat::property("Value", [this] {
                                   std::lock_guard<std::mutex> guard(valueMutex);
                                   return logValue;
                               }));
    }

    void exportAgent()
    {
        agent = sdbuscompat::makeObject(*conn, kAgentPath);
        sdbuscompat::addVTable(
            *agent, kIfaceAgent, sdbuscompat::method("Release", [] {}),
            sdbuscompat::method("RequestPinCode",
                                [](sdbus::ObjectPath) -> std::string {
                                    throw sdbuscompat::dbusError("org.bluez.Error.Rejected", "display-only device");
                                }),
            sdbuscompat::method("DisplayPinCode", [](sdbus::ObjectPath, std::string) {}),
            sdbuscompat::method("RequestPasskey",
                                [](sdbus::ObjectPath) -> uint32_t {
                                    throw sdbuscompat::dbusError("org.bluez.Error.Rejected", "display-only device");
                                }),
            sdbuscompat::method("DisplayPasskey", [this](sdbus::ObjectPath device, uint32_t passkey,
                                                         uint16_t entered) { onDisplayPasskey(device, passkey, entered); }),
            sdbuscompat::method("RequestConfirmation",
                                [](sdbus::ObjectPath, uint32_t) {
                                    // Never expected with our capabilities; with a
                                    // PIN mode configured, silently confirming would
                                    // bypass MITM.
                                    if (pinPairing())
                                        throw sdbuscompat::dbusError("org.bluez.Error.Rejected", "passkey required");
                                }),
            sdbuscompat::method("RequestAuthorization", [](sdbus::ObjectPath) {}),
            sdbuscompat::method("AuthorizeService", [](sdbus::ObjectPath, std::string) {}), sdbuscompat::method("Cancel", [this] {
                LOG_INFO("BLE pairing canceled");
                if (!checkIsConnected())
                    publishStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
            }));
    }

    void exportAdvertisement()
    {
        advert = sdbuscompat::makeObject(*conn, kAdvertPath);
        sdbuscompat::addVTable(*advert, kIfaceAdvert, sdbuscompat::method("Release", [] {}),
                               sdbuscompat::property("Type", [] { return std::string("peripheral"); }),
                               sdbuscompat::property("ServiceUUIDs", [] { return std::vector<std::string>{MESH_SERVICE_UUID}; }),
                               sdbuscompat::property("LocalName", [] { return std::string(getDeviceName()); }),
                               sdbuscompat::property("Discoverable", [] { return true; }));
    }

    void registerApplication()
    {
        // Must be async: before replying, bluetoothd calls GetManagedObjects back
        // on our connection, and a synchronous call would sit on the connection
        // until timeout.
        adapterProxy->callMethodAsync("RegisterApplication")
            .onInterface(kIfaceGattManager)
            .withArguments(sdbus::ObjectPath{kGattAppPath}, PropertyMap{})
            .uponReplyInvoke([](sdbuscompat::AsyncError error) {
                if (sdbuscompat::asyncFailed(error))
                    LOG_ERROR("BLE GATT registration failed: %s", sdbuscompat::asyncErrorMessage(error).c_str());
                else
                    LOG_INFO("BLE GATT service registered");
            });
        appRegistered = true;
    }

    void registerAgent()
    {
        agentManagerProxy = sdbuscompat::makeProxy(*conn, kBluezService, kBluezManagerPath);
        sdbuscompat::finishProxy(*agentManagerProxy);
        const std::string capability = pinPairing() ? "DisplayOnly" : "NoInputNoOutput";
        agentManagerProxy->callMethod("RegisterAgent")
            .onInterface(kIfaceAgentManager)
            .withArguments(sdbus::ObjectPath{kAgentPath}, capability);
        agentRegistered = true;
        try {
            // Make our agent answer this host's pairing requests while BLE is
            // enabled; without this, headless systems have no agent at all and
            // pairing fails.
            agentManagerProxy->callMethod("RequestDefaultAgent")
                .onInterface(kIfaceAgentManager)
                .withArguments(sdbus::ObjectPath{kAgentPath});
        } catch (const sdbus::Error &e) {
            LOG_WARN("BLE could not become default pairing agent: %s", e.getMessage().c_str());
        }
    }

    void registerAdvertisement()
    {
        if (!enabled || advertising)
            return;
        advertising = true;
        // Async for the same reason as RegisterApplication: bluetoothd reads our
        // advertisement object's properties before replying.
        adapterProxy->callMethodAsync("RegisterAdvertisement")
            .onInterface(kIfaceAdvManager)
            .withArguments(sdbus::ObjectPath{kAdvertPath}, PropertyMap{})
            .uponReplyInvoke([this](sdbuscompat::AsyncError error) {
                if (sdbuscompat::asyncFailed(error)) {
                    advertising = false;
                    LOG_ERROR("BLE could not start advertising: %s", sdbuscompat::asyncErrorMessage(error).c_str());
                } else {
                    LOG_INFO("BLE advertising as '%s'", getDeviceName());
                }
            });
    }

    void unregisterAdvertisement()
    {
        if (!enabled || !advertising)
            return;
        try {
            adapterProxy->callMethod("UnregisterAdvertisement")
                .onInterface(kIfaceAdvManager)
                .withArguments(sdbus::ObjectPath{kAdvertPath});
        } catch (const sdbus::Error &e) {
            LOG_WARN("BLE could not stop advertising: %s", e.getMessage().c_str());
        }
        advertising = false;
        LOG_INFO("BLE advertising stopped");
    }

    void doDeinit()
    {
        if (!conn)
            return;
        draining = true;
        failParkedRead();
        unregisterAdvertisement();
        if (agentRegistered.exchange(false)) {
            try {
                agentManagerProxy->callMethod("UnregisterAgent")
                    .onInterface(kIfaceAgentManager)
                    .withArguments(sdbus::ObjectPath{kAgentPath});
            } catch (const sdbus::Error &) {
            }
        }
        if (appRegistered.exchange(false)) {
            try {
                adapterProxy->callMethod("UnregisterApplication")
                    .onInterface(kIfaceGattManager)
                    .withArguments(sdbus::ObjectPath{kGattAppPath});
            } catch (const sdbus::Error &) {
            }
        }
        enabled = false;
        teardownBus();
        publishStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
        LOG_INFO("BLE disabled");
    }

    void teardownBus()
    {
        if (conn)
            conn->leaveEventLoop();
        {
            // Move the proxies out so their (D-Bus) destruction happens without
            // holding devMutex - the main thread must never call into sdbus under
            // that lock.
            std::map<std::string, std::unique_ptr<sdbus::IProxy>> doomed;
            std::lock_guard<std::mutex> guard(devMutex);
            doomed.swap(deviceProxies);
            connectedDevices.clear();
        }
        agentManagerProxy.reset();
        adapterProxy.reset();
        bluezRootProxy.reset();
        gattRoot.reset();
        service.reset();
        toRadioChar.reset();
        fromRadioChar.reset();
        fromNumChar.reset();
        logRadioChar.reset();
        advert.reset();
        agent.reset();
        conn.reset();
        draining = false;
        enabled = false;
        advertising = false;
        agentRegistered = false;
        appRegistered = false;
    }

    void doClearBonds()
    {
        if (!conn) {
            LOG_WARN("BLE clearBonds: Bluetooth is not running, nothing to clear");
            return;
        }
        try {
            ManagedObjects objects;
            bluezRootProxy->callMethod("GetManagedObjects").onInterface(kIfaceObjectManager).storeResultsTo(objects);
            for (const auto &entry : objects) {
                auto dev = entry.second.find(kIfaceDevice);
                if (dev == entry.second.end() || !belongsToAdapter(entry.first))
                    continue;
                auto paired = dev->second.find("Paired");
                if (paired == dev->second.end() || !paired->second.get<bool>())
                    continue;
                LOG_INFO("BLE removing bond %s", entry.first.c_str());
                try {
                    adapterProxy->callMethod("RemoveDevice").onInterface(kIfaceAdapter).withArguments(entry.first);
                } catch (const sdbus::Error &e) {
                    LOG_WARN("BLE could not remove %s: %s", entry.first.c_str(), e.getMessage().c_str());
                }
            }
        } catch (const sdbus::Error &e) {
            LOG_ERROR("BLE clearBonds failed: %s", e.getMessage().c_str());
        }
    }

    void doSendLog(const uint8_t *logMessage, size_t length)
    {
        // CAUTION: called from the logger; never LOG_* in here (infinite
        // recursion).
        if (!enabled || !logNotifying || !logRadioChar || length == 0)
            return;
        if (length > MAX_TO_FROM_RADIO_SIZE)
            length = MAX_TO_FROM_RADIO_SIZE;
        {
            std::lock_guard<std::mutex> guard(valueMutex);
            logValue.assign(logMessage, logMessage + length);
        }
        sdbuscompat::emitPropertiesChanged(*logRadioChar, kIfaceGattChar, "Value");
    }
};

LinuxBluetooth::LinuxBluetooth() : impl(new Impl(portduino_config.bluetooth_adapter)) {}

LinuxBluetooth::~LinuxBluetooth()
{
    if (impl)
        impl->doDeinit();
}

void LinuxBluetooth::setup()
{
    impl->doSetup();
}

void LinuxBluetooth::shutdown()
{
    impl->unregisterAdvertisement();
}

void LinuxBluetooth::resumeAdvertising()
{
    impl->registerAdvertisement();
}

void LinuxBluetooth::deinit()
{
    impl->doDeinit();
}

void LinuxBluetooth::clearBonds()
{
    impl->doClearBonds();
}

bool LinuxBluetooth::isConnected()
{
    return impl->checkIsConnected();
}

int LinuxBluetooth::getRssi()
{
    return 0; // not exposed by BlueZ for connected peers; same answer as NRF52
}

bool LinuxBluetooth::isEnabled()
{
    return impl->enabled;
}

void LinuxBluetooth::sendLog(const uint8_t *logMessage, size_t length)
{
    impl->doSendLog(logMessage, length);
}

#endif // MESHTASTIC_LINUX_BLE
