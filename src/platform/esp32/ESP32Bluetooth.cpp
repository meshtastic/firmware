#ifdef USE_NEW_ESP32_BLUETOOTH

#include "configuration.h"
#include "ESP32Bluetooth.h"
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

class ESP32BluetoothToRadioCallback : public NimBLECharacteristicCallbacks 
{
    virtual void onWrite(NimBLECharacteristic *pCharacteristic) {
        DEBUG_MSG("To Radio onwrite\n");
        auto val = pCharacteristic->getValue();
        
        bluetoothPhoneAPI->handleToRadio(val.data(), val.length());
    }
};

class ESP32BluetoothFromRadioCallback : public NimBLECharacteristicCallbacks 
{
    virtual void onRead(NimBLECharacteristic *pCharacteristic) {
        DEBUG_MSG("From Radio onread\n");
        uint8_t fromRadioBytes[FromRadio_size];
        size_t numBytes = bluetoothPhoneAPI->getFromRadio(fromRadioBytes);

        std::string fromRadioByteString(fromRadioBytes, fromRadioBytes + numBytes);

        pCharacteristic->setValue(fromRadioByteString);
    }
};

class ESP32BluetoothServerCallback : public NimBLEServerCallbacks 
{
    virtual uint32_t onPassKeyRequest() {

        uint32_t passkey = 0;

        if (doublepressed > 0 && (doublepressed + (30 * 1000)) > millis()) {
            DEBUG_MSG("User has overridden passkey\n");
            passkey = defaultBLEPin;
        } else {
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
    }

    virtual void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc *desc)
     {
        DEBUG_MSG("BLE disconnect\n");
    }
};

static ESP32BluetoothToRadioCallback *toRadioCallbacks;
static ESP32BluetoothFromRadioCallback *fromRadioCallbacks;

void ESP32Bluetooth::shutdown()
{
    // Shutdown bluetooth for minimum power draw
    DEBUG_MSG("Disable bluetooth\n");
    //Bluefruit.Advertising.stop();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->reset();
    pAdvertising->stop();
}

bool ESP32Bluetooth::isActive()
{
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    return bleServer && (bleServer->getConnectedCount() > 0 || pAdvertising->isAdvertising());
}

void ESP32Bluetooth::setup()
{
    DEBUG_MSG("Initialise the ESP32 bluetooth module\n");

    NimBLEDevice::init(getDeviceName());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    bleServer = NimBLEDevice::createServer();
    
    ESP32BluetoothServerCallback *serverCallbacks = new ESP32BluetoothServerCallback();
    bleServer->setCallbacks(serverCallbacks, true);

    setupService();
    startAdvertising();
}

void ESP32Bluetooth::setupService() 
{
    NimBLEService *bleService = bleServer->createService(MESH_SERVICE_UUID);

    //define the characteristics that the app is looking for
    NimBLECharacteristic *ToRadioCharacteristic = bleService->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC);
    NimBLECharacteristic *FromRadioCharacteristic = bleService->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
    fromNumCharacteristic = bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
    
    bluetoothPhoneAPI = new BluetoothPhoneAPI();

    toRadioCallbacks = new ESP32BluetoothToRadioCallback();
    ToRadioCharacteristic->setCallbacks(toRadioCallbacks);

    fromRadioCallbacks = new ESP32BluetoothFromRadioCallback();
    FromRadioCharacteristic->setCallbacks(fromRadioCallbacks);

    bleService->start();
}

void ESP32Bluetooth::startAdvertising() 
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

void ESP32Bluetooth::clearBonds()
{
    DEBUG_MSG("Clearing bluetooth bonds!\n");
    NimBLEDevice::deleteAllBonds();
}

void clearNVS() 
{
    NimBLEDevice::deleteAllBonds();
    ESP.restart();
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

#endif
