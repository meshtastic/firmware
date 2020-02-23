#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include <esp_gatt_defs.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <Update.h>
#include "configuration.h"
#include "screen.h"

SimpleAllocator btPool;

/**
 * Create standard device info service
 **/
BLEService *createDeviceInfomationService(BLEServer *server, std::string hwVendor, std::string swVersion, std::string hwVersion = "")
{
  BLEService *deviceInfoService = server->createService(BLEUUID((uint16_t)ESP_GATT_UUID_DEVICE_INFO_SVC));

  BLECharacteristic *swC = new (btPool) BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_SW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
  BLECharacteristic *mfC = new (btPool) BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_MANU_NAME), BLECharacteristic::PROPERTY_READ);
  // BLECharacteristic SerialNumberCharacteristic(BLEUUID((uint16_t) ESP_GATT_UUID_SERIAL_NUMBER_STR), BLECharacteristic::PROPERTY_READ);

  /*
	 * Mandatory characteristic for device info service?
	 
	BLECharacteristic *m_pnpCharacteristic = m_deviceInfoService->createCharacteristic(ESP_GATT_UUID_PNP_ID, BLECharacteristic::PROPERTY_READ);

    uint8_t sig, uint16_t vid, uint16_t pid, uint16_t version;
	uint8_t pnp[] = { sig, (uint8_t) (vid >> 8), (uint8_t) vid, (uint8_t) (pid >> 8), (uint8_t) pid, (uint8_t) (version >> 8), (uint8_t) version };
	m_pnpCharacteristic->setValue(pnp, sizeof(pnp));
    */
  swC->setValue(swVersion);
  deviceInfoService->addCharacteristic(swC);
  mfC->setValue(hwVendor);
  deviceInfoService->addCharacteristic(mfC);
  if (!hwVersion.empty())
  {
    BLECharacteristic *hwvC = new (btPool) BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_HW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
    hwvC->setValue(hwVersion);
    deviceInfoService->addCharacteristic(hwvC);
  }
  //SerialNumberCharacteristic.setValue("FIXME");
  //deviceInfoService->addCharacteristic(&SerialNumberCharacteristic);

  // m_manufacturerCharacteristic = m_deviceInfoService->createCharacteristic((uint16_t) 0x2a29, BLECharacteristic::PROPERTY_READ);
  // m_manufacturerCharacteristic->setValue(name);

  /* add these later?
    ESP_GATT_UUID_SYSTEM_ID
    */

  // caller must call service->start();
  return deviceInfoService;
}

bool _BLEClientConnected = false;

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    _BLEClientConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    _BLEClientConnected = false;
  }
};

// Help routine to add a description to any BLECharacteristic and add it to the service
// We default to require an encrypted BOND for all these these characterstics
void addWithDesc(BLEService *service, BLECharacteristic *c, const char *description)
{
  c->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);

  BLEDescriptor *desc = new (btPool) BLEDescriptor(BLEUUID((uint16_t)ESP_GATT_UUID_CHAR_DESCRIPTION), strlen(description) + 1);
  assert(desc);
  desc->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  desc->setValue(description);
  c->addDescriptor(desc);
  service->addCharacteristic(c);
}

static BLECharacteristic *batteryLevelC;

/**
 * Create a battery level service
 */
BLEService *createBatteryService(BLEServer *server)
{
  // Create the BLE Service
  BLEService *pBattery = server->createService(BLEUUID((uint16_t)0x180F));

  batteryLevelC = new (btPool) BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_BATTERY_LEVEL), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  addWithDesc(pBattery, batteryLevelC, "Percentage 0 - 100");
  batteryLevelC->addDescriptor(new (btPool) BLE2902()); // Needed so clients can request notification

  // I don't think we need to advertise this
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
  // Pretend to update battery levels - fixme do elsewhere
  if (batteryLevelC)
  {
    batteryLevelC->setValue(&level, 1);
    batteryLevelC->notify();
  }
}

void dumpCharacteristic(BLECharacteristic *c)
{
  std::string value = c->getValue();

  if (value.length() > 0)
  {
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
    Serial.printf("onPassKeyNotify %u\n", pass_key);
    screen_start_bluetooth(pass_key);
  }

  bool onSecurityRequest()
  {
    Serial.println("onSecurityRequest");
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
  {
    if (cmpl.success)
    {
      uint16_t length;
      esp_ble_gap_get_whitelist_size(&length);
      Serial.printf(" onAuthenticationComplete -> success size: %d\n", length);
    }
    else
    {
      Serial.printf("onAuthenticationComplete -> fail %d\n", cmpl.fail_reason);
    }

    // Remove our custom screen
    screen_set_frames();
  }
};

BLEServer *pServer;

BLEService *pDevInfo, *pUpdate;

void deinitBLE()
{
  assert(pServer);

  // First shutdown bluetooth
  BLEDevice::deinit(false);

  // do not delete this - it is dynamically allocated, but only once - statically in BLEDevice
  // delete pServer->getAdvertising();

  delete pUpdate;
  delete pDevInfo;
  delete pServer;

  batteryLevelC = NULL; // Don't let anyone generate bogus notifies
  destroyUpdateService();

  btPool.reset();
}

BLEServer *initBLE(std::string deviceName, std::string hwVendor, std::string swVersion, std::string hwVersion)
{
  BLEDevice::init(deviceName);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

  /*
   * Required in authentication process to provide displaying and/or input passkey or yes/no butttons confirmation
   */
  static MySecurity mySecurity;
  BLEDevice::setSecurityCallbacks(&mySecurity);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  static MyServerCallbacks myCallbacks;
  pServer->setCallbacks(&myCallbacks);

  pDevInfo = createDeviceInfomationService(pServer, hwVendor, swVersion, hwVersion);

  // We now let users create the battery service only if they really want (not all devices have a battery)
  // BLEService *pBattery = createBatteryService(pServer);

  pUpdate = createUpdateService(pServer); // We need to advertise this so our android ble scan operation can see it

  // It seems only one service can be advertised - so for now don't advertise our updater
  // pServer->getAdvertising()->addServiceUUID(pUpdate->getUUID());

  // start all our services (do this after creating all of them)
  pDevInfo->start();
  pUpdate->start();

  // FIXME turn on this restriction only after the device is paired with a phone
  // advert->setScanFilter(false, true); // We let anyone scan for us (FIXME, perhaps only allow that until we are paired with a phone and configured) but only let whitelist phones connect

  static BLESecurity security; // static to avoid allocs
  BLESecurity *pSecurity = &security;
  pSecurity->setCapability(ESP_IO_CAP_OUT);
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  return pServer;
}

// Called from loop
void loopBLE()
{
  bluetoothRebootCheck();
}
