#include "configuration.h"
#include "NimbleBluetooth.h"
#include "BluetoothCommon.h"
#include "PowerFSM.h"
#include "sleep.h"
#include "main.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"
#include <NimBLEDevice.h>

NimBLECharacteristic *fromNumCharacteristic;
NimBLEServer *bleServer;

static bool passkeyShowing;
static uint32_t doublepressed;

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) 
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        DEBUG_MSG("BLE notify fromNum\n");
        
        uint8_t val[4];
        put_le32(val, fromRadioNum);

        fromNumCharacteristic->setValue(val, sizeof(val));
        fromNumCharacteristic->notify();
    }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() 
    {
        return bleServer && bleServer->getConnectedCount() > 0;
    }
};

static BluetoothPhoneAPI *bluetoothPhoneAPI;
/**
 * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
 */

class NimbleBluetoothToRadioCallback : public NimBLECharacteristicCallbacks 
{
    virtual void onWrite(NimBLECharacteristic *pCharacteristic) {
        DEBUG_MSG("To Radio onwrite\n");
        auto val = pCharacteristic->getValue();
        
        bluetoothPhoneAPI->handleToRadio(val.data(), val.length());
    }
};

class NimbleBluetoothFromRadioCallback : public NimBLECharacteristicCallbacks 
{
    virtual void onRead(NimBLECharacteristic *pCharacteristic) {
        DEBUG_MSG("From Radio onread\n");
        uint8_t fromRadioBytes[FromRadio_size];
        size_t numBytes = bluetoothPhoneAPI->getFromRadio(fromRadioBytes);

        std::string fromRadioByteString(fromRadioBytes, fromRadioBytes + numBytes);

        pCharacteristic->setValue(fromRadioByteString);
    }
};

class NimbleBluetoothServerCallback : public NimBLEServerCallbacks 
{
    virtual uint32_t onPassKeyRequest() {
        uint32_t passkey = config.bluetooth.fixed_pin;

        if (doublepressed > 0 && (doublepressed + (30 * 1000)) > millis()) {
            DEBUG_MSG("User has set BLE pairing mode to fixed-pin\n");
            config.bluetooth.mode = Config_BluetoothConfig_PairingMode_FixedPin;
            nodeDB.saveToDisk();
        } else if (config.bluetooth.mode == Config_BluetoothConfig_PairingMode_RandomPin) {
            DEBUG_MSG("Using random passkey\n");
            // This is the passkey to be entered on peer - we pick a number >100,000 to ensure 6 digits
            passkey = random(100000, 999999); 
        }
        DEBUG_MSG("*** Enter passkey %d on the peer side ***\n", passkey);

        powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
        screen->startBluetoothPinScreen(passkey);
        passkeyShowing = true;

        return passkey;
    }

    virtual void onAuthenticationComplete(ble_gap_conn_desc *desc) 
    {
        DEBUG_MSG("BLE authentication complete\n");

        if (passkeyShowing) {
            passkeyShowing = false;
            screen->stopBluetoothPinScreen();
        }
        // bluetoothPhoneAPI->setInitialState();
    }

    virtual void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc *desc)
     {
        DEBUG_MSG("BLE disconnect\n");
    }
};

static NimbleBluetoothToRadioCallback *toRadioCallbacks;
static NimbleBluetoothFromRadioCallback *fromRadioCallbacks;

void NimbleBluetooth::shutdown()
{
    // Shutdown bluetooth for minimum power draw
    DEBUG_MSG("Disable bluetooth\n");
    //Bluefruit.Advertising.stop();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->reset();
    pAdvertising->stop();
}

bool NimbleBluetooth::isActive()
{
    return bleServer;
}

void NimbleBluetooth::setup()
{
    // Uncomment for testing
    // NimbleBluetooth::clearBonds();

    DEBUG_MSG("Initialise the NimBLE bluetooth module\n");

    NimBLEDevice::init(getDeviceName());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    if (config.bluetooth.mode != Config_BluetoothConfig_PairingMode_NoPin) {
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
    if (config.bluetooth.mode == Config_BluetoothConfig_PairingMode_NoPin) {
        ToRadioCharacteristic = bleService->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE);
        FromRadioCharacteristic = bleService->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
        fromNumCharacteristic = bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    }
    else {
        ToRadioCharacteristic = bleService->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC);
        FromRadioCharacteristic = bleService->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
        fromNumCharacteristic = bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
    }
    bluetoothPhoneAPI = new BluetoothPhoneAPI();

    toRadioCallbacks = new NimbleBluetoothToRadioCallback();
    ToRadioCharacteristic->setCallbacks(toRadioCallbacks);

    fromRadioCallbacks = new NimbleBluetoothFromRadioCallback();
    FromRadioCharacteristic->setCallbacks(fromRadioCallbacks);

    bleService->start();
}

void NimbleBluetooth::startAdvertising() 
{
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->reset();
    pAdvertising->addServiceUUID(MESH_SERVICE_UUID);
    pAdvertising->start(0);
}

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level)
{
    //blebas.write(level);
}

void NimbleBluetooth::clearBonds()
{
    DEBUG_MSG("Clearing bluetooth bonds!\n");
    NimBLEDevice::deleteAllBonds();
}

void clearNVS() 
{
    NimBLEDevice::deleteAllBonds();
#ifdef ARCH_ESP32
    ESP.restart();
#endif
}

void disablePin() 
{
    DEBUG_MSG("User Override, disabling bluetooth pin requirement\n");
    // keep track of when it was pressed, so we know it was within X seconds

    // Flash the LED
    setLed(true);
    delay(100);
    setLed(false);
    delay(100);
    setLed(true);
    delay(100);
    setLed(false);
    delay(100);
    setLed(true);
    delay(100);
    setLed(false);

    doublepressed = millis();
}
