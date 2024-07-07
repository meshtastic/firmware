#include "NRF52Bluetooth.h"
#include "BluetoothCommon.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"
#include <bluefruit.h>
#include <utility/bonding.h>
static BLEService meshBleService = BLEService(BLEUuid(MESH_SERVICE_UUID_16));
static BLECharacteristic fromNum = BLECharacteristic(BLEUuid(FROMNUM_UUID_16));
static BLECharacteristic fromRadio = BLECharacteristic(BLEUuid(FROMRADIO_UUID_16));
static BLECharacteristic toRadio = BLECharacteristic(BLEUuid(TORADIO_UUID_16));
static BLECharacteristic logRadio = BLECharacteristic(BLEUuid(LOGRADIO_UUID_16));

static BLEDis bledis; // DIS (Device Information Service) helper class instance
static BLEBas blebas; // BAS (Battery Service) helper class instance

static BLEDfu bledfu; // DFU software update helper service
// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in
// process at once
// static uint8_t trBytes[_max(_max(_max(_max(ToRadio_size, RadioConfig_size), User_size), MyNodeInfo_size), FromRadio_size)];
static uint8_t fromRadioBytes[meshtastic_FromRadio_size];
static uint8_t toRadioBytes[meshtastic_ToRadio_size];

static uint16_t connectionHandle;

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) override
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        LOG_INFO("BLE notify fromNum\n");
        fromNum.notify32(fromRadioNum);
    }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override
    {
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
    LOG_INFO("BLE Connected to %s\n", central_name);
}
/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void onDisconnect(uint16_t conn_handle, uint8_t reason)
{
    // FIXME - we currently assume only one active connection
    LOG_INFO("BLE Disconnected, reason = 0x%x\n", reason);
}
void onCccd(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t cccd_value)
{
    // Display the raw request packet
    LOG_INFO("CCCD Updated: %u\n", cccd_value);
    // Check the characteristic this CCCD update is associated with in case
    // this handler is used for multiple CCCD records.

    // According to the GATT spec: cccd value = 0x0001 means notifications are enabled
    // and cccd value = 0x0002 means indications are enabled

    if (chr->uuid == fromNum.uuid || chr->uuid == logRadio.uuid) {
        auto result = cccd_value == 2 ? chr->indicateEnabled(conn_hdl) : chr->notifyEnabled(conn_hdl);
        if (result) {
            LOG_INFO("Notify/Indicate enabled\n");
        } else {
            LOG_INFO("Notify/Indicate disabled\n");
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
        // LOG_INFO("Ignoring successor read\n");
    }
    authorizeRead(conn_hdl);
}
void onToRadioWrite(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len)
{
    LOG_INFO("toRadioWriteCb data %p, len %u\n", data, len);
    bluetoothPhoneAPI->handleToRadio(data, len);
}

void setupMeshService(void)
{
    bluetoothPhoneAPI = new BluetoothPhoneAPI();
    meshBleService.begin();
    // Note: You must call .begin() on the BLEService before calling .begin() on
    // any characteristic(s) within that service definition.. Calling .begin() on
    // a BLECharacteristic will cause it to be added to the last BLEService that
    // was 'begin()'ed!
    auto secMode =
        config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN ? SECMODE_OPEN : SECMODE_ENC_NO_MITM;
    fromNum.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    fromNum.setPermission(secMode, SECMODE_NO_ACCESS); // FIXME, secure this!!!
    fromNum.setFixedLen(
        0); // Variable len (either 0 or 4)  FIXME consider changing protocol so it is fixed 4 byte len, where 0 means empty
    fromNum.setMaxLen(4);
    fromNum.setCccdWriteCallback(onCccd); // Optionally capture CCCD updates
    // We don't yet need to hook the fromNum auth callback
    // fromNum.setReadAuthorizeCallback(fromNumAuthorizeCb);
    fromNum.write32(0); // Provide default fromNum of 0
    fromNum.begin();

    fromRadio.setProperties(CHR_PROPS_READ);
    fromRadio.setPermission(secMode, SECMODE_NO_ACCESS);
    fromRadio.setMaxLen(sizeof(fromRadioBytes));
    fromRadio.setReadAuthorizeCallback(
        onFromRadioAuthorize,
        false); // We don't call this callback via the adafruit queue, because we can safely run in the BLE context
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

    logRadio.setProperties(CHR_PROPS_INDICATE | CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    logRadio.setPermission(secMode, SECMODE_NO_ACCESS);
    logRadio.setMaxLen(512);
    logRadio.setCccdWriteCallback(onCccd);
    logRadio.write32(0);
    logRadio.begin();
}
static uint32_t configuredPasskey;
void NRF52Bluetooth::shutdown()
{
    // Shutdown bluetooth for minimum power draw
    LOG_INFO("Disable NRF52 bluetooth\n");
    if (connectionHandle != 0) {
        Bluefruit.disconnect(connectionHandle);
    }
    Bluefruit.Advertising.stop();
}
void NRF52Bluetooth::startDisabled()
{
    // Setup Bluetooth
    nrf52Bluetooth->setup();
    // Shutdown bluetooth for minimum power draw
    Bluefruit.Advertising.stop();
    Bluefruit.setTxPower(-40); // Minimum power
    LOG_INFO("Disabling NRF52 Bluetooth. (Workaround: tx power min, advertising stopped)\n");
}
bool NRF52Bluetooth::isConnected()
{
    return Bluefruit.connected(connectionHandle);
}
int NRF52Bluetooth::getRssi()
{
    return 0; // FIXME figure out where to source this
}
void NRF52Bluetooth::setup()
{
    // Initialise the Bluefruit module
    LOG_INFO("Initialize the Bluefruit nRF52 module\n");
    Bluefruit.autoConnLed(false);
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    // Clear existing data.
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
    if (config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        configuredPasskey = config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN
                                ? config.bluetooth.fixed_pin
                                : random(100000, 999999);
        auto pinString = std::to_string(configuredPasskey);
        LOG_INFO("Bluetooth pin set to '%i'\n", configuredPasskey);
        Bluefruit.Security.setPIN(pinString.c_str());
        Bluefruit.Security.setIOCaps(true, false, false);
        Bluefruit.Security.setPairPasskeyCallback(NRF52Bluetooth::onPairingPasskey);
        Bluefruit.Security.setPairCompleteCallback(NRF52Bluetooth::onPairingCompleted);
        Bluefruit.Security.setSecuredCallback(NRF52Bluetooth::onConnectionSecured);
        meshBleService.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
    } else {
        Bluefruit.Security.setIOCaps(false, false, false);
        meshBleService.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    }
    // Set the advertised device name (keep it short!)
    Bluefruit.setName(getDeviceName());
    // Set the connect/disconnect callback handlers
    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);
    bledfu.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
    bledfu.begin(); // Install the DFU helper
    // Configure and Start the Device Information Service
    LOG_INFO("Configuring the Device Information Service\n");
    bledis.setModel(optstr(HW_VERSION));
    bledis.setFirmwareRev(optstr(APP_VERSION));
    bledis.begin();
    // Start the BLE Battery Service and set it to 100%
    LOG_INFO("Configuring the Battery Service\n");
    blebas.begin();
    blebas.write(0); // Unknown battery level for now
    // Setup the Heart Rate Monitor service using
    // BLEService and BLECharacteristic classes
    LOG_INFO("Configuring the Mesh bluetooth service\n");
    setupMeshService();
    // Setup the advertising packet(s)
    LOG_INFO("Setting up the advertising payload(s)\n");
    startAdv();
    LOG_INFO("Advertising\n");
}
void NRF52Bluetooth::resumeAdvertising()
{
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);   // number of seconds in fast mode
    Bluefruit.Advertising.start(0);
}
/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level)
{
    blebas.write(level);
}
void NRF52Bluetooth::clearBonds()
{
    LOG_INFO("Clearing bluetooth bonds!\n");
    bond_print_list(BLE_GAP_ROLE_PERIPH);
    bond_print_list(BLE_GAP_ROLE_CENTRAL);
    Bluefruit.Periph.clearBonds();
    Bluefruit.Central.clearBonds();
}
void NRF52Bluetooth::onConnectionSecured(uint16_t conn_handle)
{
    LOG_INFO("BLE connection secured\n");
}
bool NRF52Bluetooth::onPairingPasskey(uint16_t conn_handle, uint8_t const passkey[6], bool match_request)
{
    LOG_INFO("BLE pairing process started with passkey %.3s %.3s\n", passkey, passkey + 3);
    powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
    screen->startAlert([](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) -> void {
        char btPIN[16] = "888888";
        snprintf(btPIN, sizeof(btPIN), "%06u", configuredPasskey);
        int x_offset = display->width() / 2;
        int y_offset = display->height() <= 80 ? 0 : 32;
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_MEDIUM);
        display->drawString(x_offset + x, y_offset + y, "Bluetooth");

        display->setFont(FONT_SMALL);
        y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_MEDIUM - 4 : y_offset + FONT_HEIGHT_MEDIUM + 5;
        display->drawString(x_offset + x, y_offset + y, "Enter this code");

        display->setFont(FONT_LARGE);
        String displayPin(btPIN);
        String pin = displayPin.substring(0, 3) + " " + displayPin.substring(3, 6);
        y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_SMALL - 5 : y_offset + FONT_HEIGHT_SMALL + 5;
        display->drawString(x_offset + x, y_offset + y, pin);

        display->setFont(FONT_SMALL);
        String deviceName = "Name: ";
        deviceName.concat(getDeviceName());
        y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_LARGE - 6 : y_offset + FONT_HEIGHT_LARGE + 5;
        display->drawString(x_offset + x, y_offset + y, deviceName);
    });
    if (match_request) {
        uint32_t start_time = millis();
        while (millis() < start_time + 30000) {
            if (!Bluefruit.connected(conn_handle))
                break;
        }
    }
    LOG_INFO("BLE passkey pairing: match_request=%i\n", match_request);
    return true;
}
void NRF52Bluetooth::onPairingCompleted(uint16_t conn_handle, uint8_t auth_status)
{
    if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS)
        LOG_INFO("BLE pairing success\n");
    else
        LOG_INFO("BLE pairing failed\n");
    screen->endAlert();
}

void NRF52Bluetooth::sendLog(const uint8_t *logMessage, size_t length)
{
    if (!isConnected() || length > 512)
        return;
    if (logRadio.indicateEnabled())
        logRadio.indicate(logMessage, (uint16_t)length);
    else
        logRadio.notify(logMessage, (uint16_t)length);
}