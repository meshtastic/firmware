#include "NRF52Bluetooth.h"
#include "BluetoothCommon.h"
#include "configuration.h"
#include "main.h"
#include <bluefruit.h>



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

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum)
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        DEBUG_MSG("BLE notify fromNum\n");
        fromNum.notify32(fromRadioNum);
    }
};

static BluetoothPhoneAPI *bluetoothPhoneAPI;

void connect_callback(uint16_t conn_handle)
{
    // Get the reference to current connection
    BLEConnection *connection = Bluefruit.Connection(conn_handle);

    char central_name[32] = {0};
    connection->getPeerName(central_name, sizeof(central_name));

    DEBUG_MSG("BLE Connected to %s\n", central_name);
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;

    DEBUG_MSG("BLE Disconnected, reason = 0x%x\n", reason);
}

void cccd_callback(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t cccd_value)
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
void fromRadioAuthorizeCb(uint16_t conn_hdl, BLECharacteristic *chr, ble_gatts_evt_read_t *request)
{
    if (request->offset == 0) {
        // If the read is long, we will get multiple authorize invocations - we only populate data on the first

        size_t numBytes = bluetoothPhoneAPI->getFromRadio(fromRadioBytes);

        // DEBUG_MSG("fromRadioAuthorizeCb numBytes=%u\n", numBytes);
        // if (numBytes >= 2) DEBUG_MSG("fromRadio bytes %x %x\n", fromRadioBytes[0], fromRadioBytes[1]);

        // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
        // or make empty if the queue is empty
        fromRadio.write(fromRadioBytes, numBytes);
    } else {
        // DEBUG_MSG("Ignoring successor read\n");
    }
    authorizeRead(conn_hdl);
}

void toRadioWriteCb(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len)
{
    DEBUG_MSG("toRadioWriteCb data %p, len %u\n", data, len);

    bluetoothPhoneAPI->handleToRadio(data, len);
}

/**
 * client is starting read, pull the bytes from our API class
 */
void fromNumAuthorizeCb(uint16_t conn_hdl, BLECharacteristic *chr, ble_gatts_evt_read_t *request)
{
    DEBUG_MSG("fromNumAuthorizeCb\n");

    authorizeRead(conn_hdl);
}

void setupMeshService(void)
{
    bluetoothPhoneAPI = new BluetoothPhoneAPI();
    bluetoothPhoneAPI->init();

    meshBleService.begin();

    // Note: You must call .begin() on the BLEService before calling .begin() on
    // any characteristic(s) within that service definition.. Calling .begin() on
    // a BLECharacteristic will cause it to be added to the last BLEService that
    // was 'begin()'ed!

    fromNum.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    fromNum.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS); // FIXME, secure this!!!
    fromNum.setFixedLen(
        0); // Variable len (either 0 or 4)  FIXME consider changing protocol so it is fixed 4 byte len, where 0 means empty
    fromNum.setMaxLen(4);
    fromNum.setCccdWriteCallback(cccd_callback); // Optionally capture CCCD updates
    // We don't yet need to hook the fromNum auth callback
    // fromNum.setReadAuthorizeCallback(fromNumAuthorizeCb);
    fromNum.write32(0); // Provide default fromNum of 0
    fromNum.begin();
    // uint8_t hrmdata[2] = {0b00000110, 0x40}; // Set the characteristic to use 8-bit values, with the sensor connected and
    // detected
    // hrmc.write(hrmdata, 2);

    fromRadio.setProperties(CHR_PROPS_READ);
    fromRadio.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS); // FIXME secure this!
    fromRadio.setMaxLen(sizeof(fromRadioBytes));
    fromRadio.setReadAuthorizeCallback(
        fromRadioAuthorizeCb,
        false); // We don't call this callback via the adafruit queue, because we can safely run in the BLE context
    fromRadio.setBuffer(fromRadioBytes, sizeof(fromRadioBytes)); // we preallocate our fromradio buffer so we won't waste space
    // for two copies
    fromRadio.begin();

    toRadio.setProperties(CHR_PROPS_WRITE);
    toRadio.setPermission(SECMODE_OPEN, SECMODE_OPEN); // FIXME secure this!
    toRadio.setFixedLen(0);
    toRadio.setMaxLen(512);
    toRadio.setBuffer(toRadioBytes, sizeof(toRadioBytes));
    toRadio.setWriteCallback(
        toRadioWriteCb,
        false); // We don't call this callback via the adafruit queue, because we can safely run in the BLE context
    toRadio.begin();
}

// FIXME, turn off soft device access for debugging
static bool isSoftDeviceAllowed = true;

void NRF52Bluetooth::setup()
{
    // Initialise the Bluefruit module
    DEBUG_MSG("Initialise the Bluefruit nRF52 module\n");
    Bluefruit.begin();

    // Set the advertised device name (keep it short!)
    Bluefruit.setName(getDeviceName());

    // Set the connect/disconnect callback handlers
    Bluefruit.Periph.setConnectCallback(connect_callback);
    Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

    // Configure and Start the Device Information Service
    DEBUG_MSG("Configuring the Device Information Service\n");
    bledis.setManufacturer(HW_VENDOR);
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
    if (isSoftDeviceAllowed) {
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