#include "GPS.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "sleep.h"

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

void GPS::UBXChecksum(byte *message, size_t length)
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

bool GPS::getACK(uint8_t class_id, uint8_t msg_id)
{
    uint8_t b;
    uint8_t ack = 0;
    const uint8_t ackP[2] = {class_id, msg_id};
    uint8_t buf[10] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    unsigned long startTime = millis();

    for (int j = 2; j < 6; j++) {
        buf[8] += buf[j];
        buf[9] += buf[8];
    }

    for (int j = 0; j < 2; j++) {
        buf[6 + j] = ackP[j];
        buf[8] += buf[6 + j];
        buf[9] += buf[8];
    }

    while (1) {
        if (ack > 9) {
            // LOG_INFO("Got ACK for class %02X message %02X\n", class_id, msg_id);
            return true; // ACK received
        }
        if (millis() - startTime > 3000) {
            LOG_WARN("No response for class %02X message %02X\n", class_id, msg_id);
            return false; // No response received within 3 seconds
        }
        if (_serial_gps->available()) {
            b = _serial_gps->read();
            if (b == buf[ack]) {
                ack++;
            } else {
                ack = 0;              // Reset the acknowledgement counter
                if (buf[3] == 0x00) { // UBX-ACK-NAK message
                    LOG_WARN("Got NAK for class %02X message %02X\n", class_id, msg_id);
                    return false; // NAK received
                }
            }
        }
    }
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
int GPS::getAck(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID)
{
    uint16_t ubxFrameCounter = 0;
    uint32_t startTime = millis();
    uint16_t needRead;

    while (millis() - startTime < 1200) {
        while (_serial_gps->available()) {
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
                break;
            case 6:
                // Check for buffer overflow
                if (needRead >= size) {
                    ubxFrameCounter = 0;
                    break;
                }
                if (_serial_gps->readBytes(buffer, needRead) != needRead) {
                    ubxFrameCounter = 0;
                } else {
                    // return payload length
                    return needRead;
                }
                break;

            default:
                break;
            }
        }
    }
    return 0;
}

bool GPS::setupGPS()
{
    if (_serial_gps && !didSerialInit) {
        didSerialInit = true;

#ifdef ARCH_ESP32
        // In esp32 framework, setRxBufferSize needs to be initialized before Serial
        _serial_gps->setRxBufferSize(2048); // the default is 256
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

//#define BAUD_RATE 115200
// ESP32 has a special set of parameters vs other arduino ports
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
        gnssModel = probe();

        if (gnssModel == GNSS_MODEL_MTK) {
            /*
             * t-beam-s3-core uses the same L76K GNSS module as t-echo.
             * Unlike t-echo, L76K uses 9600 baud rate for communication by default.
             * */
            // _serial_gps->begin(9600);    //The baud rate of 9600 has been initialized at the beginning of setupGPS, this line
            // is the redundant part delay(250);

            // Initialize the L76K Chip, use GPS + GLONASS
            _serial_gps->write("$PCAS04,5*1C\r\n");
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
            byte _message_GNSS[36] = {
                0xb5, 0x62, // Sync message for UBX protocol
                0x06, 0x3e, // Message class and ID (UBX-CFG-GNSS)
                0x1c, 0x00, // Length of payload (28 bytes)
                0x00,       // msgVer (0 for this version)
                0x00,       // numTrkChHw (max number of hardware channels, read only, so it's always 0)
                0xff,       // numTrkChUse (max number of channels to use, 0xff = max available)
                0x03,       // numConfigBlocks (number of GNSS systems), most modules support maximum 3 GNSS systems
                // GNSS config format: gnssId, resTrkCh, maxTrkCh, reserved1, flags
                0x00, 0x08, 0x10, 0x00, 0x01, 0x00, 0x01, 0x01, // GPS
                0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0x01, // SBAS
                0x06, 0x08, 0x0e, 0x00, 0x01, 0x00, 0x01, 0x01, // GLONASS
                0x00, 0x00                                      // Checksum (to be calculated below)
            };

            // Calculate the checksum and update the message.
            UBXChecksum(_message_GNSS, sizeof(_message_GNSS));

            // Send the message to the module
            _serial_gps->write(_message_GNSS, sizeof(_message_GNSS));

            if (!getACK(0x06, 0x3e)) {
                // It's not critical if the module doesn't acknowledge this configuration.
                // The module should operate adequately with its factory or previously saved settings.
                // It appears that there is a firmware bug in some GPS modules: When an attempt is made
                // to overwrite a saved state with identical values, no ACK/NAK is received, contrary to
                // what is specified in the Ublox documentation.
                // There is also a possibility that the module may be GPS-only.
                LOG_INFO("Unable to reconfigure GNSS - defaults maintained. Is this module GPS-only?\n");
                return true;
            } else {
                LOG_INFO("GNSS configured for GPS+SBAS+GLONASS. Pause for 0.75s before sending next command.\n");
                // Documentation say, we need wait atleast 0.5s after reconfiguration of GNSS module, before sending next commands
                delay(750);
                return true;
            }

            // Enable interference resistance, because we are using LoRa, WiFi and Bluetooth on same board,
            // and we need to reduce interference from them
            byte _message_JAM[16] = {
                0xB5, 0x62, // UBX protocol sync characters
                0x06, 0x39, // Message class and ID (UBX-CFG-ITFM)
                0x08, 0x00, // Length of payload (8 bytes)
                // bbThreshold (Broadband jamming detection threshold) is set to 0x3F (63 in decimal)
                // cwThreshold (CW jamming detection threshold) is set to 0x10 (16 in decimal)
                // algorithmBits (Reserved algorithm settings) is set to 0x16B156 as recommended
                // enable (Enable interference detection) is set to 1 (enabled)
                0x3F, 0x10, 0xB1, 0x56, // config: Interference config word
                // generalBits (General settings) is set to 0x31E as recommended
                // antSetting (Antenna setting, 0=unknown, 1=passive, 2=active) is set to 0 (unknown)
                // ToDo: Set to 1 (passive) or 2 (active) if known, for example from UBX-MON-HW, or from board info
                // enable2 (Set to 1 to scan auxiliary bands, u-blox 8 / u-blox M8 only, otherwise ignored) is set to 1
                // (enabled)
                0x1E, 0x03, 0x00, 0x01, // config2: Extra settings for jamming/interference monitor
                0x00, 0x00              // Checksum (calculated below)
            };

            // Calculate the checksum and update the message.
            UBXChecksum(_message_JAM, sizeof(_message_JAM));

            // Send the message to the module
            _serial_gps->write(_message_JAM, sizeof(_message_JAM));

            if (!getACK(0x06, 0x39)) {
                LOG_WARN("Unable to enable interference resistance.\n");
                return true;
            }

            // Configure navigation engine expert settings:
            byte _message_NAVX5[48] = {
                0xb5, 0x62, // UBX protocol sync characters
                0x06, 0x23, // Message class and ID (UBX-CFG-NAVX5)
                0x28, 0x00, // Length of payload (40 bytes)
                0x00, 0x00, // msgVer (0 for this version)
                // minMax flag = 1: apply min/max SVs settings
                // minCno flag = 1: apply minimum C/N0 setting
                // initial3dfix flag = 0: apply initial 3D fix settings
                // aop flag = 1: apply aopCfg (useAOP flag) settings (AssistNow Autonomous)
                0x1B, 0x00, // mask1 (First parameters bitmask)
                // adr flag = 0: apply ADR sensor fusion on/off setting (useAdr flag)
                // If firmware is not ADR/UDR, enabling this flag will fail configuration
                // ToDo: check this with UBX-MON-VER
                0x00, 0x00, 0x00, 0x00, // mask2 (Second parameters bitmask)
                0x00, 0x00,             // Reserved
                0x03,                   // minSVs (Minimum number of satellites for navigation) = 3
                0x10,                   // maxSVs (Maximum number of satellites for navigation) = 16
                0x06,                   // minCNO (Minimum satellite signal level for navigation) = 6 dBHz
                0x00,                   // Reserved
                0x00,                   // iniFix3D (Initial fix must be 3D) = 0 (disabled)
                0x00, 0x00,             // Reserved
                0x00,                   // ackAiding (Issue acknowledgements for assistance message input) = 0 (disabled)
                0x00, 0x00,             // Reserved
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reserved
                0x00,                               // Reserved
                0x01,                               // aopCfg (AssistNow Autonomous configuration) = 1 (enabled)
                0x00, 0x00,                         // Reserved
                0x00, 0x00,                         // Reserved
                0x00, 0x00, 0x00, 0x00,             // Reserved
                0x00, 0x00, 0x00,                   // Reserved
                0x01,                               // useAdr (Enable/disable ADR sensor fusion) = 1 (enabled)
                0x00, 0x00                          // Checksum (calculated below)
            };

            // Calculate the checksum and update the message.
            UBXChecksum(_message_NAVX5, sizeof(_message_NAVX5));

            // Send the message to the module
            _serial_gps->write(_message_NAVX5, sizeof(_message_NAVX5));

            if (!getACK(0x06, 0x23)) {
                LOG_WARN("Unable to configure extra settings.\n");
                return true;
            }

            /*
                tips: NMEA Only should not be set here, otherwise initializing Ublox gnss module again after
                setting will not output command messages in UART1, resulting in unrecognized module information

                // Set the UART port to output NMEA only
                byte _message_nmea[] = {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xC0, 0x08, 0x00, 0x00,
                                        0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x91, 0xAF};
                _serial_gps->write(_message_nmea, sizeof(_message_nmea));
                if (!getACK(0x06, 0x00)) {
                    LOG_WARN("Unable to enable NMEA Mode.\n");
                    return true;
                }
            */

            // ublox-M10S can be compatible with UBLOX traditional protocol, so the following sentence settings are also valid

            // Set GPS update rate to 1Hz
            // Lowering the update rate helps to save power.
            // Additionally, for some new modules like the M9/M10, an update rate lower than 5Hz
            // is recommended to avoid a known issue with satellites disappearing.
            byte _message_1Hz[] = {
                0xB5, 0x62, // UBX protocol sync characters
                0x06, 0x08, // Message class and ID (UBX-CFG-RATE)
                0x06, 0x00, // Length of payload (6 bytes)
                0xE8, 0x03, // Measurement Rate (1000ms for 1Hz)
                0x01, 0x00, // Navigation rate, always 1 in GPS mode
                0x01, 0x00, // Time reference
                0x00, 0x00  // Placeholder for checksum, will be calculated next
            };

            // Calculate the checksum and update the message.
            UBXChecksum(_message_1Hz, sizeof(_message_1Hz));

            // Send the message to the module
            _serial_gps->write(_message_1Hz, sizeof(_message_1Hz));

            if (!getACK(0x06, 0x08)) {
                LOG_WARN("Unable to set GPS update rate.\n");
                return true;
            }

            // Disable GGL. GGL - Geographic position (latitude and longitude), which provides the current geographical
            // coordinates.
            byte _message_GGL[] = {
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

            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to disable NMEA GGL.\n");
                return true;
            }

            // Enable GSA. GSA - GPS DOP and active satellites, used for detailing the satellites used in the positioning and
            // the DOP (Dilution of Precision)
            byte _message_GSA[] = {
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
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to Enable NMEA GSA.\n");
                return true;
            }

            // Disable GSV. GSV - Satellites in view, details the number and location of satellites in view.
            byte _message_GSV[] = {
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
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to disable NMEA GSV.\n");
                return true;
            }

            // Disable VTG. VTG - Track made good and ground speed, which provides course and speed information relative to
            // the ground.
            byte _message_VTG[] = {
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
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to disable NMEA VTG.\n");
                return true;
            }

            // Enable RMC. RMC - Recommended Minimum data, the essential gps pvt (position, velocity, time) data.
            byte _message_RMC[] = {
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
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to enable NMEA RMC.\n");
                return true;
            }

            // Enable GGA. GGA - Global Positioning System Fix Data, which provides 3D location and accuracy data.
            byte _message_GGA[] = {
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
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to enable NMEA GGA.\n");
                return true;
            }

            // The Power Management configuration allows the GPS module to operate in different power modes for optimized power
            // consumption.
            // The modes supported are:
            // 0x00 = Full power: The module operates at full power with no power saving.
            // 0x01 = Balanced: The module dynamically adjusts the tracking behavior to balance power consumption.
            // 0x02 = Interval: The module operates in a periodic mode, cycling between tracking and power saving states.
            // 0x03 = Aggressive with 1 Hz: The module operates in a power saving mode with a 1 Hz update rate.
            // 0x04 = Aggressive with 2 Hz: The module operates in a power saving mode with a 2 Hz update rate.
            // 0x05 = Aggressive with 4 Hz: The module operates in a power saving mode with a 4 Hz update rate.
            // The 'period' field specifies the position update and search period. It is only valid when the powerSetupValue is
            // set to Interval; otherwise, it must be set to '0'. The 'onTime' field specifies the duration of the ON phase and
            // must be smaller than the period. It is only valid when the powerSetupValue is set to Interval; otherwise, it must
            // be set to '0'.
            byte UBX_CFG_PMS[14] = {
                0xB5, 0x62, // UBX sync characters
                0x06, 0x86, // Message class and ID (UBX-CFG-PMS)
                0x06, 0x00, // Length of payload (6 bytes)
                0x00,       // Version (0)
                0x03,       // Power setup value
                0x00, 0x00, // period: not applicable, set to 0
                0x00, 0x00, // onTime: not applicable, set to 0
                0x00, 0x00  // Placeholder for checksum, will be calculated next
            };

            // Calculate the checksum and update the message
            UBXChecksum(UBX_CFG_PMS, sizeof(UBX_CFG_PMS));

            // Send the message to the module
            _serial_gps->write(UBX_CFG_PMS, sizeof(UBX_CFG_PMS));
            if (!getACK(0x06, 0x86)) {
                LOG_WARN("Unable to enable powersaving for GPS.\n");
                return true;
            }

            // We need save configuration to flash to make our config changes persistent
            byte _message_SAVE[21] = {
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

            if (!getACK(0x06, 0x09)) {
                LOG_WARN("Unable to save GNSS module configuration.\n");
                return true;
            } else {
                LOG_INFO("GNSS module configuration saved!\n");
                return true;
            }
        }
    }

    return true;
}

bool GPS::setup()
{
    // Master power for the GPS

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
    bool ok = setupGPS();

    if (ok) {
        notifySleepObserver.observe(&notifySleep);
        notifyDeepSleepObserver.observe(&notifyDeepSleep);
        notifyGPSSleepObserver.observe(&notifyGPSSleep);
    }

    if (config.position.gps_enabled == false && config.position.fixed_position == false) {
        setAwake(false);
        doGPSpowersave(false);
    }
    return ok;
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

    if (!(fixeddelayCtr >= 20) && config.position.fixed_position && hasValidLocation) {
        fixeddelayCtr++;
        // LOG_DEBUG("Our delay counter is %d\n", fixeddelayCtr);
        if (fixeddelayCtr >= 20) {
            doGPSpowersave(false);
            forceWake(false);
        }
    }
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

GnssModel_t GPS::probe()
{
    memset(&info, 0, sizeof(struct uBloxGnssModelInfo));
// return immediately if the model is set by the variant.h file
//#ifdef GPS_UBLOX               (unless it's a ublox, because we might want to know the module info!
//    return GNSS_MODEL_UBLOX;    think about removing this macro and return)
#if defined(GPS_L76K)
    return GNSS_MODEL_MTK;
#elif defined(GPS_UC6580)
    _serial_gps->updateBaudRate(115200);
    return GNSS_MODEL_UC6850;
#else
    uint8_t buffer[384] = {0};

    // Close all NMEA sentences , Only valid for MTK platform
    _serial_gps->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(20);

    // Get version information
    _serial_gps->write("$PCAS06,0*1B\r\n");
    uint32_t startTimeout = millis() + 500;
    while (millis() < startTimeout) {
        if (_serial_gps->available()) {
            String ver = _serial_gps->readStringUntil('\r');
            // Get module info , If the correct header is returned,
            // it can be determined that it is the MTK chip
            int index = ver.indexOf("$");
            if (index != -1) {
                ver = ver.substring(index);
                if (ver.startsWith("$GPTXT,01,01,02")) {
                    LOG_INFO("L76K GNSS init succeeded, using L76K GNSS Module\n");
                    return GNSS_MODEL_MTK;
                }
            }
        }
    }

    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};
    _serial_gps->write(cfg_rate, sizeof(cfg_rate));
    // Check that the returned response class and message ID are correct
    if (!getAck(buffer, 384, 0x06, 0x08)) {
        LOG_WARN("Failed to find UBlox & MTK GNSS Module\n");
        return GNSS_MODEL_UNKNOWN;
    }
    memset(buffer, 0, sizeof(buffer));
    byte _message_MONVER[8] = {
        0xB5, 0x62, // Sync message for UBX protocol
        0x0A, 0x04, // Message class and ID (UBX-MON-VER)
        0x00, 0x00, // Length of payload (we're asking for an answer, so no payload)
        0x00, 0x00  // Checksum
    };
    //  Get Ublox gnss module hardware and software info
    UBXChecksum(_message_MONVER, sizeof(_message_MONVER));
    _serial_gps->write(_message_MONVER, sizeof(_message_MONVER));

    uint16_t len = getAck(buffer, 384, 0x0A, 0x04);
    if (len) {
        // LOG_DEBUG("monver reply size = %d\n", len);
        uint16_t position = 0;
        for (int i = 0; i < 30; i++) {
            info.swVersion[i] = buffer[position];
            position++;
        }
        for (int i = 0; i < 10; i++) {
            info.hwVersion[i] = buffer[position - 1];
            position++;
        }

        while (len >= position + 30) {
            for (int i = 0; i < 30; i++) {
                info.extension[info.extensionNo][i] = buffer[position - 1];
                position++;
            }
            info.extensionNo++;
            if (info.extensionNo > 9)
                break;
        }

        LOG_DEBUG("Module Info : \n");
        LOG_DEBUG("Soft version: %s\n", info.swVersion);
        LOG_DEBUG("first char is %c\n", (char)info.swVersion[0]);
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
            } else if (!strncmp(info.extension[i], "PROTVER=", 8)) {
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
#endif
}

#if HAS_GPS
#include "NMEAGPS.h"
#endif

GPS *createGps()
{

#if !HAS_GPS
    return nullptr;
#else
    if (config.position.gps_enabled) {
#ifdef GPS_ALTITUDE_HAE
        LOG_DEBUG("Using HAE altitude model\n");
#else
        LOG_DEBUG("Using MSL altitude model\n");
#endif
        if (GPS::_serial_gps) {
            // Some boards might have only the TX line from the GPS connected, in that case, we can't configure it at all.
            // Just assume NMEA at 9600 baud.
            GPS *new_gps = new NMEAGPS();
            new_gps->setup();
            return new_gps;
        }
    } else {
        GPS *new_gps = new NMEAGPS();
        new_gps->setup();
        return new_gps;
    }
    return nullptr;
#endif
}