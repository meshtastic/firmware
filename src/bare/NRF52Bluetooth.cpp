#include "NRF52Bluetooth.h"
#include "configuration.h"
#include <bluefruit.h>

/* HRM Service Definitions
 * Heart Rate Monitor Service:  0x180D
 * Heart Rate Measurement Char: 0x2A37
 * Body Sensor Location Char:   0x2A38
 */
BLEService hrms = BLEService(UUID16_SVC_HEART_RATE);
BLECharacteristic hrmc = BLECharacteristic(UUID16_CHR_HEART_RATE_MEASUREMENT);
BLECharacteristic bslc = BLECharacteristic(UUID16_CHR_BODY_SENSOR_LOCATION);

BLEDis bledis; // DIS (Device Information Service) helper class instance
BLEBas blebas; // BAS (Battery Service) helper class instance

uint8_t bps = 0;

void connect_callback(uint16_t conn_handle)
{
    // Get the reference to current connection
    BLEConnection *connection = Bluefruit.Connection(conn_handle);

    char central_name[32] = {0};
    connection->getPeerName(central_name, sizeof(central_name));

    DEBUG_MSG("Connected to %s\n", central_name);
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;
    (void)reason;

    DEBUG_MSG("Disconnected, reason = 0x%x\n", reason);
}

void cccd_callback(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t cccd_value)
{
    // Display the raw request packet
    DEBUG_MSG("CCCD Updated: %u\n", cccd_value);

    // Check the characteristic this CCCD update is associated with in case
    // this handler is used for multiple CCCD records.
    if (chr->uuid == hrmc.uuid) {
        if (chr->notifyEnabled(conn_hdl)) {
            DEBUG_MSG("Heart Rate Measurement 'Notify' enabled\n");
        } else {
            DEBUG_MSG("Heart Rate Measurement 'Notify' disabled\n");
        }
    }
}

void startAdv(void)
{
    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();

    // Include HRM Service UUID
    Bluefruit.Advertising.addService(hrms);

    // Include Name
    Bluefruit.Advertising.addName();

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
    Bluefruit.Advertising.start(0);             // 0 = Don't stop advertising after n seconds
}

void setupHRM(void)
{
    // Configure the Heart Rate Monitor service
    // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.service.heart_rate.xml
    // Supported Characteristics:
    // Name                         UUID    Requirement Properties
    // ---------------------------- ------  ----------- ----------
    // Heart Rate Measurement       0x2A37  Mandatory   Notify
    // Body Sensor Location         0x2A38  Optional    Read
    // Heart Rate Control Point     0x2A39  Conditional Write       <-- Not used here
    hrms.begin();

    // Note: You must call .begin() on the BLEService before calling .begin() on
    // any characteristic(s) within that service definition.. Calling .begin() on
    // a BLECharacteristic will cause it to be added to the last BLEService that
    // was 'begin()'ed!

    // Configure the Heart Rate Measurement characteristic
    // See:
    // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.heart_rate_measurement.xml
    // Properties = Notify
    // Min Len    = 1
    // Max Len    = 8
    //    B0      = UINT8  - Flag (MANDATORY)
    //      b5:7  = Reserved
    //      b4    = RR-Internal (0 = Not present, 1 = Present)
    //      b3    = Energy expended status (0 = Not present, 1 = Present)
    //      b1:2  = Sensor contact status (0+1 = Not supported, 2 = Supported but contact not detected, 3 = Supported and
    //      detected) b0    = Value format (0 = UINT8, 1 = UINT16)
    //    B1      = UINT8  - 8-bit heart rate measurement value in BPM
    //    B2:3    = UINT16 - 16-bit heart rate measurement value in BPM
    //    B4:5    = UINT16 - Energy expended in joules
    //    B6:7    = UINT16 - RR Internal (1/1024 second resolution)
    hrmc.setProperties(CHR_PROPS_NOTIFY);
    hrmc.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    hrmc.setFixedLen(2);
    hrmc.setCccdWriteCallback(cccd_callback); // Optionally capture CCCD updates
    hrmc.begin();
    uint8_t hrmdata[2] = {0b00000110, 0x40}; // Set the characteristic to use 8-bit values, with the sensor connected and detected
    hrmc.write(hrmdata, 2);

    // Configure the Body Sensor Location characteristic
    // See:
    // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.body_sensor_location.xml
    // Properties = Read
    // Min Len    = 1
    // Max Len    = 1
    //    B0      = UINT8 - Body Sensor Location
    //      0     = Other
    //      1     = Chest
    //      2     = Wrist
    //      3     = Finger
    //      4     = Hand
    //      5     = Ear Lobe
    //      6     = Foot
    //      7:255 = Reserved
    bslc.setProperties(CHR_PROPS_READ);
    bslc.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    bslc.setFixedLen(1);
    bslc.begin();
    bslc.write8(2); // Set the characteristic to 'Wrist' (2)
}

void NRF52Bluetooth::setup()
{
    // Initialise the Bluefruit module
    DEBUG_MSG("Initialise the Bluefruit nRF52 module\n");
    Bluefruit.begin();

    // Set the advertised device name (keep it short!)
    Bluefruit.setName("Meshtastic52"); // FIXME

    // Set the connect/disconnect callback handlers
    Bluefruit.Periph.setConnectCallback(connect_callback);
    Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

    // Configure and Start the Device Information Service
    DEBUG_MSG("Configuring the Device Information Service\n");
    bledis.setManufacturer("meshtastic.org");
    bledis.setModel("NRF52-meshtastic"); // FIXME
    bledis.begin();

    // Start the BLE Battery Service and set it to 100%
    DEBUG_MSG("Configuring the Battery Service\n");
    blebas.begin();
    blebas.write(42); // FIXME, report real power levels

    // Setup the Heart Rate Monitor service using
    // BLEService and BLECharacteristic classes
    DEBUG_MSG("Configuring the Heart Rate Monitor Service\n");
    setupHRM();

    // Setup the advertising packet(s)
    DEBUG_MSG("Setting up the advertising payload(s)\n");
    startAdv();

    DEBUG_MSG("Advertising\n");
}

/*
void loop()
{
  digitalToggle(LED_RED);

  if ( Bluefruit.connected() ) {
    uint8_t hrmdata[2] = { 0b00000110, bps++ };           // Sensor connected, increment BPS value

    // Note: We use .notify instead of .write!
    // If it is connected but CCCD is not enabled
    // The characteristic's value is still updated although notification is not sent
    if ( hrmc.notify(hrmdata, sizeof(hrmdata)) ){
      Serial.print("Heart Rate Measurement updated to: "); Serial.println(bps);
    }else{
      Serial.println("ERROR: Notify not set in the CCCD or not connected!");
    }
  }

  // Only send update once per second
  delay(1000);
}
*/