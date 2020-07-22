#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include "configuration.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <Update.h>
#include <esp_gatt_defs.h>

#ifdef CONFIG_BLUEDROID_ENABLED

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
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);

    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    return pServer;
}

// Note: these callbacks might be coming in from a different thread.
BLEServer *serve = initBLE(, , getDeviceName(), HW_VENDOR, optstr(APP_VERSION),
                           optstr(HW_VERSION)); // FIXME, use a real name based on the macaddr

#else

#include "PhoneAPI.h"
#include "PowerFSM.h"
#include "host/util/util.h"
#include "main.h"
#include "nimble/NimbleDefs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static void startCb(uint32_t pin)
{
    powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
    screen.startBluetoothPinScreen(pin);
};

static void stopCb()
{
    screen.stopBluetoothPinScreen();
};

static uint8_t own_addr_type;

// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in
// proccess at once
static uint8_t trBytes[max(FromRadio_size, ToRadio_size)];

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum)
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        DEBUG_MSG("BLE notify fromNum\n");
        // fromNum.notify32(fromRadioNum);
    }
};

static BluetoothPhoneAPI *bluetoothPhoneAPI;

int toradio_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    auto om = ctxt->om;
    uint16_t len = 0;

    auto rc = ble_hs_mbuf_to_flat(om, trBytes, sizeof(trBytes), &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    DEBUG_MSG("toRadioWriteCb data %p, len %u\n", trBytes, len);

    bluetoothPhoneAPI->handleToRadio(trBytes, len);
    return 0;
}

int fromradio_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    DEBUG_MSG("BLE fromRadio called\n");
    size_t numBytes = bluetoothPhoneAPI->getFromRadio(trBytes);

    // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
    // or make empty if the queue is empty
    auto rc = os_mbuf_append(ctxt->om, trBytes, numBytes);
    assert(rc == 0);

    return 0; // success
}

int fromnum_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    static uint32_t fromNum = 0;

    DEBUG_MSG("BLE fromNum called\n");
    auto rc = os_mbuf_append(ctxt->om, &fromNum,
                             sizeof(fromNum)); // FIXME - once we report real numbers we will need to consider endianness
    assert(rc == 0);

    return 0; // success
}

// Force arduino to keep ble data around
extern "C" bool btInUse()
{
    return true;
}

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level)
{
    // FIXME
}

void deinitBLE()
{
    // FIXME - do we need to dealloc things? - what needs to stay alive across light sleep?
    auto ret = nimble_port_stop();
    assert(ret == ESP_OK);
}

void loopBLE()
{
    // FIXME
}

extern "C" void ble_store_config_init(void);

/// Print a macaddr - bytes are stored in reverse order
static void print_addr(const uint8_t v[])
{
    const int macaddrlen = 6;

    for (int i = 0; i < macaddrlen; i++) {
        DEBUG_MSG("%02x%c", v[macaddrlen - i], (i == macaddrlen - 1) ? '\n' : ':');
    }
}

/**
 * Logs information about a connection to the console.
 */
static void print_conn_desc(struct ble_gap_conn_desc *desc)
{
    DEBUG_MSG("handle=%d our_ota_addr_type=%d our_ota_addr=", desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    DEBUG_MSG(" our_id_addr_type=%d our_id_addr=", desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    DEBUG_MSG(" peer_ota_addr_type=%d peer_ota_addr=", desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    DEBUG_MSG(" peer_id_addr_type=%d peer_id_addr=", desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    DEBUG_MSG(" conn_itvl=%d conn_latency=%d supervision_timeout=%d "
              "encrypted=%d authenticated=%d bonded=%d\n",
              desc->conn_itvl, desc->conn_latency, desc->supervision_timeout, desc->sec_state.encrypted,
              desc->sec_state.authenticated, desc->sec_state.bonded);
}

static void advertise();

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        DEBUG_MSG("connection %s; status=%d ", event->connect.status == 0 ? "established" : "failed", event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
        }
        DEBUG_MSG("\n");

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        DEBUG_MSG("disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        DEBUG_MSG("\n");

        /* Connection terminated; resume advertising. */
        advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        DEBUG_MSG("connection updated; status=%d ", event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        DEBUG_MSG("\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        DEBUG_MSG("advertise complete; reason=%d", event->adv_complete.reason);
        advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        DEBUG_MSG("encryption change event; status=%d ", event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        DEBUG_MSG("\n");

        // Remove our custom PIN request screen.
        stopCb();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        DEBUG_MSG("subscribe event; conn_handle=%d attr_handle=%d "
                  "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                  event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.reason,
                  event->subscribe.prev_notify, event->subscribe.cur_notify, event->subscribe.prev_indicate,
                  event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        DEBUG_MSG("mtu update event; conn_handle=%d cid=%d mtu=%d\n", event->mtu.conn_handle, event->mtu.channel_id,
                  event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        DEBUG_MSG("PASSKEY_ACTION_EVENT started \n");
        struct ble_sm_io pkey = {0};

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = random(
                100000, 999999); // This is the passkey to be entered on peer - we pick a number >100,000 to ensure 6 digits
            DEBUG_MSG("*** Enter passkey %d on the peer side ***\n", pkey.passkey);

            startCb(pkey.passkey);

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            DEBUG_MSG("ble_sm_inject_io result: %d\n", rc);
        } else {
            DEBUG_MSG("FIXME - unexpected auth type %d\n", event->passkey.params.action);
        }
        return 0;
    }

    return 0;
}
/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void advertise(void)
{
    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    struct ble_hs_adv_fields adv_fields;
    memset(&adv_fields, 0, sizeof adv_fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    auto rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        DEBUG_MSG("error setting advertisement data; rc=%d\n", rc);
        return;
    }

    // add scan response fields
    struct ble_hs_adv_fields scan_fields;
    memset(&scan_fields, 0, sizeof scan_fields);
    scan_fields.uuids128 = const_cast<ble_uuid128_t *>(&mesh_service_uuid);
    scan_fields.num_uuids128 = 1;
    scan_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&scan_fields);
    if (rc != 0) {
        DEBUG_MSG("error setting scan response data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // FIXME - use RPA for first parameter
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        DEBUG_MSG("error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

static void on_reset(int reason)
{
    // 19 == BLE_HS_ETIMEOUT_HCI
    DEBUG_MSG("Resetting state; reason=%d\n", reason);
}

static void on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        DEBUG_MSG("error determining address type; rc=%d\n", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    DEBUG_MSG("Device Address: ");
    print_addr(addr_val);
    DEBUG_MSG("\n");
    /* Begin advertising. */
    advertise();
}

static void ble_host_task(void *param)
{
    DEBUG_MSG("BLE task running\n");
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed.

    nimble_port_deinit();          // teardown nimble datastructures
    nimble_port_freertos_deinit(); // delete the task

    auto ret = esp_nimble_hci_and_controller_deinit();
    assert(ret == ESP_OK);
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        DEBUG_MSG("registered service %s with handle=%d\n", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        DEBUG_MSG("registering characteristic %s with "
                  "def_handle=%d val_handle=%d\n",
                  ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        DEBUG_MSG("registering descriptor %s with handle=%d\n", ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

// This routine is called multiple times, once each time we come back from sleep
void reinitBluetooth()
{
    DEBUG_MSG("Starting bluetooth\n");
    esp_log_level_set("BTDM_INIT", ESP_LOG_VERBOSE);

    if (!bluetoothPhoneAPI) {
        bluetoothPhoneAPI = new BluetoothPhoneAPI();
        bluetoothPhoneAPI->init();
    }

    // FIXME - if waking from light sleep, only esp_nimble_hci_init
    // FIXME - why didn't this version work?
    auto res = esp_nimble_hci_and_controller_init();
    // auto res = esp_nimble_hci_init();
    // DEBUG_MSG("BLE result %d\n", res);
    assert(res == ESP_OK);

    nimble_port_init();

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = 1;
    ble_hs_cfg.sm_their_key_dist = 1;

    // add standard GAP services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    res = ble_gatts_count_cfg(
        gatt_svr_svcs); // assigns handles?  see docstring for note about clearing the handle list before calling SLEEP SUPPORT
    assert(res == 0);

    res = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(res == 0);

    /* Set the default device name. */
    res = ble_svc_gap_device_name_set(getDeviceName());
    assert(res == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();

    nimble_port_freertos_init(ble_host_task);
}

#endif
