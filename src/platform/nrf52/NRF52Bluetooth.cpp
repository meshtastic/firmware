#include "NRF52Bluetooth.h"
#include "BLEDfuSecure.h"
#include "BluetoothCommon.h"
#include "configuration.h"
#include "main.h"
#include "mesh/ble/BluetoothShared.h"
#include "mesh/ble/BluetoothTransport.h"
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
#ifndef BLE_DFU_SECURE
static BLEDfu bledfu; // DFU software update helper service
#else
static BLEDfuSecure bledfusecure;                                             // DFU software update helper service
#endif

// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in
// process at once
// static uint8_t trBytes[_max(_max(_max(_max(ToRadio_size, RadioConfig_size), User_size), MyNodeInfo_size), FromRadio_size)];
static uint8_t fromRadioBytes[meshtastic_FromRadio_size];
static uint8_t toRadioBytes[meshtastic_ToRadio_size];

static void refreshSharedNodePasskey();

#ifdef MODE_SHARED_NODE
static SharedNode::PeerIdentity peerIdentityFromBondAddress(const ble_gap_addr_t &addr, uint32_t irkHash = 0)
{
    SharedNode::PeerIdentity identity;
    if (bluetooth::addressIsEmpty(addr.addr, sizeof(addr.addr))) {
        return identity;
    }

    char buffer[SharedNode::PEER_IDENTITY_SIZE] = {};
    // Bluefruit/SoftDevice gives identity address + IRK in bond_keys_t. Prefix
    // with "bf:" so these records never collide with NimBLE-formatted IDs.
    snprintf(buffer, sizeof(buffer), "bf:%x%x%02x%02x%02x%02x%02x%02x%08lx", addr.addr_id_peer ? 1 : 0, addr.addr_type,
             addr.addr[5], addr.addr[4], addr.addr[3], addr.addr[2], addr.addr[1], addr.addr[0],
             static_cast<unsigned long>(irkHash));
    identity = buffer;
    return identity;
}

static SharedNode::PeerIdentity peerIdentityFromBondKeys(const bond_keys_t &bondKeys)
{
    // Prefer the peer identity record from the bond store. That is stable across
    // resolvable private address rotation and normal reconnects.
    return peerIdentityFromBondAddress(bondKeys.peer_id.id_addr_info,
                                       bluetooth::fnv1a32(bondKeys.peer_id.id_info.irk, sizeof(bondKeys.peer_id.id_info.irk)));
}

static SharedNode::PeerIdentity getPeerIdentity(uint16_t connHandle)
{
    BLEConnection *connection = Bluefruit.Connection(connHandle);
    if (!connection) {
        return SharedNode::PeerIdentity{};
    }

    bond_keys_t bondKeys = {};
    if (connection->loadBondKey(&bondKeys)) {
        return peerIdentityFromBondKeys(bondKeys);
    }

    // Some Bluefruit callbacks can run before loadBondKey() succeeds. If the
    // connection is already bonded, fall back to the peer identity address and
    // resolve the richer IRK-backed form on the secured/completed callback.
    return connection->bonded() ? peerIdentityFromBondAddress(connection->getPeerAddr()) : SharedNode::PeerIdentity{};
}
#endif

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) override
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        if (Bluefruit.connected(connHandle)) {
            LOG_INFO("BLE notify fromNum");
            fromNum.notify32(connHandle, fromRadioNum);
        }
    }

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override { return Bluefruit.connected(connHandle); }

  public:
    explicit BluetoothPhoneAPI(uint16_t connHandle_) : connHandle(connHandle_) { api_type = TYPE_BLE; }

  private:
    uint16_t connHandle;
};

static bluetooth::PhoneApiPool<BluetoothPhoneAPI> bluetoothPhoneApis;
static bluetooth::DuplicateToRadioTracker<> duplicateToRadioTracker;

static BluetoothPhoneAPI* findBluetoothPhoneAPI(uint16_t connHandle)
{
    return bluetoothPhoneApis.find(connHandle);
}

static BluetoothPhoneAPI* ensureBluetoothPhoneAPI(uint16_t connHandle)
{
    return bluetoothPhoneApis.ensure(connHandle);
}

static void removeBluetoothPhoneAPI(uint16_t connHandle)
{
    bluetoothPhoneApis.remove(connHandle);
    duplicateToRadioTracker.clear(connHandle);
}

static void closeAllBluetoothPhoneAPIs()
{
    bluetoothPhoneApis.closeAll();
    duplicateToRadioTracker.clearAll();
}

void onConnect(uint16_t conn_handle)
{
    // Get the reference to current connection
    BLEConnection *connection = Bluefruit.Connection(conn_handle);
#ifdef MODE_SHARED_NODE
    SharedNode::PeerIdentity identity = getPeerIdentity(conn_handle);
    bluetooth::rememberKnownConnection(conn_handle, identity);
    // Bluefruit stores one global passkey. Refresh it after each connection so
    // the next pair request sees the latest admin/guest decision.
    refreshSharedNodePasskey();
#endif
    ensureBluetoothPhoneAPI(conn_handle);
    char central_name[32] = {0};
    if (connection) {
        connection->getPeerName(central_name, sizeof(central_name));
    }
    LOG_INFO("BLE Connected to %s", central_name);

    bluetooth::notifyConnected();
}
/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void onDisconnect(uint16_t conn_handle, uint8_t reason)
{
    LOG_INFO("BLE Disconnected, reason = 0x%x", reason);
    removeBluetoothPhoneAPI(conn_handle);

    const bool anyConnected = Bluefruit.connected() > 0;
    if (!anyConnected) {
#ifdef MODE_SHARED_NODE
        refreshSharedNodePasskey();
#endif
        bluetooth::notifyDisconnected();
    }
}
void onCccd(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t cccd_value)
{
    // Display the raw request packet
    LOG_INFO("CCCD Updated: %u", cccd_value);
    // Check the characteristic this CCCD update is associated with in case
    // this handler is used for multiple CCCD records.

    // According to the GATT spec: cccd value = 0x0001 means notifications are enabled
    // and cccd value = 0x0002 means indications are enabled

    if (chr->uuid == fromNum.uuid || chr->uuid == logRadio.uuid) {
        auto result = cccd_value == 2 ? chr->indicateEnabled(conn_hdl) : chr->notifyEnabled(conn_hdl);
        if (result) {
            LOG_INFO("Notify/Indicate enabled");
        } else {
            LOG_INFO("Notify/Indicate disabled");
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
     * - Interval:  fast mode = 20 ms, slow mode = 417,5 ms
     * - Timeout for fast mode is 30 seconds
     * - Start(timeout) with timeout = 0 will advertise forever (until connected)
     *
     * For recommended advertising interval
     * https://developer.apple.com/library/content/qa/qa1931/_index.html
     */
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 668); // in unit of 0.625 ms
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
    (void)chr; // The characteristic is fixed by this callback registration.
    BluetoothPhoneAPI *phoneApi = findBluetoothPhoneAPI(conn_hdl);
    if (request->offset == 0) {
        // If the read is long, we will get multiple authorize invocations - we only populate data on the first
        size_t numBytes = phoneApi ? phoneApi->getFromRadio(fromRadioBytes) : 0;
        // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
        // or make empty if the queue is empty
        fromRadio.write(fromRadioBytes, numBytes);
    } else {
        // LOG_INFO("Ignore successor read");
    }
    authorizeRead(conn_hdl);
}

void onToRadioWrite(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len)
{
    (void)chr; // The characteristic is fixed by this callback registration.
    BluetoothPhoneAPI *phoneApi = findBluetoothPhoneAPI(conn_hdl);
    if (!phoneApi) {
        LOG_WARN("Drop ToRadio packet for unknown conn=%u", conn_hdl);
        return;
    }
    if (len > MAX_TO_FROM_RADIO_SIZE) {
        LOG_WARN("Drop oversized ToRadio packet for conn=%u len=%u", conn_hdl, len);
        return;
    }

    LOG_INFO("toRadioWriteCb data %p, len %u", data, len);
    if (duplicateToRadioTracker.rememberIfNew(conn_hdl, data, len)) {
        // Duplicate filtering is per connection; otherwise one guest could
        // suppress the first packet from another guest with identical bytes.
        LOG_DEBUG("New ToRadio packet");
        phoneApi->handleToRadio(data, len);
    } else {
        LOG_DEBUG("Drop dup ToRadio packet we just saw");
    }
}

void setupMeshService(void)
{
    meshBleService.begin();
    // Note: You must call .begin() on the BLEService before calling .begin() on
    // any characteristic(s) within that service definition.. Calling .begin() on
    // a BLECharacteristic will cause it to be added to the last BLEService that
    // was 'begin()'ed!
#ifdef MODE_SHARED_NODE
    auto secMode = SECMODE_ENC_NO_MITM;
#else
    auto secMode =
        config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN ? SECMODE_OPEN : SECMODE_ENC_NO_MITM;
#endif
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
static void setBluetoothPasskey(uint32_t passkey, const char *logLabel)
{
    char pinString[7] = {};
    snprintf(pinString, sizeof(pinString), "%06u", passkey);
    LOG_INFO("%s '%i'", logLabel, passkey);
    Bluefruit.Security.setPIN(pinString);
}

static void configureSecurePairing()
{
    Bluefruit.Security.setIOCaps(true, false, false);
    Bluefruit.Security.setPairPasskeyCallback(NRF52Bluetooth::onPairingPasskey);
    Bluefruit.Security.setPairCompleteCallback(NRF52Bluetooth::onPairingCompleted);
    Bluefruit.Security.setSecuredCallback(NRF52Bluetooth::onConnectionSecured);
    meshBleService.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
}

static void configureOpenPairing()
{
    Bluefruit.Security.setIOCaps(false, false, false);
    meshBleService.setPermission(SECMODE_OPEN, SECMODE_OPEN);
}

static void refreshSharedNodePasskey()
{
#ifdef MODE_SHARED_NODE
    // Bluefruit can only hold one active passkey, so refresh it whenever the
    // shared-node pairing state may have changed.
    setBluetoothPasskey(bluetooth::choosePairingPasskey(), "Shared-node Bluetooth pin set to");
#endif
}

void NRF52Bluetooth::shutdown()
{
    // Shutdown bluetooth for minimum power draw
    LOG_INFO("Disable NRF52 bluetooth");
    Bluefruit.Security.setPairPasskeyCallback(NRF52Bluetooth::onUnwantedPairing); // Actively refuse (during factory reset)
    closeAllBluetoothPhoneAPIs();
    disconnect();
    Bluefruit.Advertising.stop();
}
void NRF52Bluetooth::startDisabled()
{
    // Setup Bluetooth
    nrf52Bluetooth->setup();
    // Shutdown bluetooth for minimum power draw
    Bluefruit.Advertising.stop();
    Bluefruit.setTxPower(-40); // Minimum power
    LOG_INFO("Disable NRF52 Bluetooth. (Workaround: tx power min, advertise stopped)");
}
bool NRF52Bluetooth::isConnected()
{
    return Bluefruit.connected() > 0;
}
int NRF52Bluetooth::getRssi()
{
    return 0; // FIXME figure out where to source this
}

// Valid BLE TX power levels as per nRF52840 Product Specification are: "-20 to +8 dBm TX power, configurable in 4 dB steps".
// See https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html
#define VALID_BLE_TX_POWER(x)                                                                                                    \
    ((x) == -20 || (x) == -16 || (x) == -12 || (x) == -8 || (x) == -4 || (x) == 0 || (x) == 4 || (x) == 8)

void NRF52Bluetooth::setup()
{
#ifdef MODE_SHARED_NODE
    bluetooth::enforceSharedNodePairingMode();
#endif

    // Initialise the Bluefruit module
    LOG_INFO("Init the Bluefruit nRF52 module");
    Bluefruit.autoConnLed(false);
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    // Clear existing data.
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
#if defined(NRF52_BLE_TX_POWER) && VALID_BLE_TX_POWER(NRF52_BLE_TX_POWER)
    Bluefruit.setTxPower(NRF52_BLE_TX_POWER);
#endif
#ifdef MODE_SHARED_NODE
    refreshSharedNodePasskey();
#endif
    if (bluetooth::requiresSecurePairing()) {
#ifndef MODE_SHARED_NODE
        setBluetoothPasskey(bluetooth::choosePairingPasskey(), "Bluetooth pin set to");
#endif
        configureSecurePairing();
    } else {
        configureOpenPairing();
    }
    // Set the advertised device name (keep it short!)
    Bluefruit.setName(getDeviceName());
    // Set the connect/disconnect callback handlers
    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);

    // Do not change Slave Latency to value other than 0 !!!
    // There is probably a bug in SoftDevice + certain Apple iOS versions being
    // brain damaged causing connectivity problems.

    // On one side it seems SoftDevice is using SlaveLatency value even
    // if connection parameter negotation failed and phone sees it as connectivity errors.

    // On the other hand Apple can randomly refuse any parameter negotiation and shutdown connection
    // even if you meet Apple Developer Guidelines for BLE devices. Because f* you, that's why.

    // While this API call sets preferred connection parameters (PPCP) - many phones ignore it (yeah) and it seems SoftDevice
    // will try to renegotiate connection parameters based on those values after phone connection.
    // So those are relatively safe values so Apple braindead firmware won't get angry and at least we may try
    // to negotiate some longer connection interval to save battery.

    // See https://github.com/meshtastic/firmware/pull/8858 for measurements.  We are dealing with microamp savings anyway so not
    // worth dying on a hill here.

    Bluefruit.Periph.setConnSlaveLatency(0);
    // 1.25 ms units - so min, max is 15, 100 ms range.
    Bluefruit.Periph.setConnInterval(12, 80);

#ifndef BLE_DFU_SECURE
    bledfu.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
    bledfu.begin(); // Install the DFU helper
#else
    bledfusecure.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM); // add by WayenWeng
    bledfusecure.begin();                                                     // Install the DFU helper
#endif
    // Configure and Start the Device Information Service
    LOG_INFO("Init the Device Information Service");
    bledis.setModel(optstr(HW_VERSION));
    bledis.setFirmwareRev(optstr(APP_VERSION));
    bledis.begin();
    // Start the BLE Battery Service and set it to 100%
    LOG_INFO("Init the Battery Service");
    blebas.begin();
    blebas.write(0); // Unknown battery level for now
    // Setup the Heart Rate Monitor service using
    // BLEService and BLECharacteristic classes
    LOG_INFO("Init the Mesh bluetooth service");
    setupMeshService();
    // Setup the advertising packet(s)
    LOG_INFO("Set up the advertising payload(s)");
    startAdv();
    LOG_INFO("Advertise");
}
void NRF52Bluetooth::resumeAdvertising()
{
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 668); // in unit of 0.625 ms
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
#ifdef MODE_SHARED_NODE
    if (!bluetooth::canClearKnownClients("clearBonds")) {
        return;
    }
#endif
    LOG_INFO("Clear bluetooth bonds!");
    closeAllBluetoothPhoneAPIs();
#ifdef MODE_SHARED_NODE
    bluetooth::clearKnownClients();
#endif
    bond_print_list(BLE_GAP_ROLE_PERIPH);
    bond_print_list(BLE_GAP_ROLE_CENTRAL);
    Bluefruit.Periph.clearBonds();
    Bluefruit.Central.clearBonds();
}
void NRF52Bluetooth::onConnectionSecured(uint16_t conn_handle)
{
    LOG_INFO("BLE connection secured");
#ifdef MODE_SHARED_NODE
    SharedNode::PeerIdentity identity = getPeerIdentity(conn_handle);
    // Secured can fire before pair-complete on some SoftDevice flows. It is
    // safe to resolve here; the policy reuses known identity/pending slot.
    bluetooth::resolveConnectionSlot(conn_handle, identity);
    ensureBluetoothPhoneAPI(conn_handle);
#endif
}
bool NRF52Bluetooth::onPairingPasskey(uint16_t conn_handle, uint8_t const passkey[6], bool match_request)
{
    char passkey1[4] = {passkey[0], passkey[1], passkey[2], '\0'};
    char passkey2[4] = {passkey[3], passkey[4], passkey[5], '\0'};
    LOG_INFO("BLE pair process started with passkey %s %s", passkey1, passkey2);

    // Get passkey as string
    // Note: possible leading zeros
    char textkey[7] = {};
    for (uint8_t i = 0; i < 6; i++) {
        textkey[i] = static_cast<char>(passkey[i]);
    }

    bluetooth::showPairingPrompt(textkey);

    if (match_request) {
        uint32_t start_time = millis();
        while (millis() < start_time + 30000) {
            if (!Bluefruit.connected(conn_handle))
                break;
        }
    }
    LOG_INFO("BLE passkey pair: match_request=%i", match_request);
    return true;
}

// Actively refuse new BLE pairings
// After clearing bonds (at factory reset), clients seem initially able to attempt to re-pair, even with advertising disabled.
// On NRF52Bluetooth::shutdown, we change the pairing callback to this method, to aggressively refuse any connection attempts.
bool NRF52Bluetooth::onUnwantedPairing(uint16_t conn_handle, uint8_t const passkey[6], bool match_request)
{
    NRF52Bluetooth::disconnect();
    return false;
}

// Disconnect any BLE connections
void NRF52Bluetooth::disconnect()
{
    uint8_t connection_num = Bluefruit.connected();
    if (connection_num) {
        // Close all connections. We're only expecting one.
        for (uint8_t i = 0; i < connection_num; i++)
            Bluefruit.disconnect(i);

        // Wait for disconnection
        while (Bluefruit.connected())
            yield();

        LOG_INFO("Ended BLE connection");
    }
}

void NRF52Bluetooth::onPairingCompleted(uint16_t conn_handle, uint8_t auth_status)
{
    if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS) {
        LOG_INFO("BLE pair success");
#ifdef MODE_SHARED_NODE
        const SharedNode::PeerIdentity identity = getPeerIdentity(conn_handle);
        // Pair-complete is the final chance to bind the passkey-reserved
        // slot to the durable Bluefruit bond identity.
        const uint8_t slot = bluetooth::resolveConnectionSlot(conn_handle, identity);
        bluetooth::logResolvedPairingSlot(slot);
        ensureBluetoothPhoneAPI(conn_handle);
#endif
        bluetooth::notifyConnected();
    } else {
        LOG_INFO("BLE pair failed");
#ifdef MODE_SHARED_NODE
        // Drop the reserved admin/guest decision so the next passkey request
        // starts with a fresh slot choice.
        bluetooth::consumePendingPairingSlot();
#endif
        bluetooth::notifyDisconnected();
    }

    bluetooth::clearPairingPrompt();
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
