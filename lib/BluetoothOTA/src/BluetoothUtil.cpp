#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include <esp_gatt_defs.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <Update.h>

static BLECharacteristic SWVersionCharacteristic(BLEUUID((uint16_t) ESP_GATT_UUID_SW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
static BLECharacteristic ManufacturerCharacteristic(BLEUUID((uint16_t) ESP_GATT_UUID_MANU_NAME), BLECharacteristic::PROPERTY_READ);
static BLECharacteristic HardwareVersionCharacteristic(BLEUUID((uint16_t) ESP_GATT_UUID_HW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
//static BLECharacteristic SerialNumberCharacteristic(BLEUUID((uint16_t) ESP_GATT_UUID_SERIAL_NUMBER_STR), BLECharacteristic::PROPERTY_READ);

/**
 * Create standard device info service
 **/
BLEService *createDeviceInfomationService(BLEServer* server) {
    BLEService *deviceInfoService = server->createService(BLEUUID((uint16_t) ESP_GATT_UUID_DEVICE_INFO_SVC));

    /*
	 * Mandatory characteristic for device info service?
	 
	BLECharacteristic *m_pnpCharacteristic = m_deviceInfoService->createCharacteristic(ESP_GATT_UUID_PNP_ID, BLECharacteristic::PROPERTY_READ);

    uint8_t sig, uint16_t vid, uint16_t pid, uint16_t version;
	uint8_t pnp[] = { sig, (uint8_t) (vid >> 8), (uint8_t) vid, (uint8_t) (pid >> 8), (uint8_t) pid, (uint8_t) (version >> 8), (uint8_t) version };
	m_pnpCharacteristic->setValue(pnp, sizeof(pnp));
    */
    SWVersionCharacteristic.setValue("0.1");
    deviceInfoService->addCharacteristic(&SWVersionCharacteristic);
    ManufacturerCharacteristic.setValue("TTGO");
    deviceInfoService->addCharacteristic(&ManufacturerCharacteristic);
    HardwareVersionCharacteristic.setValue("1.0");
    deviceInfoService->addCharacteristic(&HardwareVersionCharacteristic); 
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

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      _BLEClientConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      _BLEClientConnected = false;
    }
};

// Help routine to add a description to any BLECharacteristic and add it to the service
void addWithDesc(BLEService *service, BLECharacteristic *c, const char *description) {
  BLEDescriptor *desc = new BLEDescriptor(BLEUUID((uint16_t) ESP_GATT_UUID_CHAR_DESCRIPTION), strlen(description) + 1);
  assert(desc);
  desc->setValue(description);
  c->addDescriptor(desc);
  service->addCharacteristic(c);
}

static BLECharacteristic BatteryLevelCharacteristic(BLEUUID((uint16_t) ESP_GATT_UUID_BATTERY_LEVEL), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

/**
 * Create a battery level service
 */
BLEService *createBatteryService(BLEServer* server) {
    // Create the BLE Service
    BLEService *pBattery = server->createService(BLEUUID((uint16_t)0x180F));

    addWithDesc(pBattery, &BatteryLevelCharacteristic, "Percentage 0 - 100");
    BatteryLevelCharacteristic.addDescriptor(new BLE2902()); // Needed so clients can request notification

    // I don't think we need to advertise this
    // server->getAdvertising()->addServiceUUID(pBattery->getUUID());
    pBattery->start();

    return pBattery;
}


/**
 * Update the battery level we are currently telling clients.
 * level should be a pct between 0 and 100
 */
void updateBatteryLevel(uint8_t level) {
      // Pretend to update battery levels - fixme do elsewhere
    BatteryLevelCharacteristic.setValue(&level, 1);
    BatteryLevelCharacteristic.notify();
}


void dumpCharacteristic(BLECharacteristic *c) {
      std::string value = c->getValue();

      if (value.length() > 0) {
        Serial.print("New value: ");
        for (int i = 0; i < value.length(); i++)
          Serial.print(value[i]);

        Serial.println();
      }
}

/** converting endianness pull out a 32 bit value */
uint32_t getValue32(BLECharacteristic *c, uint32_t defaultValue) {
    std::string value = c->getValue();
    uint32_t r = defaultValue;

    if(value.length() == 4) 
        r = value[0] | (value[1] << 8UL) | (value[2] << 16UL) | (value[3] << 24UL);
    
    return r;
}


BLEServer *initBLE(std::string deviceName) {
    BLEDevice::init(deviceName);
    // Create the BLE Server
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pDevInfo = createDeviceInfomationService(pServer);

    // We now let users create the battery service only if they really want (not all devices have a battery)
    // BLEService *pBattery = createBatteryService(pServer);

    BLEService *pUpdate = createUpdateService(pServer); // We need to advertise this so our android ble scan operation can see it
    pServer->getAdvertising()->addServiceUUID(pUpdate->getUUID());

    // start all our services (do this after creating all of them)
    pDevInfo->start();
    pUpdate->start();

    // Start advertising
    pServer->getAdvertising()->start();

    return pServer;
}

// Called from loop
void loopBLE() {
    bluetoothRebootCheck();
}
