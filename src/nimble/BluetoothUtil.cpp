#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include "NimbleBluetoothAPI.h"
#include "NodeDB.h" // FIXME - we shouldn't really douch this here - we are using it only because we currently do wifi setup when ble gets turned on
#include "PhoneAPI.h"
#include "PowerFSM.h"
#include "WiFi.h"
#include "configuration.h"
#include "esp_bt.h"
#include "host/util/util.h"
#include "main.h"
#include "nimble/NimbleDefs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <Arduino.h>

static bool pinShowing;

static void startCb(uint32_t pin)
{
    pinShowing = true;
    powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
    screen.startBluetoothPinScreen(pin);
};

static void stopCb()
{
    if (pinShowing) {
        pinShowing = false;
        screen.stopBluetoothPinScreen();
    }
};

static uint8_t own_addr_type;

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
    // DEBUG_MSG("Shutting down bluetooth\n");
    // ble_gatts_show_local();

    // FIXME - do we need to dealloc things? - what needs to stay alive across light sleep?
    auto ret = nimble_port_stop();
    assert(ret == ESP_OK);

    nimble_port_deinit(); // teardown nimble datastructures

    // DEBUG_MSG("BLE port_deinit done\n");

    ret = esp_nimble_hci_and_controller_deinit();
    assert(ret == ESP_OK);

    // DEBUG_MSG("BLE task exiting\n");

    DEBUG_MSG("Done shutting down bluetooth\n");
}

void loopBLE()
{
    // FIXME
}

extern "C" void ble_store_config_init(void);

/// Print a macaddr - bytes are sometimes stored in reverse order
static void print_addr(const uint8_t v[], bool isReversed = true)
{
    const int macaddrlen = 6;

    for (int i = 0; i < macaddrlen; i++) {
        DEBUG_MSG("%02x%c", v[isReversed ? macaddrlen - i : i], (i == macaddrlen - 1) ? '\n' : ':');
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
static int gap_event(struct ble_gap_event *event, void *arg)
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
            curConnectionHandle = event->connect.conn_handle;
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

        curConnectionHandle = -1;

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
        DEBUG_MSG("repeat pairing event; conn_handle=%d "
                  "cur_key_sz=%d cur_auth=%d cur_sc=%d "
                  "new_key_sz=%d new_auth=%d new_sc=%d "
                  "new_bonding=%d\n",
                  event->repeat_pairing.conn_handle, event->repeat_pairing.cur_key_size, event->repeat_pairing.cur_authenticated,
                  event->repeat_pairing.cur_sc, event->repeat_pairing.new_key_size, event->repeat_pairing.new_authenticated,
                  event->repeat_pairing.new_sc, event->repeat_pairing.new_bonding);
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
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
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
    int isPrivate = 0;
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, &isPrivate);
    assert(rc == 0);
    DEBUG_MSG("Addr type %d, Private=%d, Device Address: ", own_addr_type, isPrivate);
    print_addr(addr_val);
    DEBUG_MSG("\n");
    /* Begin advertising. */
    advertise();
}

static void ble_host_task(void *param)
{
    DEBUG_MSG("BLE task running\n");
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed.

    // DEBUG_MSG("BLE run complete\n");

    nimble_port_freertos_deinit(); // delete the task
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

        if (ctxt->chr.chr_def->uuid == &fromnum_uuid.u) {
            fromNumValHandle = ctxt->chr.val_handle;
            DEBUG_MSG("FromNum handle %d\n", fromNumValHandle);
        }
        if (ctxt->chr.chr_def->uuid == &update_result_uuid.u) {
            updateResultHandle = ctxt->chr.val_handle;
            DEBUG_MSG("update result handle %d\n", updateResultHandle);
        }
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        DEBUG_MSG("registering descriptor %s with handle=%d\n", ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

/**
 * A helper function that implements simple read and write handling for a uint32_t
 *
 * If a read, the provided value will be returned over bluetooth.  If a write, the value from the received packet
 * will be written into the variable.
 */
int chr_readwrite32le(uint32_t *v, struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t le[4];

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        DEBUG_MSG("BLE reading a uint32\n");
        put_le32(le, *v);
        auto rc = os_mbuf_append(ctxt->om, le, sizeof(le));
        assert(rc == 0);
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = 0;

        auto rc = ble_hs_mbuf_to_flat(ctxt->om, le, sizeof(le), &len);
        assert(rc == 0);
        if (len < sizeof(le)) {
            DEBUG_MSG("Error: wrongsized write32\n");
            *v = 0;
        } else {
            *v = get_le32(le);
            DEBUG_MSG("BLE writing a uint32\n");
        }
    } else {
        DEBUG_MSG("Unexpected readwrite32 op\n");
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0; // success
}

/**
 * A helper for readwrite access to an array of bytes (with no endian conversion)
 */
int chr_readwrite8(uint8_t *v, size_t vlen, struct ble_gatt_access_ctxt *ctxt)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        DEBUG_MSG("BLE reading bytes\n");
        auto rc = os_mbuf_append(ctxt->om, v, vlen);
        assert(rc == 0);
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = 0;

        auto rc = ble_hs_mbuf_to_flat(ctxt->om, v, vlen, &len);
        assert(rc == 0);
        if (len < vlen)
            DEBUG_MSG("Error: wrongsized write\n");
        else {
            DEBUG_MSG("BLE writing bytes\n");
        }
    } else {
        DEBUG_MSG("Unexpected readwrite8 op\n");
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0; // success
}

// This routine is called multiple times, once each time we come back from sleep
void reinitBluetooth()
{
    auto isFirstTime = !bluetoothPhoneAPI;

    DEBUG_MSG("Starting bluetooth\n");
    if (isFirstTime) {
        bluetoothPhoneAPI = new BluetoothPhoneAPI();
        bluetoothPhoneAPI->init();
    }

    // FIXME - if waking from light sleep, only esp_nimble_hci_init?
    auto res = esp_nimble_hci_and_controller_init(); // : esp_nimble_hci_init();
    // DEBUG_MSG("BLE result %d\n", res);
    assert(res == ESP_OK);

    nimble_port_init();

    ble_att_set_preferred_mtu(512);

    res = ble_gatts_reset(); // Teardown the service tables, so the next restart assigns the same handle numbers
    assert(res == ESP_OK);

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    // per https://github.com/espressif/esp-idf/issues/5530#issuecomment-652933685
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    // add standard GAP services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    res = ble_gatts_count_cfg(gatt_svr_svcs); // assigns handles?  see docstring for note about clearing the handle list
                                              // before calling SLEEP SUPPORT
    assert(res == 0);

    res = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(res == 0);

    reinitUpdateService();

    /* Set the default device name. */
    res = ble_svc_gap_device_name_set(getDeviceName());
    assert(res == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();

    nimble_port_freertos_init(ble_host_task);
}

void initWifi()
{
    // Note: Wifi is not yet supported ;-)
    strcpy(radioConfig.preferences.wifi_ssid, "");
    strcpy(radioConfig.preferences.wifi_password, "");
    if (radioConfig.has_preferences) {
        const char *wifiName = radioConfig.preferences.wifi_ssid;

        if (*wifiName) {
            const char *wifiPsw = radioConfig.preferences.wifi_password;
            if (radioConfig.preferences.wifi_ap_mode) {
                DEBUG_MSG("STARTING WIFI AP: ssid=%s, ok=%d\n", wifiName, WiFi.softAP(wifiName, wifiPsw));
            } else {
                WiFi.mode(WIFI_MODE_STA);
                DEBUG_MSG("JOINING WIFI: ssid=%s\n", wifiName);
                if (WiFi.begin(wifiName, wifiPsw) == WL_CONNECTED) {
                    DEBUG_MSG("MY IP ADDRESS: %s\n", WiFi.localIP().toString().c_str());
                } else {
                    DEBUG_MSG("Started Joining WIFI\n");
                }
            }
        }
    } else
        DEBUG_MSG("Not using WIFI\n");
}

bool bluetoothOn;

// Enable/disable bluetooth.
void setBluetoothEnable(bool on)
{
    if (on != bluetoothOn) {
        DEBUG_MSG("Setting bluetooth enable=%d\n", on);

        bluetoothOn = on;
        if (on) {
            Serial.printf("Pre BT: %u heap size\n", ESP.getFreeHeap());
            // ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
            reinitBluetooth();
            initWifi();
        } else {
            // We have to totally teardown our bluetooth objects to prevent leaks
            deinitBLE();
            WiFi.mode(WIFI_MODE_NULL); // shutdown wifi
            Serial.printf("Shutdown BT: %u heap size\n", ESP.getFreeHeap());
            // ESP_ERROR_CHECK( heap_trace_stop() );
            // heap_trace_dump();
        }
    }
}

#if 0

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



// Note: these callbacks might be coming in from a different thread.
BLEServer *serve = initBLE(, , getDeviceName(), HW_VENDOR, optstr(APP_VERSION),
                           optstr(HW_VERSION)); // FIXME, use a real name based on the macaddr

#endif