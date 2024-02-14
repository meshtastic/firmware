#include "GPS.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "main.h" // pmu_found
#include "sleep.h"
#include "ubx.h"

#ifdef ARCH_PORTDUINO
#include "PortduinoGlue.h"
#include "meshUtils.h"
#include <ctime>
#endif

#ifndef GPS_RESET_MODE
#define GPS_RESET_MODE HIGH
#endif

#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
HardwareSerial *GPS::_serial_gps = &Serial1;
#else
HardwareSerial *GPS::_serial_gps = NULL;
#endif

GPS *gps = nullptr;

/// Multiple GPS instances might use the same serial port (in sequence), but we can
/// only init that port once.
static bool didSerialInit;

struct uBloxGnssModelInfo info;
uint8_t uBloxProtocolVersion;
#define GPS_SOL_EXPIRY_MS 5000 // in millis. give 1 second time to combine different sentences. NMEA Frequency isn't higher anyway
#define NMEA_MSG_GXGSA "GNGSA" // GSA message (GPGSA, GNGSA etc)

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
#ifdef GPS_DEBUG
            LOG_DEBUG("%02X", (char *)buffer);
#endif
            buffer[bytesRead] = b;
            bytesRead++;
            if ((bytesRead == 767) || (b == '\r')) {
                if (strnstr((char *)buffer, message, bytesRead) != nullptr) {
#ifdef GPS_DEBUG
                    LOG_DEBUG("\r");
#endif
                    return GNSS_RESPONSE_OK;
                } else {
                    bytesRead = 0;
                }
            }
        }
    }
#ifdef GPS_DEBUG
    LOG_DEBUG("\n");
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

    if (!didSerialInit) {
#if !defined(GPS_UC6580)

#if defined(RAK4630) && defined(PIN_3V3_EN)
        // If we are using the RAK4630 and we have no other peripherals on the I2C bus or module interest in 3V3_S,
        // then we can safely set en_gpio turn off power to 3V3 (IO2) to hard sleep the GPS
        if (rtc_found.port == ScanI2C::DeviceType::NONE && rgb_found.type == ScanI2C::DeviceType::NONE &&
            accelerometer_found.port == ScanI2C::DeviceType::NONE && !moduleConfig.detection_sensor.enabled &&
            !moduleConfig.telemetry.air_quality_enabled && !moduleConfig.telemetry.environment_measurement_enabled &&
            config.power.device_battery_ina_address == 0 && en_gpio == 0) {
            LOG_DEBUG("Since no problematic peripherals or interested modules were found, setting power save GPS_EN to pin %i\n",
                      PIN_3V3_EN);
            en_gpio = PIN_3V3_EN;
        }
#endif

        if (tx_gpio && gnssModel == GNSS_MODEL_UNKNOWN) {
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
            }
        } else {
            gnssModel = GNSS_MODEL_UNKNOWN;
        }
#else
        gnssModel = GNSS_MODEL_UC6580;
#endif

        if (gnssModel == GNSS_MODEL_MTK) {
            /*
             * t-beam-s3-core uses the same L76K GNSS module as t-echo.
             * Unlike t-echo, L76K uses 9600 baud rate for communication by default.
             * */

            // Initialize the L76K Chip, use GPS + GLONASS + BEIDOU
            _serial_gps->write("$PCAS04,7*1E\r\n");
            delay(250);
            // only ask for RMC and GGA
            _serial_gps->write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
            delay(250);
            // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
            _serial_gps->write("$PCAS11,3*1E\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_UC6580) {
            // The Unicore UC6580 can use a lot of sat systems, enable it to
            // use GPS L1 & L5 + BDS B1I & B2a + GLONASS L1 + GALILEO E1 & E5a + SBAS
            // This will reset the receiver, so wait a bit afterwards
            // The paranoid will wait for the OK*04 confirmation response after each command.
            _serial_gps->write("$CFGSYS,h25155\r\n");
            delay(750);
            // Must be done after the CFGSYS command
            // Turn off GSV messages, we don't really care about which and where the sats are, maybe someday.
            _serial_gps->write("$CFGMSG,0,3,0\r\n");
            delay(250);
            // Turn off NOTICE __TXT messages, these may provide Unicore some info but we don't care.
            _serial_gps->write("$CFGMSG,6,0,0\r\n");
            delay(250);
            _serial_gps->write("$CFGMSG,6,1,0\r\n");
            delay(250);

        } else if (gnssModel == GNSS_MODEL_UBLOX) {
            // Configure GNSS system to GPS+SBAS+GLONASS (Module may restart after this command)
            // We need set it because by default it is GPS only, and we want to use GLONASS too
            // Also we need SBAS for better accuracy and extra features
            // ToDo: Dynamic configure GNSS systems depending of LoRa region

            if (strncmp(info.hwVersion, "000A0000", 8) != 0) {
                if (strncmp(info.hwVersion, "00040007", 8) != 0) {
                    // The original ublox Neo-6 is GPS only and doesn't support the UBX-CFG-GNSS message
                    // Max7 seems to only support GPS *or* GLONASS
                    // Neo-7 is supposed to support GPS *and* GLONASS but NAKs the CFG-GNSS command to do it
                    // So treat all the u-blox 7 series as GPS only
                    // M8 can support 3 constallations at once so turn on GPS, GLONASS and Galileo (or BeiDou)

                    if (strncmp(info.hwVersion, "00070000", 8) == 0) {
                        LOG_DEBUG("Setting GPS+SBAS\n");
                        msglen = makeUBXPacket(0x06, 0x3e, sizeof(_message_GNSS_7), _message_GNSS_7);
                        _serial_gps->write(UBXscratch, msglen);
                    } else {
                        msglen = makeUBXPacket(0x06, 0x3e, sizeof(_message_GNSS_8), _message_GNSS_8);
                        _serial_gps->write(UBXscratch, msglen);
                    }

                    if (getACK(0x06, 0x3e, 800) == GNSS_RESPONSE_NAK) {
                        // It's not critical if the module doesn't acknowledge this configuration.
                        LOG_INFO("Unable to reconfigure GNSS - defaults maintained. Is this module GPS-only?\n");
                    } else {
                        if (strncmp(info.hwVersion, "00070000", 8) == 0) {
                            LOG_INFO("GNSS configured for GPS+SBAS. Pause for 0.75s before sending next command.\n");
                        } else {
                            LOG_INFO(
                                "GNSS configured for GPS+SBAS+GLONASS+Galileo. Pause for 0.75s before sending next command.\n");
                        }
                        // Documentation say, we need wait atleast 0.5s after reconfiguration of GNSS module, before sending next
                        // commands for the M8 it tends to be more... 1 sec should be enough ;>)
                        delay(1000);
                    }
                }
                // Disable Text Info messages
                msglen = makeUBXPacket(0x06, 0x02, sizeof(_message_DISABLE_TXT_INFO), _message_DISABLE_TXT_INFO);
                clearBuffer();
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x02, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable text info messages.\n");
                }
                // ToDo add M10 tests for below
                if (strncmp(info.hwVersion, "00080000", 8) == 0) {
                    msglen = makeUBXPacket(0x06, 0x39, sizeof(_message_JAM_8), _message_JAM_8);
                    clearBuffer();
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x39, 500) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to enable interference resistance.\n");
                    }

                    msglen = makeUBXPacket(0x06, 0x23, sizeof(_message_NAVX5_8), _message_NAVX5_8);
                    clearBuffer();
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x23, 500) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to configure NAVX5_8 settings.\n");
                    }
                } else {
                    msglen = makeUBXPacket(0x06, 0x39, sizeof(_message_JAM_6_7), _message_JAM_6_7);
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x39, 500) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to enable interference resistance.\n");
                    }

                    msglen = makeUBXPacket(0x06, 0x23, sizeof(_message_NAVX5), _message_NAVX5);
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x23, 500) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to configure NAVX5 settings.\n");
                    }
                }
                // Turn off unwanted NMEA messages, set update rate

                msglen = makeUBXPacket(0x06, 0x08, sizeof(_message_1HZ), _message_1HZ);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x08, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to set GPS update rate.\n");
                }

                msglen = makeUBXPacket(0x06, 0x01, sizeof(_message_GLL), _message_GLL);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x01, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable NMEA GLL.\n");
                }

                msglen = makeUBXPacket(0x06, 0x01, sizeof(_message_GSA), _message_GSA);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x01, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to Enable NMEA GSA.\n");
                }

                msglen = makeUBXPacket(0x06, 0x01, sizeof(_message_GSV), _message_GSV);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x01, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable NMEA GSV.\n");
                }

                msglen = makeUBXPacket(0x06, 0x01, sizeof(_message_VTG), _message_VTG);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x01, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable NMEA VTG.\n");
                }

                msglen = makeUBXPacket(0x06, 0x01, sizeof(_message_RMC), _message_RMC);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x01, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable NMEA RMC.\n");
                }

                msglen = makeUBXPacket(0x06, 0x01, sizeof(_message_GGA), _message_GGA);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x01, 500) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable NMEA GGA.\n");
                }

                if (uBloxProtocolVersion >= 18) {
                    msglen = makeUBXPacket(0x06, 0x86, sizeof(_message_PMS), _message_PMS);
                    clearBuffer();
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x86, 500) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to enable powersaving for GPS.\n");
                    }
                    // For M8 we want to enable NMEA vserion 4.10 so we can see the additional sats.
                    if (strncmp(info.hwVersion, "00080000", 8) == 0) {
                        msglen = makeUBXPacket(0x06, 0x17, sizeof(_message_NMEA), _message_NMEA);
                        clearBuffer();
                        _serial_gps->write(UBXscratch, msglen);
                        if (getACK(0x06, 0x17, 500) != GNSS_RESPONSE_OK) {
                            LOG_WARN("Unable to enable NMEA 4.10.\n");
                        }
                    }

                } else {
                    if (strncmp(info.hwVersion, "00040007", 8) == 0) { // This PSM mode is only for Neo-6
                        msglen = makeUBXPacket(0x06, 0x11, 0x2, _message_CFG_RXM_ECO);
                        _serial_gps->write(UBXscratch, msglen);
                        if (getACK(0x06, 0x11, 500) != GNSS_RESPONSE_OK) {
                            LOG_WARN("Unable to enable powersaving ECO mode for Neo-6.\n");
                        }
                        msglen = makeUBXPacket(0x06, 0x3B, sizeof(_message_CFG_PM2), _message_CFG_PM2);
                        _serial_gps->write(UBXscratch, msglen);
                        if (getACK(0x06, 0x3B, 500) != GNSS_RESPONSE_OK) {
                            LOG_WARN("Unable to enable powersaving details for GPS.\n");
                        }
                        msglen = makeUBXPacket(0x06, 0x01, sizeof(_message_AID), _message_AID);
                        _serial_gps->write(UBXscratch, msglen);
                        if (getACK(0x06, 0x01, 500) != GNSS_RESPONSE_OK) {
                            LOG_WARN("Unable to disable UBX-AID.\n");
                        }
                    } else {
                        msglen = makeUBXPacket(0x06, 0x11, 0x2, _message_CFG_RXM_PSM);
                        _serial_gps->write(UBXscratch, msglen);
                        if (getACK(0x06, 0x11, 500) != GNSS_RESPONSE_OK) {
                            LOG_WARN("Unable to enable powersaving mode for GPS.\n");
                        }
                    }
                }

                msglen = makeUBXPacket(0x06, 0x09, sizeof(_message_SAVE), _message_SAVE);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x09, 2000) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to save GNSS module configuration.\n");
                } else {
                    LOG_INFO("GNSS module configuration saved!\n");
                }
            } else {
                // LOG_INFO("u-blox M10 hardware found.\n");
                delay(1000);
                // First disable all NMEA messages in RAM layer
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_DISABLE_NMEA_RAM), _message_VALSET_DISABLE_NMEA_RAM);
                clearBuffer();
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable NMEA messages for M10 GPS RAM.\n");
                }
                delay(250);
                // Next disable unwanted NMEA messages in BBR layer
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_DISABLE_NMEA_BBR), _message_VALSET_DISABLE_NMEA_BBR);
                clearBuffer();
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable NMEA messages for M10 GPS BBR.\n");
                }
                delay(250);
                // Disable Info txt messages in RAM layer
                msglen =
                    makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_DISABLE_TXT_INFO_RAM), _message_VALSET_DISABLE_TXT_INFO_RAM);
                clearBuffer();
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable Info messages for M10 GPS RAM.\n");
                }
                delay(250);
                // Next disable Info txt messages in BBR layer
                msglen =
                    makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_DISABLE_TXT_INFO_BBR), _message_VALSET_DISABLE_TXT_INFO_BBR);
                clearBuffer();
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable Info messages for M10 GPS BBR.\n");
                }
                // Do M10 configuration for Power Management.

                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_PM_RAM), _message_VALSET_PM_RAM);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable powersaving for M10 GPS RAM.\n");
                }
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_PM_BBR), _message_VALSET_PM_BBR);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable powersaving for M10 GPS BBR.\n");
                }

                delay(250);
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_ITFM_RAM), _message_VALSET_ITFM_RAM);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable Jamming detection M10 GPS RAM.\n");
                }
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_ITFM_BBR), _message_VALSET_ITFM_BBR);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable Jamming detection M10 GPS BBR.\n");
                }

                // Here is where the init commands should go to do further M10 initialization.
                delay(250);
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_DISABLE_SBAS_RAM), _message_VALSET_DISABLE_SBAS_RAM);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable SBAS M10 GPS RAM.\n");
                }
                delay(750); // will cause a receiver restart so wait a bit
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_DISABLE_SBAS_BBR), _message_VALSET_DISABLE_SBAS_BBR);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to disable SBAS M10 GPS BBR.\n");
                }
                delay(750); // will cause a receiver restart so wait a bit
                // Done with initialization, Now enable wanted NMEA messages in BBR layer so they will survive a periodic sleep.
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_ENABLE_NMEA_BBR), _message_VALSET_ENABLE_NMEA_BBR);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable messages for M10 GPS BBR.\n");
                }
                delay(250);
                // Next enable wanted NMEA messages in RAM layer
                msglen = makeUBXPacket(0x06, 0x8A, sizeof(_message_VALSET_ENABLE_NMEA_RAM), _message_VALSET_ENABLE_NMEA_RAM);
                _serial_gps->write(UBXscratch, msglen);
                if (getACK(0x06, 0x8A, 300) != GNSS_RESPONSE_OK) {
                    LOG_WARN("Unable to enable messages for M10 GPS RAM.\n");
                }
                // As the M10 has no flash, the best we can do to preserve the config is to set it in RAM and BBR.
                // BBR will survive a restart, and power off for a while, but modules with small backup
                // batteries or super caps will not retain the config for a long power off time.
            }
        }
        didSerialInit = true;
    }

    notifyDeepSleepObserver.observe(&notifyDeepSleep);
    notifyGPSSleepObserver.observe(&notifyGPSSleep);

    return true;
}

GPS::~GPS()
{
    // we really should unregister our sleep observer
    notifyDeepSleepObserver.unobserve(&notifyDeepSleep);
    notifyGPSSleepObserver.observe(&notifyGPSSleep);
}

void GPS::setGPSPower(bool on, bool standbyOnly, uint32_t sleepTime)
{
    LOG_INFO("Setting GPS power=%d\n", on);
    if (on) {
        clearBuffer(); // drop any old data waiting in the buffer before re-enabling
        if (en_gpio)
            digitalWrite(en_gpio, on ? GPS_EN_ACTIVE : !GPS_EN_ACTIVE); // turn this on if defined, every time
    }
    isInPowersave = !on;
    if (!standbyOnly && en_gpio != 0 &&
        !(HW_VENDOR == meshtastic_HardwareModel_RAK4631 && (rotaryEncoderInterruptImpl1 || upDownInterruptImpl1))) {
        LOG_DEBUG("GPS powerdown using GPS_EN_ACTIVE\n");
        digitalWrite(en_gpio, on ? GPS_EN_ACTIVE : !GPS_EN_ACTIVE);
        return;
    }
#ifdef HAS_PMU // We only have PMUs on the T-Beam, and that board has a tiny battery to save GPS ephemera, so treat as a standby.
    if (pmu_found && PMU) {
        uint8_t model = PMU->getChipModel();
        if (model == XPOWERS_AXP2101) {
            if (HW_VENDOR == meshtastic_HardwareModel_TBEAM) {
                // t-beam v1.2 GNSS power channel
                on ? PMU->enablePowerOutput(XPOWERS_ALDO3) : PMU->disablePowerOutput(XPOWERS_ALDO3);
            } else if (HW_VENDOR == meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE) {
                // t-beam-s3-core GNSS  power channel
                on ? PMU->enablePowerOutput(XPOWERS_ALDO4) : PMU->disablePowerOutput(XPOWERS_ALDO4);
            }
        } else if (model == XPOWERS_AXP192) {
            // t-beam v1.1 GNSS  power channel
            on ? PMU->enablePowerOutput(XPOWERS_LDO3) : PMU->disablePowerOutput(XPOWERS_LDO3);
        }
        return;
    }
#endif
#ifdef PIN_GPS_STANDBY // Specifically the standby pin for L76K and clones
    if (on) {
        LOG_INFO("Waking GPS");
        pinMode(PIN_GPS_STANDBY, OUTPUT);
        digitalWrite(PIN_GPS_STANDBY, 1);
        return;
    } else {
        LOG_INFO("GPS entering sleep");
        // notifyGPSSleep.notifyObservers(NULL);
        pinMode(PIN_GPS_STANDBY, OUTPUT);
        digitalWrite(PIN_GPS_STANDBY, 0);
        return;
    }
#endif
    if (!on) {
        if (gnssModel == GNSS_MODEL_UBLOX) {
            uint8_t msglen;
            LOG_DEBUG("Sleep Time: %i\n", sleepTime);
            if (strncmp(info.hwVersion, "000A0000", 8) != 0) {
                for (int i = 0; i < 4; i++) {
                    gps->_message_PMREQ[0 + i] = sleepTime >> (i * 8); // Encode the sleep time in millis into the packet
                }
                msglen = gps->makeUBXPacket(0x02, 0x41, sizeof(_message_PMREQ), gps->_message_PMREQ);
            } else {
                for (int i = 0; i < 4; i++) {
                    gps->_message_PMREQ_10[4 + i] = sleepTime >> (i * 8); // Encode the sleep time in millis into the packet
                }
                msglen = gps->makeUBXPacket(0x02, 0x41, sizeof(_message_PMREQ_10), gps->_message_PMREQ_10);
            }
            gps->_serial_gps->write(gps->UBXscratch, msglen);
        }
    } else {
        if (gnssModel == GNSS_MODEL_UBLOX) {
            gps->_serial_gps->write(0xFF);
            clearBuffer(); // This often returns old data, so drop it
        }
    }
}

/// Record that we have a GPS
void GPS::setConnected()
{
    if (!hasGPS) {
        hasGPS = true;
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
    if (isAwake != on) {
        LOG_DEBUG("WANT GPS=%d\n", on);
        isAwake = on;
        if (!enabled) { // short circuit if the user has disabled GPS
            setGPSPower(false, false, 0);
            return;
        }

        if (on) {
            lastWakeStartMsec = millis();
        } else {
            lastSleepStartMsec = millis();
            if (GPSCycles == 1) { // Skipping initial lock time, as it will likely be much longer than average
                averageLockTime = lastSleepStartMsec - lastWakeStartMsec;
            } else if (GPSCycles > 1) {
                averageLockTime += ((int32_t)(lastSleepStartMsec - lastWakeStartMsec) - averageLockTime) / (int32_t)GPSCycles;
            }
            GPSCycles++;
            LOG_DEBUG("GPS Lock took %d, average %d\n", (lastSleepStartMsec - lastWakeStartMsec) / 1000, averageLockTime / 1000);
        }
        if ((int32_t)getSleepTime() - averageLockTime >
            15 * 60 * 1000) { // 15 minutes is probably long enough to make a complete poweroff worth it.
            setGPSPower(on, false, getSleepTime() - averageLockTime);
            return;
        } else if ((int32_t)getSleepTime() - averageLockTime > 10000) { // 10 seconds is enough for standby
#ifdef GPS_UC6580
            setGPSPower(on, false, getSleepTime() - averageLockTime);
#else
            setGPSPower(on, true, getSleepTime() - averageLockTime);
#endif
            return;
        }
        if (averageLockTime > 20000) {
            averageLockTime -= 1000; // eventually want to sleep again.
        }
        if (on)
            setGPSPower(true, true, 0); // make sure we don't have a fallthrough where GPS is stuck off
    }
}

/** Get how long we should stay looking for each acquisition in msecs
 */
uint32_t GPS::getWakeTime() const
{
    uint32_t t = config.position.position_broadcast_secs;

    if (t == UINT32_MAX)
        return t; // already maxint

    return getConfiguredOrDefaultMs(t, default_broadcast_interval_secs);
}

/** Get how long we should sleep between aqusition attempts in msecs
 */
uint32_t GPS::getSleepTime() const
{
    uint32_t t = config.position.gps_update_interval;

    // We'll not need the GPS thread to wake up again after first acq. with fixed position.
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED || config.position.fixed_position)
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
        LOG_DEBUG("publishing pos@%x:2, hasVal=%d, Sats=%d, GPSlock=%d\n", p.timestamp, hasValidLocation, p.sats_in_view,
                  hasLock());

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status = meshtastic::GPSStatus(hasValidLocation, isConnected(), isPowerSaving(), p);
        newStatus.notifyObservers(&status);
        if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
            positionModule->handleNewPosition();
        }
    }
}

int32_t GPS::runOnce()
{
    if (!GPSInitFinished) {
        if (!_serial_gps || config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT) {
            LOG_INFO("GPS set to not-present. Skipping probe.\n");
            return disable();
        }
        if (!setup())
            return 2000; // Setup failed, re-run in two seconds

        // We have now loaded our saved preferences from flash
        if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
            return disable();
        }
        // ONCE we will factory reset the GPS for bug #327
        if (!devicestate.did_gps_reset) {
            LOG_WARN("GPS FactoryReset requested\n");
            if (gps->factoryReset()) { // If we don't succeed try again next time
                devicestate.did_gps_reset = true;
                nodeDB.saveToDisk(SEGMENT_DEVICESTATE);
            }
        }
        GPSInitFinished = true;
    }

    // Repeaters have no need for GPS
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        return disable();
    }

    if (whileIdle()) {
        // if we have received valid NMEA claim we are connected
        setConnected();
    } else {
        if ((config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) && (gnssModel == GNSS_MODEL_UBLOX)) {
            // reset the GPS on next bootup
            if (devicestate.did_gps_reset && (millis() - lastWakeStartMsec > 60000) && !hasFlow()) {
                LOG_DEBUG("GPS is not communicating, trying factory reset on next bootup.\n");
                devicestate.did_gps_reset = false;
                nodeDB.saveDeviceStateToDisk();
                return disable(); // Stop the GPS thread as it can do nothing useful until next reboot.
            }
        }
    }
    // At least one GPS has a bad habit of losing its mind from time to time
    if (rebootsSeen > 2) {
        rebootsSeen = 0;
        LOG_DEBUG("Would normally factoryReset()\n");
        // gps->factoryReset();
    }

    // If we are overdue for an update, turn on the GPS and at least publish the current status
    uint32_t now = millis();
    uint32_t timeAsleep = now - lastSleepStartMsec;

    auto sleepTime = getSleepTime();
    if (!isAwake && (sleepTime != UINT32_MAX) &&
        ((timeAsleep > sleepTime) || (isInPowersave && timeAsleep > (sleepTime - averageLockTime)))) {
        // We now want to be awake - so wake up the GPS
        setAwake(true);
    }

    // While we are awake
    if (isAwake) {
        // LOG_DEBUG("looking for location\n");
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

    if (config.position.fixed_position == true && hasValidLocation)
        return disable(); // This should trigger when we have a fixed position, and get that first position

    // 9600bps is approx 1 byte per msec, so considering our buffer size we never need to wake more often than 200ms
    // if not awake we can run super infrquently (once every 5 secs?) to see if we need to wake.
    return isAwake ? GPS_THREAD_INTERVAL : 5000;
}

// clear the GPS rx buffer as quickly as possible
void GPS::clearBuffer()
{
    int x = _serial_gps->available();
    while (x--)
        _serial_gps->read();
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareDeepSleep(void *unused)
{
    LOG_INFO("GPS deep sleep!\n");

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
                    LOG_INFO("UBlox GNSS probe succeeded, using UBlox %s GNSS Module\n", (char *)buffer);
                } else {
                    LOG_INFO("UBlox GNSS probe succeeded, using UBlox GNSS Module\n");
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

GPS *GPS::createGps()
{
    int8_t _rx_gpio = config.position.rx_gpio;
    int8_t _tx_gpio = config.position.tx_gpio;
    int8_t _en_gpio = config.position.gps_en_gpio;
#if HAS_GPS && !defined(ARCH_ESP32)
    _rx_gpio = 1; // We only specify GPS serial ports on ESP32. Otherwise, these are just flags.
    _tx_gpio = 1;
#endif
#if defined(GPS_RX_PIN)
    if (!_rx_gpio)
        _rx_gpio = GPS_RX_PIN;
#endif
#if defined(GPS_TX_PIN)
    if (!_tx_gpio)
        _tx_gpio = GPS_TX_PIN;
#endif
#if defined(PIN_GPS_EN)
    if (!_en_gpio)
        _en_gpio = PIN_GPS_EN;
#endif
#ifdef ARCH_PORTDUINO
    if (!settingsMap[has_gps])
        return nullptr;
#endif
    if (!_rx_gpio || !_serial_gps) // Configured to have no GPS at all
        return nullptr;

    GPS *new_gps = new GPS;
    new_gps->rx_gpio = _rx_gpio;
    new_gps->tx_gpio = _tx_gpio;
    new_gps->en_gpio = _en_gpio;

    if (_en_gpio != 0) {
        LOG_DEBUG("Setting %d to output.\n", _en_gpio);
        pinMode(_en_gpio, OUTPUT);
        digitalWrite(_en_gpio, !GPS_EN_ACTIVE);
    }

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

    new_gps->setGPSPower(true, false, 0);

#ifdef PIN_GPS_RESET
    pinMode(PIN_GPS_RESET, OUTPUT);
    digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE); // assert for 10ms
    delay(10);
    digitalWrite(PIN_GPS_RESET, !GPS_RESET_MODE);
#endif
    new_gps->setAwake(true); // Wake GPS power before doing any init

    if (_serial_gps) {
#ifdef ARCH_ESP32
        // In esp32 framework, setRxBufferSize needs to be initialized before Serial
        _serial_gps->setRxBufferSize(SERIAL_BUFFER_SIZE); // the default is 256
#endif

//  ESP32 has a special set of parameters vs other arduino ports
#if defined(ARCH_ESP32)
        LOG_DEBUG("Using GPIO%d for GPS RX\n", new_gps->rx_gpio);
        LOG_DEBUG("Using GPIO%d for GPS TX\n", new_gps->tx_gpio);
        _serial_gps->begin(GPS_BAUDRATE, SERIAL_8N1, new_gps->rx_gpio, new_gps->tx_gpio);

#else
        _serial_gps->begin(GPS_BAUDRATE);
#endif

        /*
         * T-Beam-S3-Core will be preset to use gps Probe here, and other boards will not be changed first
         */
#if defined(GPS_UC6580)
        _serial_gps->updateBaudRate(115200);
#endif
    }
    return new_gps;
}

static int32_t toDegInt(RawDegrees d)
{
    int32_t degMult = 10000000; // 1e7
    int32_t r = d.deg * degMult + d.billionths / 100;
    if (d.negative)
        r *= -1;
    return r;
}

bool GPS::factoryReset()
{
#ifdef PIN_GPS_REINIT
    // The L76K GNSS on the T-Echo requires the RESET pin to be pulled LOW
    pinMode(PIN_GPS_REINIT, OUTPUT);
    digitalWrite(PIN_GPS_REINIT, 0);
    delay(150); // The L76K datasheet calls for at least 100MS delay
    digitalWrite(PIN_GPS_REINIT, 1);
#endif

    if (HW_VENDOR == meshtastic_HardwareModel_TBEAM) {
        byte _message_reset1[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1C, 0xA2};
        _serial_gps->write(_message_reset1, sizeof(_message_reset1));
        if (getACK(0x05, 0x01, 10000)) {
            LOG_INFO("Get ack success!\n");
        }
        delay(100);
        byte _message_reset2[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1B, 0xA1};
        _serial_gps->write(_message_reset2, sizeof(_message_reset2));
        if (getACK(0x05, 0x01, 10000)) {
            LOG_INFO("Get ack success!\n");
        }
        delay(100);
        byte _message_reset3[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0x1D, 0xB3};
        _serial_gps->write(_message_reset3, sizeof(_message_reset3));
        if (getACK(0x05, 0x01, 10000)) {
            LOG_INFO("Get ack success!\n");
        }
        // Reset device ram to COLDSTART state
        // byte _message_CFG_RST_COLDSTART[] = {0xB5, 0x62, 0x06, 0x04, 0x04, 0x00, 0xFF, 0xB9, 0x00, 0x00, 0xC6, 0x8B};
        // _serial_gps->write(_message_CFG_RST_COLDSTART, sizeof(_message_CFG_RST_COLDSTART));
        // delay(1000);
    } else {
        // send the UBLOX Factory Reset Command regardless of detect state, something is very wrong, just assume it's UBLOX.
        // Factory Reset
        byte _message_reset[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFB, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x17, 0x2B, 0x7E};
        _serial_gps->write(_message_reset, sizeof(_message_reset));
    }
    delay(1000);
    return true;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool GPS::lookForTime()
{
    auto ti = reader.time;
    auto d = reader.date;
    if (ti.isValid() && d.isValid()) { // Note: we don't check for updated, because we'll only be called if needed
        /* Convert to unix time
The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
(midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
*/
        struct tm t;
        t.tm_sec = ti.second();
        t.tm_min = ti.minute();
        t.tm_hour = ti.hour();
        t.tm_mday = d.day();
        t.tm_mon = d.month() - 1;
        t.tm_year = d.year() - 1900;
        t.tm_isdst = false;
        if (t.tm_mon > -1) {
            LOG_DEBUG("NMEA GPS time %02d-%02d-%02d %02d:%02d:%02d\n", d.year(), d.month(), t.tm_mday, t.tm_hour, t.tm_min,
                      t.tm_sec);
            perhapsSetRTC(RTCQualityGPS, t);
            return true;
        } else
            return false;
    } else
        return false;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool GPS::lookForLocation()
{
    // By default, TinyGPS++ does not parse GPGSA lines, which give us
    //   the 2D/3D fixType (see NMEAGPS.h)
    // At a minimum, use the fixQuality indicator in GPGGA (FIXME?)
    fixQual = reader.fixQuality();

#ifndef TINYGPS_OPTION_NO_STATISTICS
    if (reader.failedChecksum() > lastChecksumFailCount) {
        LOG_WARN("Warning, %u new GPS checksum failures, for a total of %u.\n", reader.failedChecksum() - lastChecksumFailCount,
                 reader.failedChecksum());
        lastChecksumFailCount = reader.failedChecksum();
    }
#endif

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    fixType = atoi(gsafixtype.value()); // will set to zero if no data
                                        // LOG_DEBUG("FIX QUAL=%d, TYPE=%d\n", fixQual, fixType);
#endif

    // check if GPS has an acceptable lock
    if (!hasLock())
        return false;

#ifdef GPS_EXTRAVERBOSE
    LOG_DEBUG("AGE: LOC=%d FIX=%d DATE=%d TIME=%d\n", reader.location.age(),
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
              gsafixtype.age(),
#else
              0,
#endif
              reader.date.age(), reader.time.age());
#endif // GPS_EXTRAVERBOSE

    // Is this a new point or are we re-reading the previous one?
    if (!reader.location.isUpdated())
        return false;

    // check if a complete GPS solution set is available for reading
    //   tinyGPSDatum::age() also includes isValid() test
    // FIXME
    if (!((reader.location.age() < GPS_SOL_EXPIRY_MS) &&
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
          (gsafixtype.age() < GPS_SOL_EXPIRY_MS) &&
#endif
          (reader.time.age() < GPS_SOL_EXPIRY_MS) && (reader.date.age() < GPS_SOL_EXPIRY_MS))) {
        LOG_WARN("SOME data is TOO OLD: LOC %u, TIME %u, DATE %u\n", reader.location.age(), reader.time.age(), reader.date.age());
        return false;
    }

    // We know the solution is fresh and valid, so just read the data
    auto loc = reader.location.value();

    // Bail out EARLY to avoid overwriting previous good data (like #857)
    if (toDegInt(loc.lat) > 900000000) {
#ifdef GPS_EXTRAVERBOSE
        LOG_DEBUG("Bail out EARLY on LAT %i\n", toDegInt(loc.lat));
#endif
        return false;
    }
    if (toDegInt(loc.lng) > 1800000000) {
#ifdef GPS_EXTRAVERBOSE
        LOG_DEBUG("Bail out EARLY on LNG %i\n", toDegInt(loc.lng));
#endif
        return false;
    }

    p.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

    // Dilution of precision (an accuracy metric) is reported in 10^2 units, so we need to scale down when we use it
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    p.HDOP = reader.hdop.value();
    p.PDOP = TinyGPSPlus::parseDecimal(gsapdop.value());
    // LOG_DEBUG("PDOP=%d, HDOP=%d\n", p.PDOP, p.HDOP);
#else
    // FIXME! naive PDOP emulation (assumes VDOP==HDOP)
    // correct formula is PDOP = SQRT(HDOP^2 + VDOP^2)
    p.HDOP = reader.hdop.value();
    p.PDOP = 1.41 * reader.hdop.value();
#endif

    // Discard incomplete or erroneous readings
    if (reader.hdop.value() == 0) {
        LOG_WARN("BOGUS hdop.value() REJECTED: %d\n", reader.hdop.value());
        return false;
    }

    p.latitude_i = toDegInt(loc.lat);
    p.longitude_i = toDegInt(loc.lng);

    p.altitude_geoidal_separation = reader.geoidHeight.meters();
    p.altitude_hae = reader.altitude.meters() + p.altitude_geoidal_separation;
    p.altitude = reader.altitude.meters();

    p.fix_quality = fixQual;
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    p.fix_type = fixType;
#endif

    // positional timestamp
    struct tm t;
    t.tm_sec = reader.time.second();
    t.tm_min = reader.time.minute();
    t.tm_hour = reader.time.hour();
    t.tm_mday = reader.date.day();
    t.tm_mon = reader.date.month() - 1;
    t.tm_year = reader.date.year() - 1900;
    t.tm_isdst = false;
    p.timestamp = mktime(&t);

    // Nice to have, if available
    if (reader.satellites.isUpdated()) {
        p.sats_in_view = reader.satellites.value();
    }

    if (reader.course.isUpdated() && reader.course.isValid()) {
        if (reader.course.value() < 36000) { // sanity check
            p.ground_track =
                reader.course.value() * 1e3; // Scale the heading (in degrees * 10^-2) to match the expected degrees * 10^-5
        } else {
            LOG_WARN("BOGUS course.value() REJECTED: %d\n", reader.course.value());
        }
    }

    if (reader.speed.isUpdated() && reader.speed.isValid()) {
        p.ground_speed = reader.speed.kmph();
    }

    return true;
}

bool GPS::hasLock()
{
    // Using GPGGA fix quality indicator
    if (fixQual >= 1 && fixQual <= 5) {
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
        // Use GPGSA fix type 2D/3D (better) if available
        if (fixType == 3 || fixType == 0) // zero means "no data received"
#endif
            return true;
    }

    return false;
}

bool GPS::hasFlow()
{
    return reader.passedChecksum() > 0;
}

bool GPS::whileIdle()
{
    int charsInBuf = 0;
    bool isValid = false;
    if (!isAwake) {
        clearBuffer();
        return isAwake;
    }
#ifdef SERIAL_BUFFER_SIZE
    if (_serial_gps->available() >= SERIAL_BUFFER_SIZE - 1) {
        LOG_WARN("GPS Buffer full with %u bytes waiting. Flushing to avoid corruption.\n", _serial_gps->available());
        clearBuffer();
    }
#endif
    // if (_serial_gps->available() > 0)
    // LOG_DEBUG("GPS Bytes Waiting: %u\n", _serial_gps->available());
    // First consume any chars that have piled up at the receiver
    while (_serial_gps->available() > 0) {
        int c = _serial_gps->read();
        UBXscratch[charsInBuf] = c;
#ifdef GPS_DEBUG
        LOG_DEBUG("%c", c);
#endif
        isValid |= reader.encode(c);
        if (charsInBuf > sizeof(UBXscratch) - 10 || c == '\r') {
            if (strnstr((char *)UBXscratch, "$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50", charsInBuf)) {
                rebootsSeen++;
            }
            charsInBuf = 0;
        } else {
            charsInBuf++;
        }
    }
    return isValid;
}
void GPS::enable()
{
    enabled = true;
    setInterval(GPS_THREAD_INTERVAL);
    setAwake(true);
}

int32_t GPS::disable()
{
    enabled = false;
    setInterval(INT32_MAX);
    setAwake(false);

    return INT32_MAX;
}

void GPS::toggleGpsMode()
{
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
        LOG_DEBUG("Flag set to false for gps power. GpsMode: DISABLED\n");
        disable();
    } else if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED) {
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
        LOG_DEBUG("Flag set to true to restore power. GpsMode: ENABLED\n");
        enable();
    }
}