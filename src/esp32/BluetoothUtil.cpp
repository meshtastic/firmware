#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include "configuration.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <Update.h>
#include <esp_gatt_defs.h>

SimpleAllocator btPool;

bool _BLEClientConnected = false;

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer) { _BLEClientConnected = true; };

    void onDisconnect(BLEServer *pServer) { _BLEClientConnected = false; }
};

#define MAX_DESCRIPTORS 32
#define MAX_CHARACTERISTICS 32

static BLECharacteristic *chars[MAX_CHARACTERISTICS];
static size_t numChars;
static BLEDescriptor *descs[MAX_DESCRIPTORS];
static size_t numDescs;

/// Add a characteristic that we will delete when we restart
BLECharacteristic *addBLECharacteristic(BLECharacteristic *c)
{
    assert(numChars < MAX_CHARACTERISTICS);
    chars[numChars++] = c;
    return c;
}

/// Add a characteristic that we will delete when we restart
BLEDescriptor *addBLEDescriptor(BLEDescriptor *c)
{
    assert(numDescs < MAX_DESCRIPTORS);
    descs[numDescs++] = c;

    return c;
}

// Help routine to add a description to any BLECharacteristic and add it to the service
// We default to require an encrypted BOND for all these these characterstics
void addWithDesc(BLEService *service, BLECharacteristic *c, const char *description)
{
    c->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);

    BLEDescriptor *desc = new BLEDescriptor(BLEUUID((uint16_t)ESP_GATT_UUID_CHAR_DESCRIPTION), strlen(description) + 1);
    assert(desc);
    desc->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
    desc->setValue(description);
    c->addDescriptor(desc);
    service->addCharacteristic(c);
    addBLECharacteristic(c);
    addBLEDescriptor(desc);
}

/**
 * Create standard device info service
 **/
BLEService *createDeviceInfomationService(BLEServer *server, std::string hwVendor, std::string swVersion,
                                          std::string hwVersion = "")
{
    BLEService *deviceInfoService = server->createService(BLEUUID((uint16_t)ESP_GATT_UUID_DEVICE_INFO_SVC));

    BLECharacteristic *swC =
        new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_SW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
    BLECharacteristic *mfC = new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_MANU_NAME), BLECharacteristic::PROPERTY_READ);
    // BLECharacteristic SerialNumberCharacteristic(BLEUUID((uint16_t) ESP_GATT_UUID_SERIAL_NUMBER_STR),
    // BLECharacteristic::PROPERTY_READ);

    /*
           * Mandatory characteristic for device info service?

          BLECharacteristic *m_pnpCharacteristic = m_deviceInfoService->createCharacteristic(ESP_GATT_UUID_PNP_ID,
      BLECharacteristic::PROPERTY_READ);

      uint8_t sig, uint16_t vid, uint16_t pid, uint16_t version;
          uint8_t pnp[] = { sig, (uint8_t) (vid >> 8), (uint8_t) vid, (uint8_t) (pid >> 8), (uint8_t) pid, (uint8_t) (version >>
      8), (uint8_t) version }; m_pnpCharacteristic->setValue(pnp, sizeof(pnp));
      */
    swC->setValue(swVersion);
    deviceInfoService->addCharacteristic(addBLECharacteristic(swC));
    mfC->setValue(hwVendor);
    deviceInfoService->addCharacteristic(addBLECharacteristic(mfC));
    if (!hwVersion.empty()) {
        BLECharacteristic *hwvC =
            new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_HW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
        hwvC->setValue(hwVersion);
        deviceInfoService->addCharacteristic(addBLECharacteristic(hwvC));
    }
    // SerialNumberCharacteristic.setValue("FIXME");
    // deviceInfoService->addCharacteristic(&SerialNumberCharacteristic);

    // m_manufacturerCharacteristic = m_deviceInfoService->createCharacteristic((uint16_t) 0x2a29,
    // BLECharacteristic::PROPERTY_READ); m_manufacturerCharacteristic->setValue(name);

    /* add these later?
      ESP_GATT_UUID_SYSTEM_ID
      */

    // caller must call service->start();
    return deviceInfoService;
}

static BLECharacteristic *batteryLevelC;

/**
 * Create a battery level service
 */
BLEService *createBatteryService(BLEServer *server)
{
    // Create the BLE Service
    BLEService *pBattery = server->createService(BLEUUID((uint16_t)0x180F));

    batteryLevelC = new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_BATTERY_LEVEL),
                                          BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    addWithDesc(pBattery, batteryLevelC, "Percentage 0 - 100");
    batteryLevelC->addDescriptor(addBLEDescriptor(new BLE2902())); // Needed so clients can request notification

    // I don't think we need to advertise this? and some phones only see the first thing advertised anyways...
    // server->getAdvertising()->addServiceUUID(pBattery->getUUID());
    pBattery->start();

    return pBattery;
}

/**
 * Update the battery level we are currently telling clients.
 * level should be a pct between 0 and 100
 */
void updateBatteryLevel(uint8_t level)
{
    if (batteryLevelC) {
        DEBUG_MSG("set BLE battery level %u\n", level);
        batteryLevelC->setValue(&level, 1);
        batteryLevelC->notify();
    }
}

void dumpCharacteristic(BLECharacteristic *c)
{
    std::string value = c->getValue();

    if (value.length() > 0) {
        DEBUG_MSG("New value: ");
        for (int i = 0; i < value.length(); i++)
            DEBUG_MSG("%c", value[i]);

        DEBUG_MSG("\n");
    }
}

/** converting endianness pull out a 32 bit value */
uint32_t getValue32(BLECharacteristic *c, uint32_t defaultValue)
{
    std::string value = c->getValue();
    uint32_t r = defaultValue;

    if (value.length() == 4)
        r = value[0] | (value[1] << 8UL) | (value[2] << 16UL) | (value[3] << 24UL);

    return r;
}

class MySecurity : public BLESecurityCallbacks
{
  protected:
    bool onConfirmPIN(uint32_t pin)
    {
        Serial.printf("onConfirmPIN %u\n", pin);
        return false;
    }

    uint32_t onPassKeyRequest()
    {
        Serial.println("onPassKeyRequest");
        return 123511; // not used
    }

    void onPassKeyNotify(uint32_t pass_key)
    {
        Serial.printf("onPassKeyNotify %06u\n", pass_key);
        startCb(pass_key);
    }

    bool onSecurityRequest()
    {
        Serial.println("onSecurityRequest");
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
    {
        if (cmpl.success) {
            uint16_t length;
            esp_ble_gap_get_whitelist_size(&length);
            Serial.printf(" authenticated and connected to phone\n");
        } else {
            Serial.printf("phone authenticate failed %d\n", cmpl.fail_reason);
        }

        // Remove our custom PIN request screen.
        stopCb();
    }

  public:
    StartBluetoothPinScreenCallback startCb;
    StopBluetoothPinScreenCallback stopCb;
};

BLEServer *pServer;

BLEService *pDevInfo, *pUpdate, *pBattery;

void deinitBLE()
{
    assert(pServer);

    pServer->getAdvertising()->stop();

    if (pUpdate != NULL) {
        destroyUpdateService();

        pUpdate->stop(); // we delete them below
        pUpdate->executeDelete();
    }

    pBattery->stop();
    pBattery->executeDelete();

    pDevInfo->stop();
    pDevInfo->executeDelete();

    // First shutdown bluetooth
    BLEDevice::deinit(false);

    // do not delete this - it is dynamically allocated, but only once - statically in BLEDevice
    // delete pServer->getAdvertising();

    if (pUpdate != NULL)
        delete pUpdate;
    delete pDevInfo;
    delete pBattery;
    delete pServer;

    batteryLevelC = NULL; // Don't let anyone generate bogus notifies

    for (int i = 0; i < numChars; i++) {
        delete chars[i];
    }
    numChars = 0;

    for (int i = 0; i < numDescs; i++)
        delete descs[i];
    numDescs = 0;

    btPool.reset();
}

BLEServer *initBLE(StartBluetoothPinScreenCallback startBtPinScreen, StopBluetoothPinScreenCallback stopBtPinScreen,
                   std::string deviceName, std::string hwVendor, std::string swVersion, std::string hwVersion)
{
    BLEDevice::init(deviceName);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

    /*
     * Required in authentication process to provide displaying and/or input passkey or yes/no butttons confirmation
     */
    static MySecurity mySecurity;
    mySecurity.startCb = startBtPinScreen;
    mySecurity.stopCb = stopBtPinScreen;
    BLEDevice::setSecurityCallbacks(&mySecurity);

    // Create the BLE Server
    pServer = BLEDevice::createServer();
    static MyServerCallbacks myCallbacks;
    pServer->setCallbacks(&myCallbacks);

    pDevInfo = createDeviceInfomationService(pServer, hwVendor, swVersion, hwVersion);

    pBattery = createBatteryService(pServer);

#define BLE_SOFTWARE_UPDATE
#ifdef BLE_SOFTWARE_UPDATE
    pUpdate = createUpdateService(pServer, hwVendor, swVersion,
                                  hwVersion); // We need to advertise this so our android ble scan operation can see it

    pUpdate->start();
#endif

    // It seems only one service can be advertised - so for now don't advertise our updater
    // pServer->getAdvertising()->addServiceUUID(pUpdate->getUUID());

    // start all our services (do this after creating all of them)
    pDevInfo->start();

    // FIXME turn on this restriction only after the device is paired with a phone
    // advert->setScanFilter(false, true); // We let anyone scan for us (FIXME, perhaps only allow that until we are paired with a
    // phone and configured) but only let whitelist phones connect

    static BLESecurity security; // static to avoid allocs
    BLESecurity *pSecurity = &security;
    pSecurity->setCapability(ESP_IO_CAP_OUT);

    // FIXME - really should be ESP_LE_AUTH_REQ_SC_BOND but it seems there is a bug right now causing that bonding info to be lost
    // occasionally?
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);

    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    return pServer;
}

// Called from loop
void loopBLE()
{
    bluetoothRebootCheck();
}
