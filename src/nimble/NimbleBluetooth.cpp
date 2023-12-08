#include "NimbleBluetooth.h"
#include "BluetoothCommon.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"
#include "sleep.h"
#include <NimBLEDevice.h>

NimBLECharacteristic *fromNumCharacteristic;
NimBLECharacteristic *BatteryCharacteristic;
NimBLEServer *bleServer;

static bool passkeyShowing;

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum)
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        LOG_INFO("BLE notify fromNum\n");

        uint8_t val[4];
        put_le32(val, fromRadioNum);

        fromNumCharacteristic->setValue(val, sizeof(val));
        fromNumCharacteristic->notify();
    }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() { return bleServer && bleServer->getConnectedCount() > 0; }
};

static BluetoothPhoneAPI *bluetoothPhoneAPI;
/**
 * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
 */

class NimbleBluetoothToRadioCallback : public NimBLECharacteristicCallbacks
{
    virtual void onWrite(NimBLECharacteristic *pCharacteristic)
    {
        LOG_INFO("To Radio onwrite\n");
        auto val = pCharacteristic->getValue();

        bluetoothPhoneAPI->handleToRadio(val.data(), val.length());
    }
};

class NimbleBluetoothFromRadioCallback : public NimBLECharacteristicCallbacks
{
    virtual void onRead(NimBLECharacteristic *pCharacteristic)
    {
        LOG_INFO("From Radio onread\n");
        uint8_t fromRadioBytes[meshtastic_FromRadio_size];
        size_t numBytes = bluetoothPhoneAPI->getFromRadio(fromRadioBytes);

        std::string fromRadioByteString(fromRadioBytes, fromRadioBytes + numBytes);

        pCharacteristic->setValue(fromRadioByteString);
    }
};

class NimbleBluetoothServerCallback : public NimBLEServerCallbacks
{
    virtual uint32_t onPassKeyRequest()
    {
        uint32_t passkey = config.bluetooth.fixed_pin;

        if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
            LOG_INFO("Using random passkey\n");
            // This is the passkey to be entered on peer - we pick a number >100,000 to ensure 6 digits
            passkey = random(100000, 999999);
        }
        LOG_INFO("*** Enter passkey %d on the peer side ***\n", passkey);

        powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
        screen->startBluetoothPinScreen(passkey);
        passkeyShowing = true;

        return passkey;
    }

    virtual void onAuthenticationComplete(ble_gap_conn_desc *desc)
    {
        LOG_INFO("BLE authentication complete\n");

        if (passkeyShowing) {
            passkeyShowing = false;
            screen->stopBluetoothPinScreen();
        }
    }

    virtual void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) { LOG_INFO("BLE disconnect\n"); }
};

static NimbleBluetoothToRadioCallback *toRadioCallbacks;
static NimbleBluetoothFromRadioCallback *fromRadioCallbacks;

void NimbleBluetooth::shutdown()
{
    // Shutdown bluetooth for minimum power draw
    LOG_INFO("Disable bluetooth\n");
    // Bluefruit.Advertising.stop();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->reset();
    pAdvertising->stop();
}

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
        return NimBLEDevice::getClientByID(handle)->getRssi();
    }
    return 0; // FIXME figure out where to source this
}

void NimbleBluetooth::setup()
{
    // Uncomment for testing
    // NimbleBluetooth::clearBonds();

    LOG_INFO("Initialise the NimBLE bluetooth module\n");

    NimBLEDevice::init(getDeviceName());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    if (config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    }
    bleServer = NimBLEDevice::createServer();

    NimbleBluetoothServerCallback *serverCallbacks = new NimbleBluetoothServerCallback();
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
        FromRadioCharacteristic = bleService->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
        fromNumCharacteristic = bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    } else {
        ToRadioCharacteristic = bleService->createCharacteristic(
            TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC);
        FromRadioCharacteristic = bleService->createCharacteristic(
            FROMRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
        fromNumCharacteristic =
            bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ |
                                                               NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
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

    NimBLE2904 *batteryLevelDescriptor = (NimBLE2904 *)BatteryCharacteristic->createDescriptor((uint16_t)0x2904);
    batteryLevelDescriptor->setFormat(NimBLE2904::FORMAT_UINT8);
    batteryLevelDescriptor->setNamespace(1);
    batteryLevelDescriptor->setUnit(0x27ad);

    batteryService->start();
}

void NimbleBluetooth::startAdvertising()
{
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->reset();
    pAdvertising->addServiceUUID(MESH_SERVICE_UUID);
    pAdvertising->addServiceUUID(NimBLEUUID((uint16_t)0x180f)); // 0x180F is the Battery Service
    pAdvertising->start(0);
}

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level)
{
    if ((config.bluetooth.enabled == true) && bleServer && nimbleBluetooth->isConnected()) {
        BatteryCharacteristic->setValue(&level, 1);
        BatteryCharacteristic->notify();
    }
}

void NimbleBluetooth::clearBonds()
{
    LOG_INFO("Clearing bluetooth bonds!\n");
    NimBLEDevice::deleteAllBonds();
}

void clearNVS()
{
    NimBLEDevice::deleteAllBonds();
#ifdef ARCH_ESP32
    ESP.restart();
#endif
}
