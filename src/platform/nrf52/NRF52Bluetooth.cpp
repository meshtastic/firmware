#include "configuration.h"
#include "NRF52Bluetooth.h"
#include "BluetoothCommon.h"
#include "main.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"
#include <bluefruit.h>
#include <utility/bonding.h>

static BLEService meshBleService = BLEService(BLEUuid(MESH_SERVICE_UUID_16));
static BLECharacteristic fromNum = BLECharacteristic(BLEUuid(FROMNUM_UUID_16));
static BLECharacteristic fromRadio = BLECharacteristic(BLEUuid(FROMRADIO_UUID_16));
static BLECharacteristic toRadio = BLECharacteristic(BLEUuid(TORADIO_UUID_16));

static BLEDis bledis; // DIS (Device Information Service) helper class instance
static BLEBas blebas; // BAS (Battery Service) helper class instance
static BLEDfu bledfu; // DFU software update helper service

// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in
// proccess at once
// static uint8_t trBytes[_max(_max(_max(_max(ToRadio_size, RadioConfig_size), User_size), MyNodeInfo_size), FromRadio_size)];
static uint8_t fromRadioBytes[FromRadio_size];
static uint8_t toRadioBytes[ToRadio_size];

static uint16_t connectionHandle;

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) override
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        DEBUG_MSG("BLE notify fromNum\n");
        fromNum.notify32(fromRadioNum);
    }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override {
        BLEConnection *connection = Bluefruit.Connection(connectionHandle);
        return connection->connected();
    }
};

static BluetoothPhoneAPI *bluetoothPhoneAPI;

void onConnect(uint16_t conn_handle)
{
    // Get the reference to current connection
    BLEConnection *connection = Bluefruit.Connection(conn_handle);
    connectionHandle = conn_handle;

    char central_name[32] = {0};
    connection->getPeerName(central_name, sizeof(central_name));

    DEBUG_MSG("BLE Connected to %s\n", central_name);
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void onDisconnect(uint16_t conn_handle, uint8_t reason)
{
    // FIXME - we currently assume only one active connection
    DEBUG_MSG("BLE Disconnected, reason = 0x%x\n", reason);
}

void onCccd(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t cccd_value)
{
    // Display the raw request packet
    DEBUG_MSG("CCCD Updated: %u\n", cccd_value);

    // Check the characteristic this CCCD update is associated with in case
    // this handler is used for multiple CCCD records.
    if (chr->uuid == fromNum.uuid) {
        if (chr->notifyEnabled(conn_hdl)) {
            DEBUG_MSG("fromNum 'Notify' enabled\n");
        } else {
            DEBUG_MSG("fromNum 'Notify' disabled\n");
        }
    }
}

void startAdv(void)
{
    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);

    // IncludeService UUID
    // Bluefruit.ScanResponse.addService(meshBleService);
    Bluefruit.ScanResponse.addTxPower();
    Bluefruit.ScanResponse.addName();

    // Include Name
    // Bluefruit.Advertising.addName();
    Bluefruit.Advertising.addService(meshBleService);

    /* Start Advertising
     * - Enable auto advertising if disconnected
     * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
     * - Timeout for fast mode is 30 seconds
     * - Start(timeout) with timeout = 0 will advertise forever (until connected)
     *
     * For recommended advertising interval
     * https://developer.apple.com/library/content/qa/qa1931/_index.html
     */
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);   // number of seconds in fast mode
    Bluefruit.Advertising.start(0); // 0 = Don't stop advertising after n seconds.  FIXME, we should stop advertising after X
}

// Just ack that the caller is allowed to read
static void authorizeRead(uint16_t conn_hdl)
{
    ble_gatts_rw_authorize_reply_params_t reply = {.type = BLE_GATTS_AUTHORIZE_TYPE_READ};
    reply.params.write.gatt_status = BLE_GATT_STATUS_SUCCESS;
    sd_ble_gatts_rw_authorize_reply(conn_hdl, &reply);
}

/**
 * client is starting read, pull the bytes from our API class
 */
void onFromRadioAuthorize(uint16_t conn_hdl, BLECharacteristic *chr, ble_gatts_evt_read_t *request)
{
    if (request->offset == 0) {
        // If the read is long, we will get multiple authorize invocations - we only populate data on the first
        size_t numBytes = bluetoothPhoneAPI->getFromRadio(fromRadioBytes);

        // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
        // or make empty if the queue is empty
        fromRadio.write(fromRadioBytes, numBytes);
    } else {
        // DEBUG_MSG("Ignoring successor read\n");
    }
    authorizeRead(conn_hdl);
}

void onToRadioWrite(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len)
{
    DEBUG_MSG("toRadioWriteCb data %p, len %u\n", data, len);

    bluetoothPhoneAPI->handleToRadio(data, len);
}

/**
 * client is starting read, pull the bytes from our API class
 */
void onFromNumAuthorize(uint16_t conn_hdl, BLECharacteristic *chr, ble_gatts_evt_read_t *request)
{
    DEBUG_MSG("fromNumAuthorizeCb\n");

    authorizeRead(conn_hdl);
}

void setupMeshService(void)
{
    bluetoothPhoneAPI = new BluetoothPhoneAPI();

    meshBleService.begin();

    // Note: You must call .begin() on the BLEService before calling .begin() on
    // any characteristic(s) within that service definition.. Calling .begin() on
    // a BLECharacteristic will cause it to be added to the last BLEService that
    // was 'begin()'ed!
    auto secMode = config.bluetooth.mode == Config_BluetoothConfig_PairingMode_NoPin ? SECMODE_OPEN : SECMODE_ENC_NO_MITM;

    fromNum.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    fromNum.setPermission(secMode, SECMODE_NO_ACCESS); // FIXME, secure this!!!
    fromNum.setFixedLen(0); // Variable len (either 0 or 4)  FIXME consider changing protocol so it is fixed 4 byte len, where 0 means empty
    fromNum.setMaxLen(4);
    fromNum.setCccdWriteCallback(onCccd); // Optionally capture CCCD updates
    // We don't yet need to hook the fromNum auth callback
    // fromNum.setReadAuthorizeCallback(fromNumAuthorizeCb);
    fromNum.write32(0); // Provide default fromNum of 0
    fromNum.begin();

    fromRadio.setProperties(CHR_PROPS_READ);
    fromRadio.setPermission(secMode, SECMODE_NO_ACCESS); 
    fromRadio.setMaxLen(sizeof(fromRadioBytes));
    fromRadio.setReadAuthorizeCallback(onFromRadioAuthorize, false); // We don't call this callback via the adafruit queue, because we can safely run in the BLE context
    fromRadio.setBuffer(fromRadioBytes, sizeof(fromRadioBytes)); // we preallocate our fromradio buffer so we won't waste space
    // for two copies
    fromRadio.begin();

    toRadio.setProperties(CHR_PROPS_WRITE);
    toRadio.setPermission(secMode, secMode); // FIXME secure this!
    toRadio.setFixedLen(0);
    toRadio.setMaxLen(512);
    toRadio.setBuffer(toRadioBytes, sizeof(toRadioBytes));
    // We don't call this callback via the adafruit queue, because we can safely run in the BLE context
    toRadio.setWriteCallback(onToRadioWrite, false); 
    toRadio.begin();
}

// FIXME, turn off soft device access for debugging
static bool isSoftDeviceAllowed = true;
static uint32_t configuredPasskey;

void NRF52Bluetooth::shutdown()
{
    // Shutdown bluetooth for minimum power draw
    DEBUG_MSG("Disable NRF52 bluetooth\n");
    Bluefruit.Advertising.stop();
}

void NRF52Bluetooth::setup()
{
    // Initialise the Bluefruit module
    DEBUG_MSG("Initialize the Bluefruit nRF52 module\n");
    Bluefruit.autoConnLed(false);
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();

    // Clear existing data.
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();

    if (config.bluetooth.mode != Config_BluetoothConfig_PairingMode_NoPin) {
        configuredPasskey = config.bluetooth.mode == Config_BluetoothConfig_PairingMode_FixedPin ? 
            config.bluetooth.fixed_pin : random(100000, 999999);
        auto pinString = std::to_string(configuredPasskey);
        DEBUG_MSG("Bluetooth pin set to '%i'\n", configuredPasskey);
        Bluefruit.Security.setPIN(pinString.c_str());
        Bluefruit.Security.setIOCaps(true, false, false);
        Bluefruit.Security.setPairPasskeyCallback(NRF52Bluetooth::onPairingPasskey);
        Bluefruit.Security.setPairCompleteCallback(NRF52Bluetooth::onPairingCompleted);
        Bluefruit.Security.setSecuredCallback(NRF52Bluetooth::onConnectionSecured);
        meshBleService.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
    }
    else {
        Bluefruit.Security.setIOCaps(false, false, false);
        meshBleService.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    }
    // Set the advertised device name (keep it short!)
    Bluefruit.setName(getDeviceName());

    // Set the connect/disconnect callback handlers
    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);

    // Configure and Start the Device Information Service
    DEBUG_MSG("Configuring the Device Information Service\n");
    bledis.setModel(optstr(HW_VERSION));
    bledis.setFirmwareRev(optstr(APP_VERSION));
    bledis.begin();

    // Start the BLE Battery Service and set it to 100%
    DEBUG_MSG("Configuring the Battery Service\n");
    blebas.begin();
    blebas.write(0); // Unknown battery level for now
    bledfu.begin(); // Install the DFU helper

    // Setup the Heart Rate Monitor service using
    // BLEService and BLECharacteristic classes
    DEBUG_MSG("Configuring the Mesh bluetooth service\n");
    setupMeshService();

    // Supposedly debugging works with soft device if you disable advertising
    if (isSoftDeviceAllowed) 
    {
        // Setup the advertising packet(s)
        DEBUG_MSG("Setting up the advertising payload(s)\n");
        startAdv();

        DEBUG_MSG("Advertising\n");
    }
}

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level)
{
    blebas.write(level);
}

void NRF52Bluetooth::clearBonds()
{
    DEBUG_MSG("Clearing bluetooth bonds!\n");
    bond_print_list(BLE_GAP_ROLE_PERIPH);
    bond_print_list(BLE_GAP_ROLE_CENTRAL);

    Bluefruit.Periph.clearBonds();
    Bluefruit.Central.clearBonds();
}

void NRF52Bluetooth::onConnectionSecured(uint16_t conn_handle)
{
    DEBUG_MSG("BLE connection secured\n");
}

bool NRF52Bluetooth::onPairingPasskey(uint16_t conn_handle, uint8_t const passkey[6], bool match_request)
{
    DEBUG_MSG("BLE pairing process started with passkey %.3s %.3s\n", passkey, passkey+3);
    screen->startBluetoothPinScreen(configuredPasskey);

    if (match_request)
    {
        uint32_t start_time = millis();
        while(millis() < start_time + 30000)
        {
            if (!Bluefruit.connected(conn_handle)) break;
        }
    }
    DEBUG_MSG("BLE passkey pairing: match_request=%i\n", match_request);
    return true;
}

void NRF52Bluetooth::onPairingCompleted(uint16_t conn_handle, uint8_t auth_status)
{
    if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS)
        DEBUG_MSG("BLE pairing success\n");
    else
        DEBUG_MSG("BLE pairing failed\n");

    screen->stopBluetoothPinScreen();
}
