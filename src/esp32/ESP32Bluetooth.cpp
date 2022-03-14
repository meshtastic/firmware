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

//static BLEService meshBleService = BLEService(BLEUuid(MESH_SERVICE_UUID_16));
//static BLECharacteristic fromNum = BLECharacteristic(BLEUuid(FROMNUM_UUID_16));
//static BLECharacteristic fromRadio = BLECharacteristic(BLEUuid(FROMRADIO_UUID_16));
//static BLECharacteristic toRadio = BLECharacteristic(BLEUuid(TORADIO_UUID_16));

//static BLEDis bledis; // DIS (Device Information Service) helper class instance
//static BLEBas blebas; // BAS (Battery Service) helper class instance
//static BLEDfu bledfu; // DFU software update helper service

// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in
// proccess at once
// static uint8_t trBytes[_max(_max(_max(_max(ToRadio_size, RadioConfig_size), User_size), MyNodeInfo_size), FromRadio_size)];
static uint8_t fromRadioBytes[FromRadio_size];
static uint8_t toRadioBytes[ToRadio_size];

static bool bleConnected;

NimBLECharacteristic *FromNumCharacteristic;
NimBLEServer *bleServer;

static bool passkeyShowing;
static uint32_t doublepressed;

/**
 * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
 */
void BluetoothPhoneAPI::onNowHasData(uint32_t fromRadioNum)
{
    PhoneAPI::onNowHasData(fromRadioNum);

    DEBUG_MSG("BLE notify fromNum\n");
    //fromNum.notify32(fromRadioNum);
    
    uint8_t val[4];
    put_le32(val, fromRadioNum);

    std::string fromNumByteString(&val[0], &val[0] + sizeof(val));

    FromNumCharacteristic->setValue(fromNumByteString);
    FromNumCharacteristic->notify();
}

/// Check the current underlying physical link to see if the client is currently connected
bool BluetoothPhoneAPI::checkIsConnected() {
    if (bleServer && bleServer->getConnectedCount() > 0) {
        return true;
    }
    return false;
}

PhoneAPI *bluetoothPhoneAPI;

class ESP32BluetoothToRadioCallback : public NimBLECharacteristicCallbacks {
    virtual void onWrite(NimBLECharacteristic *pCharacteristic) {
        DEBUG_MSG("To Radio onwrite\n");
        auto valueString = pCharacteristic->getValue();
        
        bluetoothPhoneAPI->handleToRadio(reinterpret_cast<const uint8_t*>(&valueString[0]), pCharacteristic->getDataLength());
    }
};

class ESP32BluetoothFromRadioCallback : public NimBLECharacteristicCallbacks {
    virtual void onRead(NimBLECharacteristic *pCharacteristic) {
        DEBUG_MSG("From Radio onread\n");
        size_t numBytes = bluetoothPhoneAPI->getFromRadio(fromRadioBytes);

        std::string fromRadioByteString(fromRadioBytes, fromRadioBytes + numBytes);

        pCharacteristic->setValue(fromRadioByteString);
    }
};

class ESP32BluetoothServerCallback : public NimBLEServerCallbacks {
    virtual uint32_t onPassKeyRequest() {

        uint32_t passkey = 0;

        if (doublepressed > 0 && (doublepressed + (30 * 1000)) > millis()) {
            DEBUG_MSG("User has overridden passkey\n");
            passkey = defaultBLEPin;
        } else {
            DEBUG_MSG("Using random passkey\n");
            passkey = random(
                100000, 999999); // This is the passkey to be entered on peer - we pick a number >100,000 to ensure 6 digits
        }
        DEBUG_MSG("*** Enter passkey %d on the peer side ***\n", passkey);

        powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
        screen->startBluetoothPinScreen(passkey);
        passkeyShowing = true;

        return passkey;
    }

    virtual void onAuthenticationComplete(ble_gap_conn_desc *desc) {
        DEBUG_MSG("BLE authentication complete\n");

        if (passkeyShowing) {
            passkeyShowing = false;
            screen->stopBluetoothPinScreen();
        }
    }
};


static ESP32BluetoothToRadioCallback *toRadioCallbacks;
static ESP32BluetoothFromRadioCallback *fromRadioCallbacks;

void ESP32Bluetooth::shutdown()
{
    // Shutdown bluetooth for minimum power draw
    DEBUG_MSG("Disable bluetooth\n");
    //Bluefruit.Advertising.stop();
}

void ESP32Bluetooth::setup()
{
    // Initialise the Bluefruit module
    DEBUG_MSG("Initialise the ESP32 bluetooth module\n");
    //Bluefruit.autoConnLed(false);
    //Bluefruit.begin();

    // Set the advertised device name (keep it short!)
    //Bluefruit.setName(getDeviceName());

    // Set the connect/disconnect callback handlers
    //Bluefruit.Periph.setConnectCallback(connect_callback);
    //Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

    // Configure and Start the Device Information Service
    DEBUG_MSG("Configuring the Device Information Service\n");
    // FIXME, we should set a mfg string based on our HW_VENDOR enum
    // bledis.setManufacturer(HW_VENDOR);
    //bledis.setModel(optstr(HW_VERSION));
    //bledis.setFirmwareRev(optstr(APP_VERSION));
    //bledis.begin();

    // Start the BLE Battery Service and set it to 100%
    //DEBUG_MSG("Configuring the Battery Service\n");
    //blebas.begin();
    //blebas.write(0); // Unknown battery level for now

    //bledfu.begin(); // Install the DFU helper

    // Setup the Heart Rate Monitor service using
    // BLEService and BLECharacteristic classes
    DEBUG_MSG("Configuring the Mesh bluetooth service\n");
    //setupMeshService();

    // Supposedly debugging works with soft device if you disable advertising
    //if (isSoftDeviceAllowed) {
        // Setup the advertising packet(s)
    //    DEBUG_MSG("Setting up the advertising payload(s)\n");
    //    startAdv();

    //    DEBUG_MSG("Advertising\n");
    //}

    //NimBLEDevice::deleteAllBonds();

    NimBLEDevice::init("Meshtastic_1234");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    bleServer = NimBLEDevice::createServer();
    
    ESP32BluetoothServerCallback *serverCallbacks = new ESP32BluetoothServerCallback();
    bleServer->setCallbacks(serverCallbacks);

    NimBLEService *bleService = bleServer->createService(MESH_SERVICE_UUID);
    //NimBLECharacteristic *pNonSecureCharacteristic = bleService->createCharacteristic("1234", NIMBLE_PROPERTY::READ );
    //NimBLECharacteristic *pSecureCharacteristic = bleService->createCharacteristic("1235", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);

    //define the characteristics that the app is looking for
    NimBLECharacteristic *ToRadioCharacteristic = bleService->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC);
    NimBLECharacteristic *FromRadioCharacteristic = bleService->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
    FromNumCharacteristic = bleService->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
    
    bluetoothPhoneAPI = new BluetoothPhoneAPI();

    toRadioCallbacks = new ESP32BluetoothToRadioCallback();
    ToRadioCharacteristic->setCallbacks(toRadioCallbacks);

    fromRadioCallbacks = new ESP32BluetoothFromRadioCallback();
    FromRadioCharacteristic->setCallbacks(fromRadioCallbacks);

    //uint8_t val[4];
    //uint32_t zero = 0;
    //put_le32(val, zero);
    //std::string fromNumByteString(&val[0], &val[0] + sizeof(val));
    //FromNumCharacteristic->setValue(fromNumByteString);

    bleService->start();
    //pNonSecureCharacteristic->setValue("Hello Non Secure BLE");
    //pSecureCharacteristic->setValue("Hello Secure BLE");

    //FromRadioCharacteristic->setValue("FromRadioString");
    //ToRadioCharacteristic->setCallbacks()

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(MESH_SERVICE_UUID);
    pAdvertising->start();

}


/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level)
{
    //blebas.write(level);
}

void ESP32Bluetooth::clearBonds()
{
    DEBUG_MSG("Clearing bluetooth bonds!\n");
    //bond_print_list(BLE_GAP_ROLE_PERIPH);
    //bond_print_list(BLE_GAP_ROLE_CENTRAL);

    //Bluefruit.Periph.clearBonds();
    //Bluefruit.Central.clearBonds();
    
}

void clearNVS() {
    NimBLEDevice::deleteAllBonds();
    ESP.restart();
}

void disablePin() {
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