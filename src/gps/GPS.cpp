#include "GPS.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "sleep.h"
#include "ubx.h"

#ifdef ARCH_PORTDUINO
#include "meshUtils.h"
#endif

#ifndef GPS_RESET_MODE
#define GPS_RESET_MODE HIGH
#endif

// If we have a serial GPS port it will not be null
#ifdef GPS_SERIAL_NUM
HardwareSerial _serial_gps_real(GPS_SERIAL_NUM);
HardwareSerial *GPS::_serial_gps = &_serial_gps_real;
#elif defined(NRF52840_XXAA) || defined(NRF52833_XXAA)
// Assume NRF52840
HardwareSerial *GPS::_serial_gps = &Serial1;
#else
HardwareSerial *GPS::_serial_gps = NULL;
#endif

GPS *gps;

/// Multiple GPS instances might use the same serial port (in sequence), but we can
/// only init that port once.
static bool didSerialInit;

struct uBloxGnssModelInfo info;
uint8_t uBloxProtocolVersion;

void GPS::UBXChecksum(uint8_t *message, size_t length)
{
    uint8_t CK_A = 0, CK_B = 0;

    // Calculate the checksum, starting from the CLASS field (which is message[2])
    for (size_t i = 2; i < length - 2; i++) {
        CK_A = (CK_A + message[i]) & 0xFF;
        CK_B = (CK_B + CK_A) & 0xFF;
    }

    // Place the calculated checksum values in the message
    message[length - 2] = CK_A;
    message[length - 1] = CK_B;
}

// Function to create a ublox packet for editing in memory
uint8_t GPS::makeUBXPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg)
{
    // Construct the UBX packet
    UBXscratch[0] = 0xB5;         // header
    UBXscratch[1] = 0x62;         // header
    UBXscratch[2] = class_id;     // class
    UBXscratch[3] = msg_id;       // id
    UBXscratch[4] = payload_size; // length
    UBXscratch[5] = 0x00;

    UBXscratch[6 + payload_size] = 0x00; // CK_A
    UBXscratch[7 + payload_size] = 0x00; // CK_B

    for (int i = 0; i < payload_size; i++) {
        UBXscratch[6 + i] = pgm_read_byte(&msg[i]);
    }
    UBXChecksum(UBXscratch, (payload_size + 8));
    return (payload_size + 8);
}

GPS_RESPONSE GPS::getACK(const char *message, uint32_t waitMillis)
{
    uint8_t buffer[768] = {0};
    uint8_t b;
    int bytesRead = 0;
    uint32_t startTimeout = millis() + waitMillis;
    while (millis() < startTimeout) {
        if (_serial_gps->available()) {
            b = _serial_gps->read();
            buffer[bytesRead] = b;
            bytesRead++;
            if ((bytesRead == 767) || (b == '\r')) {
                if (strnstr((char *)buffer, message, bytesRead) != nullptr) {
#ifdef GPS_DEBUG
                    buffer[bytesRead] = '\0';
                    LOG_DEBUG("%s\r", (char *)buffer);
#endif
                    return GNSS_RESPONSE_OK;
                } else {
#ifdef GPS_DEBUG
                    buffer[bytesRead] = '\0';
                    LOG_INFO("Bytes read:%s\n", (char *)buffer);
#endif
                    bytesRead = 0;
                }
            }
        }
    }
#ifdef GPS_DEBUG
    buffer[bytesRead] = '\0';
    LOG_INFO("Bytes read:%s\n", (char *)buffer);
#endif
    return GNSS_RESPONSE_NONE;
}

GPS_RESPONSE GPS::getACK(uint8_t class_id, uint8_t msg_id, uint32_t waitMillis)
{
    uint8_t b;
    uint8_t ack = 0;
    const uint8_t ackP[2] = {class_id, msg_id};
    uint8_t buf[10] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t startTime = millis();
    const char frame_errors[] = "More than 100 frame errors";
    int sCounter = 0;

    for (int j = 2; j < 6; j++) {
        buf[8] += buf[j];
        buf[9] += buf[8];
    }

    for (int j = 0; j < 2; j++) {
        buf[6 + j] = ackP[j];
        buf[8] += buf[6 + j];
        buf[9] += buf[8];
    }

    while (millis() - startTime < waitMillis) {
        if (ack > 9) {
#ifdef GPS_DEBUG
            LOG_DEBUG("\n");
            LOG_INFO("Got ACK for class %02X message %02X in %d millis.\n", class_id, msg_id, millis() - startTime);
#endif
            return GNSS_RESPONSE_OK; // ACK received
        }
        if (_serial_gps->available()) {
            b = _serial_gps->read();
            if (b == frame_errors[sCounter]) {
                sCounter++;
                if (sCounter == 26) {
                    return GNSS_RESPONSE_FRAME_ERRORS;
                }
            } else {
                sCounter = 0;
            }
#ifdef GPS_DEBUG
            LOG_DEBUG("%02X", b);
#endif
            if (b == buf[ack]) {
                ack++;
            } else {
                if (ack == 3 && b == 0x00) { // UBX-ACK-NAK message
#ifdef GPS_DEBUG
                    LOG_DEBUG("\n");
#endif
                    LOG_WARN("Got NAK for class %02X message %02X\n", class_id, msg_id);
                    return GNSS_RESPONSE_NAK; // NAK received
                }
                ack = 0; // Reset the acknowledgement counter
            }
        }
    }
#ifdef GPS_DEBUG
    LOG_DEBUG("\n");
    LOG_WARN("No response for class %02X message %02X\n", class_id, msg_id);
#endif
    return GNSS_RESPONSE_NONE; // No response received within timeout
}

/**
 * @brief
 * @note   New method, this method can wait for the specified class and message ID, and return the payload
 * @param  *buffer: The message buffer, if there is a response payload message, it will be returned through the buffer parameter
 * @param  size:    size of buffer
 * @param  requestedClass:  request class constant
 * @param  requestedID:     request message ID constant
 * @retval length of payload message
 */
int GPS::getACK(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID, uint32_t waitMillis)
{
    uint16_t ubxFrameCounter = 0;
    uint32_t startTime = millis();
    uint16_t needRead;

    while (millis() - startTime < waitMillis) {
        if (_serial_gps->available()) {
            int c = _serial_gps->read();
            switch (ubxFrameCounter) {
            case 0:
                // ubxFrame 'μ'
                if (c == 0xB5) {
                    ubxFrameCounter++;
                }
                break;
            case 1:
                // ubxFrame 'b'
                if (c == 0x62) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 2:
                // Class
                if (c == requestedClass) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 3:
                // Message ID
                if (c == requestedID) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 4:
                // Payload length lsb
                needRead = c;
                ubxFrameCounter++;
                break;
            case 5:
                // Payload length msb
                needRead |= (c << 8);
                ubxFrameCounter++;
                // Check for buffer overflow
                if (needRead >= size) {
                    ubxFrameCounter = 0;
                    break;
                }
                if (_serial_gps->readBytes(buffer, needRead) != needRead) {
                    ubxFrameCounter = 0;
                } else {
                    // return payload length
#ifdef GPS_DEBUG
                    LOG_INFO("Got ACK for class %02X message %02X in %d millis.\n", requestedClass, requestedID,
                             millis() - startTime);
#endif
                    return needRead;
                }
                break;

            default:
                break;
            }
        }
    }
    // LOG_WARN("No response for class %02X message %02X\n", requestedClass, requestedID);
    return 0;
}

bool GPS::setup()
{
    int msglen = 0;
    // Master power for the GPS
#ifdef PIN_GPS_PPS
    // pulse per second
    pinMode(PIN_GPS_PPS, INPUT);
#endif

// Currently disabled per issue #525 (TinyGPS++ crash bug)
// when fixed upstream, can be un-disabled to enable 3D FixType and PDOP
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // see NMEAGPS.h
    gsafixtype.begin(reader, NMEA_MSG_GXGSA, 2);
    gsapdop.begin(reader, NMEA_MSG_GXGSA, 15);
    LOG_DEBUG("Using " NMEA_MSG_GXGSA " for 3DFIX and PDOP\n");
#endif

#if defined(HAS_PMU) || defined(PIN_GPS_EN)
    if (config.position.gps_enabled) {
#ifdef PIN_GPS_EN
        pinMode(PIN_GPS_EN, OUTPUT);
#endif
        setGPSPower(true);
    }
#endif

#ifdef PIN_GPS_RESET
    digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE); // assert for 10ms
    pinMode(PIN_GPS_RESET, OUTPUT);
    delay(10);
    digitalWrite(PIN_GPS_RESET, !GPS_RESET_MODE);
#endif
    setAwake(true); // Wake GPS power before doing any init

    if (_serial_gps && !didSerialInit) {
        if (!GPSInitStarted) {
            GPSInitStarted = true;
#ifdef ARCH_ESP32
            // In esp32 framework, setRxBufferSize needs to be initialized before Serial
            _serial_gps->setRxBufferSize(SERIAL_BUFFER_SIZE); // the default is 256
#endif

            // if the overrides are not dialled in, set them from the board definitions, if they exist

#if defined(GPS_RX_PIN)
            if (!config.position.rx_gpio)
                config.position.rx_gpio = GPS_RX_PIN;
#endif
#if defined(GPS_TX_PIN)
            if (!config.position.tx_gpio)
                config.position.tx_gpio = GPS_TX_PIN;
#endif

// #define BAUD_RATE 115200
//  ESP32 has a special set of parameters vs other arduino ports
#if defined(ARCH_ESP32)
            if (config.position.rx_gpio) {
                LOG_DEBUG("Using GPIO%d for GPS RX\n", config.position.rx_gpio);
                LOG_DEBUG("Using GPIO%d for GPS TX\n", config.position.tx_gpio);
                _serial_gps->begin(GPS_BAUDRATE, SERIAL_8N1, config.position.rx_gpio, config.position.tx_gpio);
            }
#else
            _serial_gps->begin(GPS_BAUDRATE);
#endif

            /*
             * T-Beam-S3-Core will be preset to use gps Probe here, and other boards will not be changed first
             */
#if defined(GPS_UC6580)
            _serial_gps->updateBaudRate(115200);
            gnssModel = GNSS_MODEL_UC6850;
#else
        }
        LOG_DEBUG("Probing for GPS at %d \n", serialSpeeds[speedSelect]);
        gnssModel = probe(serialSpeeds[speedSelect]);
        if (gnssModel == GNSS_MODEL_UNKNOWN) {
            if (++speedSelect == sizeof(serialSpeeds) / sizeof(int)) {
                speedSelect = 0;
                if (--probeTries == 0) {
                    LOG_WARN("Giving up on GPS probe and setting to 9600.\n");
                    return true;
                }
            }
            return false;
#endif
        }

        if (gnssModel == GNSS_MODEL_MTK) {
            /*
             * t-beam-s3-core uses the same L76K GNSS module as t-echo.
             * Unlike t-echo, L76K uses 9600 baud rate for communication by default.
             * */
            // _serial_gps->begin(9600);    //The baud rate of 9600 has been initialized at the beginning of setupGPS, this
            // line is the redundant part delay(250);

            // Initialize the L76K Chip, use GPS + GLONASS + BEIDOU
            _serial_gps->write("$PCAS04,7*1E\r\n");
            delay(250);
            // only ask for RMC and GGA
            _serial_gps->write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
            delay(250);
            // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
            _serial_gps->write("$PCAS11,3*1E\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_UC6850) {

            // use GPS + GLONASS
            _serial_gps->write("$CFGSYS,h15\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_UBLOX) {
            // Configure GNSS system to GPS+SBAS+GLONASS (Module may restart after this command)
            // We need set it because by default it is GPS only, and we want to use GLONASS too
            // Also we need SBAS for better accuracy and extra features
            // ToDo: Dynamic configure GNSS systems depending of LoRa region

            if (strncmp(info.hwVersion, "00040007", 8) !=
                0) { // The original ublox 6 is GPS only and doesn't support the UBX-CFG-GNSS message
                if (strncmp(info.hwVersion, "00070000", 8) == 0) { // Max7 seems to only support GPS *or* GLONASS
                    LOG_DEBUG("Setting GPS+SBAS\n");
                    msglen = makeUBXPacket(0x06, 0x3e, sizeof(_message_GNSS_7), _message_GNSS_7);
                    _serial_gps->write(UBXscratch, msglen);
                } else {
                    msglen = makeUBXPacket(0x06, 0x3e, sizeof(_message_GNSS), _message_GNSS);
                    _serial_gps->write(UBXscratch, msglen);
                }

                if (getACK(0x06, 0x3e, 800) == GNSS_RESPONSE_NAK) {
                    // It's not critical if the module doesn't acknowledge this configuration.
                    // The module should operate adequately with its factory or previously saved settings.
                    // It appears that there is a firmware bug in some GPS modules: When an attempt is made
                    // to overwrite a saved state with identical values, no ACK/NAK is received, contrary to
                    // what is specified in the Ublox documentation.
                    // There is also a possibility that the module may be GPS-only.
                    LOG_INFO("Unable to reconfigure GNSS - defaults maintained. Is this module GPS-only?\n");
                } else {
                    if (strncmp(info.hwVersion, "00070000", 8) == 0) {
                        LOG_INFO("GNSS configured for GPS+SBAS. Pause for 0.75s before sending next command.\n");
                    } else {
                        LOG_INFO("GNSS configured for GPS+SBAS+GLONASS. Pause for 0.75s before sending next command.\n");
                    }
                    // Documentation say, we need wait atleast 0.5s after reconfiguration of GNSS module, before sending next
                    // commands
                    delay(750);
                }
            }

            // Enable interference resistance, because we are using LoRa, WiFi and Bluetooth on same board,
            // and we need to reduce interference from them
            msglen = makeUBXPacket(0x06, 0x39, sizeof(_message_JAM), _message_JAM);
            _serial_gps->write(UBXscratch, msglen);
            if (getACK(0x06, 0x39, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to enable interference resistance.\n");
            }

            // Configure navigation engine expert settings:
            msglen = makeUBXPacket(0x06, 0x23, sizeof(_message_NAVX5), _message_NAVX5);
            _serial_gps->write(UBXscratch, msglen);
            if (getACK(0x06, 0x23, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to configure extra settings.\n");
            }

            // ublox-M10S can be compatible with UBLOX traditional protocol, so the following sentence settings are also valid

            // Set GPS update rate to 1Hz
            // Lowering the update rate helps to save power.
            // Additionally, for some new modules like the M9/M10, an update rate lower than 5Hz
            // is recommended to avoid a known issue with satellites disappearing.
            msglen = makeUBXPacket(0x06, 0x08, sizeof(_message_1HZ), _message_1HZ);
            _serial_gps->write(UBXscratch, msglen);
            if (getACK(0x06, 0x08, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to set GPS update rate.\n");
            }

            // Disable GGL. GGL - Geographic position (latitude and longitude), which provides the current geographical
            // coordinates.
            uint8_t _message_GGL[] = {
                0xB5, 0x62,             // UBX sync characters
                0x06, 0x01,             // Message class and ID (UBX-CFG-MSG)
                0x08, 0x00,             // Length of payload (8 bytes)
                0xF0, 0x01,             // NMEA ID for GLL
                0x01,                   // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
                0x00,                   // Disable
                0x01, 0x01, 0x01, 0x01, // Reserved
                0x00, 0x00              // CK_A and CK_B (Checksum)
            };

            // Calculate the checksum and update the message.
            UBXChecksum(_message_GGL, sizeof(_message_GGL));

            // Send the message to the module
            _serial_gps->write(_message_GGL, sizeof(_message_GGL));

            if (getACK(0x06, 0x01, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to disable NMEA GGL.\n");
            }

            // Enable GSA. GSA - GPS DOP and active satellites, used for detailing the satellites used in the positioning and
            // the DOP (Dilution of Precision)
            uint8_t _message_GSA[] = {
                0xB5, 0x62,             // UBX sync characters
                0x06, 0x01,             // Message class and ID (UBX-CFG-MSG)
                0x08, 0x00,             // Length of payload (8 bytes)
                0xF0, 0x02,             // NMEA ID for GSA
                0x01,                   // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
                0x01,                   // Enable
                0x01, 0x01, 0x01, 0x01, // Reserved
                0x00, 0x00              // CK_A and CK_B (Checksum)
            };
            UBXChecksum(_message_GSA, sizeof(_message_GSA));
            _serial_gps->write(_message_GSA, sizeof(_message_GSA));
            if (getACK(0x06, 0x01, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to Enable NMEA GSA.\n");
            }

            // Disable GSV. GSV - Satellites in view, details the number and location of satellites in view.
            uint8_t _message_GSV[] = {
                0xB5, 0x62,             // UBX sync characters
                0x06, 0x01,             // Message class and ID (UBX-CFG-MSG)
                0x08, 0x00,             // Length of payload (8 bytes)
                0xF0, 0x03,             // NMEA ID for GSV
                0x01,                   // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
                0x00,                   // Disable
                0x01, 0x01, 0x01, 0x01, // Reserved
                0x00, 0x00              // CK_A and CK_B (Checksum)
            };
            UBXChecksum(_message_GSV, sizeof(_message_GSV));
            _serial_gps->write(_message_GSV, sizeof(_message_GSV));
            if (getACK(0x06, 0x01, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to disable NMEA GSV.\n");
            }

            // Disable VTG. VTG - Track made good and ground speed, which provides course and speed information relative to
            // the ground.
            uint8_t _message_VTG[] = {
                0xB5, 0x62,             // UBX sync characters
                0x06, 0x01,             // Message class and ID (UBX-CFG-MSG)
                0x08, 0x00,             // Length of payload (8 bytes)
                0xF0, 0x05,             // NMEA ID for VTG
                0x01,                   // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
                0x00,                   // Disable
                0x01, 0x01, 0x01, 0x01, // Reserved
                0x00, 0x00              // CK_A and CK_B (Checksum)
            };
            UBXChecksum(_message_VTG, sizeof(_message_VTG));
            _serial_gps->write(_message_VTG, sizeof(_message_VTG));
            if (getACK(0x06, 0x01, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to disable NMEA VTG.\n");
            }

            // Enable RMC. RMC - Recommended Minimum data, the essential gps pvt (position, velocity, time) data.
            uint8_t _message_RMC[] = {
                0xB5, 0x62,             // UBX sync characters
                0x06, 0x01,             // Message class and ID (UBX-CFG-MSG)
                0x08, 0x00,             // Length of payload (8 bytes)
                0xF0, 0x04,             // NMEA ID for RMC
                0x01,                   // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
                0x01,                   // Enable
                0x01, 0x01, 0x01, 0x01, // Reserved
                0x00, 0x00              // CK_A and CK_B (Checksum)
            };
            UBXChecksum(_message_RMC, sizeof(_message_RMC));
            _serial_gps->write(_message_RMC, sizeof(_message_RMC));
            if (getACK(0x06, 0x01, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to enable NMEA RMC.\n");
            }

            // Enable GGA. GGA - Global Positioning System Fix Data, which provides 3D location and accuracy data.
            uint8_t _message_GGA[] = {
                0xB5, 0x62,             // UBX sync characters
                0x06, 0x01,             // Message class and ID (UBX-CFG-MSG)
                0x08, 0x00,             // Length of payload (8 bytes)
                0xF0, 0x00,             // NMEA ID for GGA
                0x01,                   // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
                0x01,                   // Enable
                0x01, 0x01, 0x01, 0x01, // Reserved
                0x00, 0x00              // CK_A and CK_B (Checksum)
            };
            UBXChecksum(_message_GGA, sizeof(_message_GGA));
            _serial_gps->write(_message_GGA, sizeof(_message_GGA));
            if (getACK(0x06, 0x01, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to enable NMEA GGA.\n");
            }

            // The Power Management configuration allows the GPS module to operate in different power modes for optimized
            // power consumption. The modes supported are: 0x00 = Full power: The module operates at full power with no power
            // saving. 0x01 = Balanced: The module dynamically adjusts the tracking behavior to balance power consumption.
            // 0x02 = Interval: The module operates in a periodic mode, cycling between tracking and power saving states.
            // 0x03 = Aggressive with 1 Hz: The module operates in a power saving mode with a 1 Hz update rate.
            // 0x04 = Aggressive with 2 Hz: The module operates in a power saving mode with a 2 Hz update rate.
            // 0x05 = Aggressive with 4 Hz: The module operates in a power saving mode with a 4 Hz update rate.
            // The 'period' field specifies the position update and search period. It is only valid when the powerSetupValue
            // is set to Interval; otherwise, it must be set to '0'. The 'onTime' field specifies the duration of the ON phase
            // and must be smaller than the period. It is only valid when the powerSetupValue is set to Interval; otherwise,
            // it must be set to '0'.
            if (uBloxProtocolVersion >= 18) {
                byte UBX_CFG_PMS[16] = {
                    0xB5, 0x62, // UBX sync characters
                    0x06, 0x86, // Message class and ID (UBX-CFG-PMS)
                    0x08, 0x00, // Length of payload (6 bytes)
                    0x00,       // Version (0)
                    0x03,       // Power setup value
                    0x00, 0x00, // period: not applicable, set to 0
                    0x00, 0x00, // onTime: not applicable, set to 0
                    0x97, 0x6F, // reserved, generated by u-center
                    0x00, 0x00  // Placeholder for checksum, will be calculated next
                };

                // Calculate the checksum and update the message
                UBXChecksum(UBX_CFG_PMS, sizeof(UBX_CFG_PMS));

                // Send the message to the module
                _serial_gps->write(UBX_CFG_PMS, sizeof(UBX_CFG_PMS));
                if (getACK(0x06, 0x86, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable powersaving for GPS.\n");
                }
            } else {
                if (strncmp(info.hwVersion, "00040007", 8) == 0) { // This PSM mode has only been tested on this hardware
                    msglen = makeUBXPacket(0x06, 0x11, 0x2, _message_CFG_RXM_PSM);
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x11, 300) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to enable powersaving mode for GPS.\n");
                    }
                    msglen = makeUBXPacket(0x06, 0x3B, 44, _message_CFG_PM2);
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x3B, 300) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to enable powersaving details for GPS.\n");
                    }
                } else {
                    msglen = makeUBXPacket(0x06, 0x11, 0x2, _message_CFG_RXM_ECO);
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x11, 300) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to enable powersaving ECO mode for GPS.\n");
                    }
                }
            }
            // We need save configuration to flash to make our config changes persistent
            uint8_t _message_SAVE[21] = {
                0xB5, 0x62,             // UBX protocol header
                0x06, 0x09,             // UBX class ID (Configuration Input Messages), message ID (UBX-CFG-CFG)
                0x0D, 0x00,             // Length of payload (13 bytes)
                0x00, 0x00, 0x00, 0x00, // clearMask: no sections cleared
                0xFF, 0xFF, 0x00, 0x00, // saveMask: save all sections
                0x00, 0x00, 0x00, 0x00, // loadMask: no sections loaded
                0x0F,                   // deviceMask: BBR, Flash, EEPROM, and SPI Flash
                0x00, 0x00              // Checksum (calculated below)
            };

            // Calculate the checksum and update the message.
            UBXChecksum(_message_SAVE, sizeof(_message_SAVE));

            // Send the message to the module
            _serial_gps->write(_message_SAVE, sizeof(_message_SAVE));

            if (getACK(0x06, 0x09, 300) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to save GNSS module configuration.\n");
            } else {
                LOG_INFO("GNSS module configuration saved!\n");
            }
        }
        didSerialInit = true;
    }

    notifySleepObserver.observe(&notifySleep);
    notifyDeepSleepObserver.observe(&notifyDeepSleep);
    notifyGPSSleepObserver.observe(&notifyGPSSleep);

    if (config.position.gps_enabled == false && config.position.fixed_position == false) {
        setAwake(false);
    }
    return true;
}

GPS::~GPS()
{
    // we really should unregister our sleep observer
    notifySleepObserver.unobserve(&notifySleep);
    notifyDeepSleepObserver.unobserve(&notifyDeepSleep);
    notifyGPSSleepObserver.observe(&notifyGPSSleep);
}

bool GPS::hasLock()
{
    return hasValidLocation;
}

bool GPS::hasFlow()
{
    return hasGPS;
}

// Allow defining the polarity of the WAKE output.  default is active high
#ifndef GPS_WAKE_ACTIVE
#define GPS_WAKE_ACTIVE 1
#endif

void GPS::wake()
{
#ifdef PIN_GPS_WAKE
    digitalWrite(PIN_GPS_WAKE, GPS_WAKE_ACTIVE);
    pinMode(PIN_GPS_WAKE, OUTPUT);
#endif
}

void GPS::sleep()
{
#ifdef PIN_GPS_WAKE
    digitalWrite(PIN_GPS_WAKE, GPS_WAKE_ACTIVE ? 0 : 1);
    pinMode(PIN_GPS_WAKE, OUTPUT);
#endif
}

/// Record that we have a GPS
void GPS::setConnected()
{
    if (!hasGPS) {
        hasGPS = true;
        shouldPublish = true;
    }
}

void GPS::setNumSatellites(uint8_t n)
{
    if (n != numSatellites) {
        numSatellites = n;
        shouldPublish = true;
    }
}

/**
 * Switch the GPS into a mode where we are actively looking for a lock, or alternatively switch GPS into a low power mode
 *
 * calls sleep/wake
 */
void GPS::setAwake(bool on)
{
    if (!wakeAllowed && on) {
        LOG_WARN("Inhibiting because !wakeAllowed\n");
        on = false;
    }

    if (isAwake != on) {
        LOG_DEBUG("WANT GPS=%d\n", on);
        if (on) {
            clearBuffer(); // drop any old data waiting in the buffer
            lastWakeStartMsec = millis();
            wake();
        } else {
            lastSleepStartMsec = millis();
            sleep();
        }

        isAwake = on;
    }
}

/** Get how long we should stay looking for each acquisition in msecs
 */
uint32_t GPS::getWakeTime() const
{
    uint32_t t = config.position.gps_attempt_time;

    if (t == UINT32_MAX)
        return t; // already maxint
    return t * 1000;
}

/** Get how long we should sleep between aqusition attempts in msecs
 */
uint32_t GPS::getSleepTime() const
{
    uint32_t t = config.position.gps_update_interval;
    bool gps_enabled = config.position.gps_enabled;

    // We'll not need the GPS thread to wake up again after first acq. with fixed position.
    if (!gps_enabled || config.position.fixed_position)
        t = UINT32_MAX; // Sleep forever now

    if (t == UINT32_MAX)
        return t; // already maxint

    return t * 1000;
}

void GPS::publishUpdate()
{
    if (shouldPublish) {
        shouldPublish = false;

        // In debug logs, identify position by @timestamp:stage (stage 2 = publish)
        LOG_DEBUG("publishing pos@%x:2, hasVal=%d, GPSlock=%d\n", p.timestamp, hasValidLocation, hasLock());

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status = meshtastic::GPSStatus(hasValidLocation, isConnected(), isPowerSaving(), p);
        newStatus.notifyObservers(&status);
    }
}

int32_t GPS::runOnce()
{
    if (!GPSInitFinished) {
        if (!setup())
            return 2000; // Setup failed, re-run in two seconds
        gpsStatus->observe(&gps->newStatus);

        // We have now loaded our saved preferences from flash

        // ONCE we will factory reset the GPS for bug #327
        if (gps && !devicestate.did_gps_reset) {
            LOG_WARN("GPS FactoryReset requested\n");
            if (gps->factoryReset()) { // If we don't succeed try again next time
                devicestate.did_gps_reset = true;
                nodeDB.saveToDisk(SEGMENT_DEVICESTATE);
            }
        }
        GPSInitFinished = true;
        if (config.position.gps_enabled == false) {
            doGPSpowersave(false);
            return 0;
        }
    }

    // Repeaters have no need for GPS
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        disable();

    if (whileIdle()) {
        // if we have received valid NMEA claim we are connected
        setConnected();
    } else {
        if ((config.position.gps_enabled == 1) && (gnssModel == GNSS_MODEL_UBLOX)) {
            // reset the GPS on next bootup
            if (devicestate.did_gps_reset && (millis() > 60000) && !hasFlow()) {
                LOG_DEBUG("GPS is not communicating, trying factory reset on next bootup.\n");
                devicestate.did_gps_reset = false;
                nodeDB.saveDeviceStateToDisk();
                disable(); // Stop the GPS thread as it can do nothing useful until next reboot.
            }
        }
    }

    // If we are overdue for an update, turn on the GPS and at least publish the current status
    uint32_t now = millis();

    auto sleepTime = getSleepTime();
    if (!isAwake && sleepTime != UINT32_MAX && (now - lastSleepStartMsec) > sleepTime) {
        // We now want to be awake - so wake up the GPS
        setAwake(true);
    }

    // While we are awake
    if (isAwake) {
        // LOG_DEBUG("looking for location\n");
        if ((now - lastWhileActiveMsec) > 5000) {
            lastWhileActiveMsec = now;
            whileActive();
        }

        // If we've already set time from the GPS, no need to ask the GPS
        bool gotTime = (getRTCQuality() >= RTCQualityGPS);
        if (!gotTime && lookForTime()) { // Note: we count on this && short-circuiting and not resetting the RTC time
            gotTime = true;
            shouldPublish = true;
        }

        bool gotLoc = lookForLocation();
        if (gotLoc && !hasValidLocation) { // declare that we have location ASAP
            LOG_DEBUG("hasValidLocation RISING EDGE\n");
            hasValidLocation = true;
            shouldPublish = true;
        }

        // We've been awake too long - force sleep
        now = millis();
        auto wakeTime = getWakeTime();
        bool tooLong = wakeTime != UINT32_MAX && (now - lastWakeStartMsec) > wakeTime;

        // Once we get a location we no longer desperately want an update
        // LOG_DEBUG("gotLoc %d, tooLong %d, gotTime %d\n", gotLoc, tooLong, gotTime);
        if ((gotLoc && gotTime) || tooLong) {

            if (tooLong) {
                // we didn't get a location during this ack window, therefore declare loss of lock
                if (hasValidLocation) {
                    LOG_DEBUG("hasValidLocation FALLING EDGE (last read: %d)\n", gotLoc);
                }
                p = meshtastic_Position_init_default;
                hasValidLocation = false;
            }

            setAwake(false);
            shouldPublish = true; // publish our update for this just finished acquisition window
        }
    }

    // If state has changed do a publish
    publishUpdate();
    // 9600bps is approx 1 byte per msec, so considering our buffer size we never need to wake more often than 200ms
    // if not awake we can run super infrquently (once every 5 secs?) to see if we need to wake.
    return isAwake ? GPS_THREAD_INTERVAL : 5000;
}

void GPS::forceWake(bool on)
{
    if (on) {
        LOG_DEBUG("Allowing GPS lock\n");
        // lastSleepStartMsec = 0; // Force an update ASAP
        wakeAllowed = true;
    } else {
        wakeAllowed = false;

        // Note: if the gps was already awake, we DO NOT shut it down, because we want to allow it to complete its lock
        // attempt even if we are in light sleep.  Once the attempt succeeds (or times out) we'll then shut it down.
        // setAwake(false);
    }
}

// clear the GPS rx buffer as quickly as possible
void GPS::clearBuffer()
{
    int x = _serial_gps->available();
    while (x--)
        _serial_gps->read();
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareSleep(void *unused)
{
    LOG_INFO("GPS prepare sleep!\n");
    forceWake(false);

    return 0;
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareDeepSleep(void *unused)
{
    LOG_INFO("GPS deep sleep!\n");

    // For deep sleep we also want abandon any lock attempts (because we want minimum power)
    getSleepTime();
    setAwake(false);

    return 0;
}

GnssModel_t GPS::probe(int serialSpeed)
{
#if defined(ARCH_NRF52) || defined(ARCH_PORTDUINO) || defined(ARCH_RP2040)
    _serial_gps->end();
    _serial_gps->begin(serialSpeed);
#else
    if (_serial_gps->baudRate() != serialSpeed) {
        LOG_DEBUG("Setting Baud to %i\n", serialSpeed);
        _serial_gps->updateBaudRate(serialSpeed);
    }
#endif
#ifdef GPS_DEBUG
    for (int i = 0; i < 20; i++) {
        getACK("$GP", 200);
    }
#endif
    memset(&info, 0, sizeof(struct uBloxGnssModelInfo));
    uint8_t buffer[768] = {0};
    delay(100);

    // Close all NMEA sentences , Only valid for MTK platform
    _serial_gps->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(20);

    // Get version information
    clearBuffer();
    _serial_gps->write("$PCAS06,0*1B\r\n");
    if (getACK("$GPTXT,01,01,02,SW=", 500) == GNSS_RESPONSE_OK) {
        LOG_INFO("L76K GNSS init succeeded, using L76K GNSS Module\n");
        return GNSS_MODEL_MTK;
    }

    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x00, 0x00};
    UBXChecksum(cfg_rate, sizeof(cfg_rate));
    clearBuffer();
    _serial_gps->write(cfg_rate, sizeof(cfg_rate));
    // Check that the returned response class and message ID are correct
    GPS_RESPONSE response = getACK(0x06, 0x08, 750);
    if (response == GNSS_RESPONSE_NONE) {
        LOG_WARN("Failed to find UBlox & MTK GNSS Module using baudrate %d\n", serialSpeed);
        return GNSS_MODEL_UNKNOWN;
    } else if (response == GNSS_RESPONSE_FRAME_ERRORS) {
        LOG_INFO("UBlox Frame Errors using baudrate %d\n", serialSpeed);
    } else if (response == GNSS_RESPONSE_OK) {
        LOG_INFO("Found a UBlox Module using baudrate %d\n", serialSpeed);
    }

    // tips: NMEA Only should not be set here, otherwise initializing Ublox gnss module again after
    // setting will not output command messages in UART1, resulting in unrecognized module information
    if (serialSpeed != 9600) {
        // Set the UART port to 9600
        uint8_t _message_prt[] = {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00,
                                  0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        UBXChecksum(_message_prt, sizeof(_message_prt));
        _serial_gps->write(_message_prt, sizeof(_message_prt));
        delay(500);
        serialSpeed = 9600;
#if defined(ARCH_NRF52) || defined(ARCH_PORTDUINO) || defined(ARCH_RP2040)
        _serial_gps->end();
        _serial_gps->begin(serialSpeed);
#else
        _serial_gps->updateBaudRate(serialSpeed);
#endif
        delay(200);
    }

    memset(buffer, 0, sizeof(buffer));
    uint8_t _message_MONVER[8] = {
        0xB5, 0x62, // Sync message for UBX protocol
        0x0A, 0x04, // Message class and ID (UBX-MON-VER)
        0x00, 0x00, // Length of payload (we're asking for an answer, so no payload)
        0x00, 0x00  // Checksum
    };
    //  Get Ublox gnss module hardware and software info
    UBXChecksum(_message_MONVER, sizeof(_message_MONVER));
    clearBuffer();
    _serial_gps->write(_message_MONVER, sizeof(_message_MONVER));

    uint16_t len = getACK(buffer, sizeof(buffer), 0x0A, 0x04, 1200);
    if (len) {
        // LOG_DEBUG("monver reply size = %d\n", len);
        uint16_t position = 0;
        for (int i = 0; i < 30; i++) {
            info.swVersion[i] = buffer[position];
            position++;
        }
        for (int i = 0; i < 10; i++) {
            info.hwVersion[i] = buffer[position];
            position++;
        }

        while (len >= position + 30) {
            for (int i = 0; i < 30; i++) {
                info.extension[info.extensionNo][i] = buffer[position];
                position++;
            }
            info.extensionNo++;
            if (info.extensionNo > 9)
                break;
        }

        LOG_DEBUG("Module Info : \n");
        LOG_DEBUG("Soft version: %s\n", info.swVersion);
        LOG_DEBUG("Hard version: %s\n", info.hwVersion);
        LOG_DEBUG("Extensions:%d\n", info.extensionNo);
        for (int i = 0; i < info.extensionNo; i++) {
            LOG_DEBUG("  %s\n", info.extension[i]);
        }

        memset(buffer, 0, sizeof(buffer));

        // tips: extensionNo field is 0 on some 6M GNSS modules
        for (int i = 0; i < info.extensionNo; ++i) {
            if (!strncmp(info.extension[i], "MOD=", 4)) {
                strncpy((char *)buffer, &(info.extension[i][4]), sizeof(buffer));
                // LOG_DEBUG("GetModel:%s\n", (char *)buffer);
                if (strlen((char *)buffer)) {
                    LOG_INFO("UBlox GNSS init succeeded, using UBlox %s GNSS Module\n", (char *)buffer);
                } else {
                    LOG_INFO("UBlox GNSS init succeeded, using UBlox GNSS Module\n");
                }
            } else if (!strncmp(info.extension[i], "PROTVER", 7)) {
                char *ptr = nullptr;
                memset(buffer, 0, sizeof(buffer));
                strncpy((char *)buffer, &(info.extension[i][8]), sizeof(buffer));
                LOG_DEBUG("Protocol Version:%s\n", (char *)buffer);
                if (strlen((char *)buffer)) {
                    uBloxProtocolVersion = strtoul((char *)buffer, &ptr, 10);
                    LOG_DEBUG("ProtVer=%d\n", uBloxProtocolVersion);
                } else {
                    uBloxProtocolVersion = 0;
                }
            }
        }
    }

    return GNSS_MODEL_UBLOX;
}

#if HAS_GPS
#include "NMEAGPS.h"
#endif

GPS *createGps()
{

#if !HAS_GPS
    return nullptr;
#else
    return new NMEAGPS();
#endif
}