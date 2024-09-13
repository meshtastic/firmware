#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "Default.h"
#include "GPS.h"
#include "GpioLogic.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "RTC.h"

#include "main.h" // pmu_found
#include "sleep.h"

#include "GPSUpdateScheduling.h"
#include "cas.h"
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

GPSUpdateScheduling scheduling;

/// Multiple GPS instances might use the same serial port (in sequence), but we can
/// only init that port once.
static bool didSerialInit;

struct uBloxGnssModelInfo info;
uint8_t uBloxProtocolVersion;
#define GPS_SOL_EXPIRY_MS 5000 // in millis. give 1 second time to combine different sentences. NMEA Frequency isn't higher anyway
#define NMEA_MSG_GXGSA "GNGSA" // GSA message (GPGSA, GNGSA etc)

// For logging
const char *getGPSPowerStateString(GPSPowerState state)
{
    switch (state) {
    case GPS_ACTIVE:
        return "ACTIVE";
    case GPS_IDLE:
        return "IDLE";
    case GPS_SOFTSLEEP:
        return "SOFTSLEEP";
    case GPS_HARDSLEEP:
        return "HARDSLEEP";
    case GPS_OFF:
        return "OFF";
    default:
        assert(false); // Unhandled enum value..
    }
}

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

// Calculate the checksum for a CAS packet
void GPS::CASChecksum(uint8_t *message, size_t length)
{
    uint32_t cksum = ((uint32_t)message[5] << 24); // Message ID
    cksum += ((uint32_t)message[4]) << 16;         // Class
    cksum += message[2];                           // Payload Len

    // Iterate over the payload as a series of uint32_t's and
    // accumulate the cksum
    uint32_t const *payload = (uint32_t *)(message + 6);
    for (size_t i = 0; i < (length - 10) / 4; i++) {
        uint32_t pl = payload[i];
        cksum += pl;
    }

    // Place the checksum values in the message
    message[length - 4] = (cksum & 0xFF);
    message[length - 3] = (cksum & (0xFF << 8)) >> 8;
    message[length - 2] = (cksum & (0xFF << 16)) >> 16;
    message[length - 1] = (cksum & (0xFF << 24)) >> 24;
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

// Function to create a CAS packet for editing in memory
uint8_t GPS::makeCASPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg)
{
    // General CAS structure
    //        | H1   | H2   | payload_len | cls  | msg  | Payload       ...   | Checksum                  |
    // Size:  | 1    | 1    | 2           | 1    | 1    | payload_len         | 4                         |
    // Pos:   | 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7      ...   | 6 + payload_len ...       |
    //        |------|------|-------------|------|------|------|--------------|---------------------------|
    //        | 0xBA | 0xCE | 0xXX | 0xXX | 0xXX | 0xXX | 0xXX | 0xXX   ...   | 0xXX | 0xXX | 0xXX | 0xXX |

    // Construct the CAS packet
    UBXscratch[0] = 0xBA;         // header 1 (0xBA)
    UBXscratch[1] = 0xCE;         // header 2 (0xCE)
    UBXscratch[2] = payload_size; // length 1
    UBXscratch[3] = 0;            // length 2
    UBXscratch[4] = class_id;     // class
    UBXscratch[5] = msg_id;       // id

    UBXscratch[6 + payload_size] = 0x00; // Checksum
    UBXscratch[7 + payload_size] = 0x00;
    UBXscratch[8 + payload_size] = 0x00;
    UBXscratch[9 + payload_size] = 0x00;

    for (int i = 0; i < payload_size; i++) {
        UBXscratch[6 + i] = pgm_read_byte(&msg[i]);
    }
    CASChecksum(UBXscratch, (payload_size + 10));

#if defined(GPS_DEBUG) && defined(DEBUG_PORT)
    LOG_DEBUG("Constructed CAS packet: \n");
    DEBUG_PORT.hexDump(MESHTASTIC_LOG_LEVEL_DEBUG, UBXscratch, payload_size + 10);
#endif
    return (payload_size + 10);
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
            LOG_DEBUG("%c", (b >= 32 && b <= 126) ? b : '.');
#endif
            buffer[bytesRead] = b;
            bytesRead++;
            if ((bytesRead == 767) || (b == '\r')) {
                if (strnstr((char *)buffer, message, bytesRead) != nullptr) {
#ifdef GPS_DEBUG
                    LOG_DEBUG("\r\nFound: %s\r\n", message); // Log the found message
#endif
                    return GNSS_RESPONSE_OK;
                } else {
                    bytesRead = 0;
#ifdef GPS_DEBUG
                    LOG_DEBUG("\r\n");
#endif
                }
            }
        }
    }
#ifdef GPS_DEBUG
    LOG_DEBUG("\n");
#endif
    return GNSS_RESPONSE_NONE;
}

GPS_RESPONSE GPS::getACKCas(uint8_t class_id, uint8_t msg_id, uint32_t waitMillis)
{
    uint32_t startTime = millis();
    uint8_t buffer[CAS_ACK_NACK_MSG_SIZE] = {0};
    uint8_t bufferPos = 0;

    // CAS-ACK-(N)ACK structure
    //         | H1   | H2   | Payload Len | cls  | msg  | Payload                   | Checksum (4)              |
    //         |      |      |             |      |      | Cls  | Msg  | Reserved    |                           |
    //         |------|------|-------------|------|------|------|------|-------------|---------------------------|
    // ACK-NACK| 0xBA | 0xCE | 0x04 | 0x00 | 0x05 | 0x00 | 0xXX | 0xXX | 0x00 | 0x00 | 0xXX | 0xXX | 0xXX | 0xXX |
    // ACK-ACK | 0xBA | 0xCE | 0x04 | 0x00 | 0x05 | 0x01 | 0xXX | 0xXX | 0x00 | 0x00 | 0xXX | 0xXX | 0xXX | 0xXX |

    while (millis() - startTime < waitMillis) {
        if (_serial_gps->available()) {
            buffer[bufferPos++] = _serial_gps->read();

            // keep looking at the first two bytes of buffer until
            // we have found the CAS frame header (0xBA, 0xCE), if not
            // keep reading bytes until we find a frame header or we run
            // out of time.
            if ((bufferPos == 2) && !(buffer[0] == 0xBA && buffer[1] == 0xCE)) {
                buffer[0] = buffer[1];
                buffer[1] = 0;
                bufferPos = 1;
            }
        }

        // we have read all the bytes required for the Ack/Nack (14-bytes)
        // and we must have found a frame to get this far
        if (bufferPos == sizeof(buffer) - 1) {
            uint8_t msg_cls = buffer[4];     // message class should be 0x05
            uint8_t msg_msg_id = buffer[5];  // message id should be 0x00 or 0x01
            uint8_t payload_cls = buffer[6]; // payload class id
            uint8_t payload_msg = buffer[7]; // payload message id

            // Check for an ACK-ACK for the specified class and message id
            if ((msg_cls == 0x05) && (msg_msg_id == 0x01) && payload_cls == class_id && payload_msg == msg_id) {
#ifdef GPS_DEBUG
                LOG_INFO("Got ACK for class %02X message %02X in %d millis.\n", class_id, msg_id, millis() - startTime);
#endif
                return GNSS_RESPONSE_OK;
            }

            // Check for an ACK-NACK for the specified class and message id
            if ((msg_cls == 0x05) && (msg_msg_id == 0x00) && payload_cls == class_id && payload_msg == msg_id) {
#ifdef GPS_DEBUG
                LOG_WARN("Got NACK for class %02X message %02X in %d millis.\n", class_id, msg_id, millis() - startTime);
#endif
                return GNSS_RESPONSE_NAK;
            }

            // This isn't the frame we are looking for, clear the buffer
            // and try again until we run out of time.
            memset(buffer, 0x0, sizeof(buffer));
            bufferPos = 0;
        }
    }
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
        if (tx_gpio && gnssModel == GNSS_MODEL_UNKNOWN) {

            // if GPS_BAUDRATE is specified in variant (i.e. not 9600), skip to the specified rate.
            if (speedSelect == 0 && GPS_BAUDRATE != serialSpeeds[speedSelect]) {
                speedSelect = std::find(serialSpeeds, std::end(serialSpeeds), GPS_BAUDRATE) - serialSpeeds;
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
            }
        } else {
            gnssModel = GNSS_MODEL_UNKNOWN;
        }

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
        } else if (gnssModel == GNSS_MODEL_MTK_L76B) {
            // Waveshare Pico-GPS hat uses the L76B with 9600 baud
            // Initialize the L76B Chip, use GPS + GLONASS
            // See note in L76_Series_GNSS_Protocol_Specification, chapter 3.29
            _serial_gps->write("$PMTK353,1,1,0,0,0*2B\r\n");
            // Above command will reset the GPS and takes longer before it will accept new commands
            delay(1000);
            // only ask for RMC and GGA (GNRMC and GNGGA)
            // See note in L76_Series_GNSS_Protocol_Specification, chapter 2.1
            _serial_gps->write("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n");
            delay(250);
            // Enable SBAS
            _serial_gps->write("$PMTK301,2*2E\r\n");
            delay(250);
            // Enable PPS for 2D/3D fix only
            _serial_gps->write("$PMTK285,3,100*3F\r\n");
            delay(250);
            // Switch to Fitness Mode, for running and walking purpose with low speed (<5 m/s)
            _serial_gps->write("$PMTK886,1*29\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_ATGM336H) {
            // Set the intial configuration of the device - these _should_ work for most AT6558 devices
            msglen = makeCASPacket(0x06, 0x07, sizeof(_message_CAS_CFG_NAVX_CONF), _message_CAS_CFG_NAVX_CONF);
            _serial_gps->write(UBXscratch, msglen);
            if (getACKCas(0x06, 0x07, 250) != GNSS_RESPONSE_OK) {
                LOG_WARN("ATGM336H - Could not set Configuration");
            }

            // Set the update frequence to 1Hz
            msglen = makeCASPacket(0x06, 0x04, sizeof(_message_CAS_CFG_RATE_1HZ), _message_CAS_CFG_RATE_1HZ);
            _serial_gps->write(UBXscratch, msglen);
            if (getACKCas(0x06, 0x04, 250) != GNSS_RESPONSE_OK) {
                LOG_WARN("ATGM336H - Could not set Update Frequency");
            }

            // Set the NEMA output messages
            // Ask for only RMC and GGA
            uint8_t fields[] = {CAS_NEMA_RMC, CAS_NEMA_GGA};
            for (unsigned int i = 0; i < sizeof(fields); i++) {
                // Construct a CAS-CFG-MSG packet
                uint8_t cas_cfg_msg_packet[] = {0x4e, fields[i], 0x01, 0x00};
                msglen = makeCASPacket(0x06, 0x01, sizeof(cas_cfg_msg_packet), cas_cfg_msg_packet);
                _serial_gps->write(UBXscratch, msglen);
                if (getACKCas(0x06, 0x01, 250) != GNSS_RESPONSE_OK) {
                    LOG_WARN("ATGM336H - Could not enable NMEA MSG: %d\n", fields[i]);
                }
            }
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
            // Turn off GSA messages, TinyGPS++ doesn't use this message.
            _serial_gps->write("$CFGMSG,0,2,0\r\n");
            delay(250);
            // Turn off NOTICE __TXT messages, these may provide Unicore some info but we don't care.
            _serial_gps->write("$CFGMSG,6,0,0\r\n");
            delay(250);
            _serial_gps->write("$CFGMSG,6,1,0\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_AG3335 || gnssModel == GNSS_MODEL_AG3352) {

            _serial_gps->write("$PAIR066,1,0,1,0,0,1*3B\r\n"); // Enable GPS+GALILEO+NAVIC

            // Configure NMEA (sentences will output once per fix)
            _serial_gps->write("$PAIR062,0,1*3F\r\n"); // GGA ON
            _serial_gps->write("$PAIR062,1,0*3F\r\n"); // GLL OFF
            _serial_gps->write("$PAIR062,2,0*3C\r\n"); // GSA OFF
            _serial_gps->write("$PAIR062,3,0*3D\r\n"); // GSV OFF
            _serial_gps->write("$PAIR062,4,1*3B\r\n"); // RMC ON
            _serial_gps->write("$PAIR062,5,0*3B\r\n"); // VTG OFF
            _serial_gps->write("$PAIR062,6,0*38\r\n"); // ZDA ON

            delay(250);
            _serial_gps->write("$PAIR513*3D\r\n"); // save configuration

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
                    msglen = makeUBXPacket(0x06, 0x3B, sizeof(_message_CFG_PM2), _message_CFG_PM2);
                    _serial_gps->write(UBXscratch, msglen);
                    if (getACK(0x06, 0x3B, 500) != GNSS_RESPONSE_OK) {
                        LOG_WARN("Unable to enable powersaving details for GPS.\n");
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

                        msglen = makeUBXPacket(0x06, 0x3B, sizeof(_message_CFG_PM2), _message_CFG_PM2);
                        _serial_gps->write(UBXscratch, msglen);
                        if (getACK(0x06, 0x3B, 500) != GNSS_RESPONSE_OK) {
                            LOG_WARN("Unable to enable powersaving details for GPS.\n");
                        }
                    }
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
            msglen = makeUBXPacket(0x06, 0x09, sizeof(_message_SAVE), _message_SAVE);
            _serial_gps->write(UBXscratch, msglen);
            if (getACK(0x06, 0x09, 2000) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to save GNSS module configuration.\n");
            } else {
                LOG_INFO("GNSS module configuration saved!\n");
            }
        }
        didSerialInit = true;
    }

    notifyDeepSleepObserver.observe(&notifyDeepSleep);

    return true;
}

GPS::~GPS()
{
    // we really should unregister our sleep observer
    notifyDeepSleepObserver.unobserve(&notifyDeepSleep);
}
// Put the GPS hardware into a specified state
void GPS::setPowerState(GPSPowerState newState, uint32_t sleepTime)
{
    // Update the stored GPSPowerstate, and create local copies
    GPSPowerState oldState = powerState;
    powerState = newState;
    LOG_INFO("GPS power state moving from %s to %s\n", getGPSPowerStateString(oldState), getGPSPowerStateString(newState));

#ifdef HELTEC_MESH_NODE_T114
    if ((oldState == GPS_OFF || oldState == GPS_HARDSLEEP) && (newState != GPS_OFF && newState != GPS_HARDSLEEP)) {
        _serial_gps->begin(serialSpeeds[speedSelect]);
    } else if ((newState == GPS_OFF || newState == GPS_HARDSLEEP) && (oldState != GPS_OFF && oldState != GPS_HARDSLEEP)) {
        _serial_gps->end();
    }
#endif
    switch (newState) {
    case GPS_ACTIVE:
    case GPS_IDLE:
        if (oldState == GPS_ACTIVE || oldState == GPS_IDLE) // If hardware already awake, no changes needed
            break;
        if (oldState != GPS_ACTIVE && oldState != GPS_IDLE) // If hardware just waking now, clear buffer
            clearBuffer();
        powerMon->setState(meshtastic_PowerMon_State_GPS_Active); // Report change for power monitoring (during testing)
        writePinEN(true);                                         // Power (EN pin): on
        setPowerPMU(true);                                        // Power (PMU): on
        writePinStandby(false);                                   // Standby (pin): awake (not standby)
        setPowerUBLOX(true);                                      // Standby (UBLOX): awake
        break;

    case GPS_SOFTSLEEP:
        powerMon->clearState(meshtastic_PowerMon_State_GPS_Active); // Report change for power monitoring (during testing)
        writePinEN(true);                                           // Power (EN pin): on
        setPowerPMU(true);                                          // Power (PMU): on
        writePinStandby(true);                                      // Standby (pin): asleep (not awake)
        setPowerUBLOX(false, sleepTime);                            // Standby (UBLOX): asleep, timed
        break;

    case GPS_HARDSLEEP:
        powerMon->clearState(meshtastic_PowerMon_State_GPS_Active); // Report change for power monitoring (during testing)
        writePinEN(false);                                          // Power (EN pin): off
        setPowerPMU(false);                                         // Power (PMU): off
        writePinStandby(true);                                      // Standby (pin): asleep (not awake)
        setPowerUBLOX(false, sleepTime);                            // Standby (UBLOX): asleep, timed
#ifdef GNSS_AIROHA
        if (config.position.gps_update_interval * 1000 >= GPS_FIX_HOLD_TIME * 2) {
            digitalWrite(PIN_GPS_EN, LOW);
        }
#endif
        break;

    case GPS_OFF:
        assert(sleepTime == 0);                                     // This is an indefinite sleep
        powerMon->clearState(meshtastic_PowerMon_State_GPS_Active); // Report change for power monitoring (during testing)
        writePinEN(false);                                          // Power (EN pin): off
        setPowerPMU(false);                                         // Power (PMU): off
        writePinStandby(true);                                      // Standby (pin): asleep
        setPowerUBLOX(false, 0);                                    // Standby (UBLOX): asleep, indefinitely
#ifdef GNSS_AIROHA
        if (config.position.gps_update_interval * 1000 >= GPS_FIX_HOLD_TIME * 2) {
            digitalWrite(PIN_GPS_EN, LOW);
        }
#endif
        break;
    }
}

// Set power with EN pin, if relevant
void GPS::writePinEN(bool on)
{
    // Abort: if conflict with Canned Messages when using Wisblock(?)
    if (HW_VENDOR == meshtastic_HardwareModel_RAK4631 && (rotaryEncoderInterruptImpl1 || upDownInterruptImpl1))
        return;

    // Write and log
    enablePin->set(on);
#ifdef GPS_EXTRAVERBOSE
    LOG_DEBUG("Pin EN %s\n", val == HIGH ? "HIGH" : "LOW");
#endif
}

// Set the value of the STANDBY pin, if relevant
// true for standby state, false for awake
void GPS::writePinStandby(bool standby)
{
#ifdef PIN_GPS_STANDBY // Specifically the standby pin for L76B, L76K and clones

// Determine the new value for the pin
// Normally: active HIGH for awake
#ifdef PIN_GPS_STANDBY_INVERTED
    bool val = standby;
#else
    bool val = !standby;
#endif

    // Write and log
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    digitalWrite(PIN_GPS_STANDBY, val);
#ifdef GPS_EXTRAVERBOSE
    LOG_DEBUG("Pin STANDBY %s\n", val == HIGH ? "HIGH" : "LOW");
#endif
#endif
}

// Enable / Disable GPS with PMU, if present
void GPS::setPowerPMU(bool on)
{
    // We only have PMUs on the T-Beam, and that board has a tiny battery to save GPS ephemera,
    // so treat as a standby.
#ifdef HAS_PMU
    // Abort: if no PMU
    if (!pmu_found)
        return;

    // Abort: if PMU not initialized
    if (!PMU)
        return;

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

#ifdef GPS_EXTRAVERBOSE
    LOG_DEBUG("PMU %s\n", on ? "on" : "off");
#endif
#endif
}

// Set UBLOX power, if relevant
void GPS::setPowerUBLOX(bool on, uint32_t sleepMs)
{
    // Abort: if not UBLOX hardware
    if (gnssModel != GNSS_MODEL_UBLOX)
        return;

    // If waking
    if (on) {
        gps->_serial_gps->write(0xFF);
        clearBuffer(); // This often returns old data, so drop it
#ifdef GPS_EXTRAVERBOSE
        LOG_DEBUG("UBLOX: wake\n");
#endif
    }

    // If putting to sleep
    else {
        uint8_t msglen;

        // If we're being asked to sleep indefinitely, make *sure* we're awake first, to process the new sleep command
        if (sleepMs == 0) {
            setPowerUBLOX(true);
            delay(500);
        }

        // Determine hardware version
        if (strncmp(info.hwVersion, "000A0000", 8) != 0) {
            // Encode the sleep time in millis into the packet
            for (int i = 0; i < 4; i++)
                gps->_message_PMREQ[0 + i] = sleepMs >> (i * 8);

            // Record the message length
            msglen = gps->makeUBXPacket(0x02, 0x41, sizeof(_message_PMREQ), gps->_message_PMREQ);
        } else {
            // Encode the sleep time in millis into the packet
            for (int i = 0; i < 4; i++)
                gps->_message_PMREQ_10[4 + i] = sleepMs >> (i * 8);

            // Record the message length
            msglen = gps->makeUBXPacket(0x02, 0x41, sizeof(_message_PMREQ_10), gps->_message_PMREQ_10);
        }

        // Send the UBX packet
        gps->_serial_gps->write(gps->UBXscratch, msglen);

#ifdef GPS_EXTRAVERBOSE
        LOG_DEBUG("UBLOX: sleep for %dmS\n", sleepMs);
#endif
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

// We want a GPS lock. Wake the hardware
void GPS::up()
{
    scheduling.informSearching();
    setPowerState(GPS_ACTIVE);
}

// We've got a GPS lock. Enter a low power state, potentially.
void GPS::down()
{
    scheduling.informGotLock();
    uint32_t predictedSearchDuration = scheduling.predictedSearchDurationMs();
    uint32_t sleepTime = scheduling.msUntilNextSearch();
    uint32_t updateInterval = Default::getConfiguredOrDefaultMs(config.position.gps_update_interval);

    LOG_DEBUG("%us until next search\n", sleepTime / 1000);

    // If update interval less than 10 seconds, no attempt to sleep
    if (updateInterval <= 10 * 1000UL || sleepTime == 0)
        setPowerState(GPS_IDLE);

    else {
        // Check whether the GPS hardware is capable of GPS_SOFTSLEEP
        // If not, fallback to GPS_HARDSLEEP instead
        bool softsleepSupported = false;
        if (gnssModel == GNSS_MODEL_UBLOX) // U-blox is supported via PMREQ
            softsleepSupported = true;
#ifdef PIN_GPS_STANDBY // L76B, L76K and clones have a standby pin
        softsleepSupported = true;
#endif

        // How long does gps_update_interval need to be, for GPS_HARDSLEEP to become more efficient than GPS_SOFTSLEEP?
        // Heuristic equation. A compromise manually fitted to power observations from U-blox NEO-6M and M10050
        // https://www.desmos.com/calculator/6gvjghoumr
        // This is not particularly accurate, but probably an impromevement over a single, fixed threshold
        uint32_t hardsleepThreshold = (2750 * pow(predictedSearchDuration / 1000, 1.22));
        LOG_DEBUG("gps_update_interval >= %us needed to justify hardsleep\n", hardsleepThreshold / 1000);

        // If update interval too short: softsleep (if supported by hardware)
        if (softsleepSupported && updateInterval < hardsleepThreshold)
            setPowerState(GPS_SOFTSLEEP, sleepTime);

        // If update interval long enough (or softsleep unsupported): hardsleep instead
        else
            setPowerState(GPS_HARDSLEEP, sleepTime);
    }
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
                nodeDB->saveToDisk(SEGMENT_DEVICESTATE);
            }
        }
        GPSInitFinished = true;
    }

    // Repeaters have no need for GPS
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        return disable();
    }

    if (whileActive()) {
        // if we have received valid NMEA claim we are connected
        setConnected();
    } else {
        if ((config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) && (gnssModel == GNSS_MODEL_UBLOX)) {
            // reset the GPS on next bootup
            if (devicestate.did_gps_reset && scheduling.elapsedSearchMs() > 60 * 1000UL && !hasFlow()) {
                LOG_DEBUG("GPS is not communicating, trying factory reset on next bootup.\n");
                devicestate.did_gps_reset = false;
                nodeDB->saveToDisk(SEGMENT_DEVICESTATE);
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

    // If we're due for an update, wake the GPS
    if (!config.position.fixed_position && powerState != GPS_ACTIVE && scheduling.isUpdateDue())
        up();

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

    bool tooLong = scheduling.searchedTooLong();
    if (tooLong)
        LOG_WARN("Couldn't publish a valid location: didn't get a GPS lock in time.\n");

    // Once we get a location we no longer desperately want an update
    // LOG_DEBUG("gotLoc %d, tooLong %d, gotTime %d\n", gotLoc, tooLong, gotTime);
    if ((gotLoc && gotTime) || tooLong) {

        if (tooLong) {
            // we didn't get a location during this ack window, therefore declare loss of lock
            if (hasValidLocation) {
                LOG_DEBUG("hasValidLocation FALLING EDGE\n");
            }
            p = meshtastic_Position_init_default;
            hasValidLocation = false;
        }

        down();
        shouldPublish = true; // publish our update for this just finished acquisition window
    }

    // If state has changed do a publish
    publishUpdate();

    if (config.position.fixed_position == true && hasValidLocation)
        return disable(); // This should trigger when we have a fixed position, and get that first position

    // 9600bps is approx 1 byte per msec, so considering our buffer size we never need to wake more often than 200ms
    // if not awake we can run super infrquently (once every 5 secs?) to see if we need to wake.
    return (powerState == GPS_ACTIVE) ? GPS_THREAD_INTERVAL : 5000;
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
    disable();
    return 0;
}

#define PROBE_SIMPLE(CHIP, TOWRITE, RESPONSE, DRIVER, TIMEOUT, ...)                                                              \
    LOG_DEBUG("Trying " TOWRITE " (" CHIP ") ...\n");                                                                            \
    clearBuffer();                                                                                                               \
    _serial_gps->write(TOWRITE "\r\n");                                                                                          \
    if (getACK(RESPONSE, TIMEOUT) == GNSS_RESPONSE_OK) {                                                                         \
        LOG_INFO(CHIP " detected, using " #DRIVER " Module\n");                                                                  \
        return DRIVER;                                                                                                           \
    }

GnssModel_t GPS::probe(int serialSpeed)
{
#if defined(ARCH_NRF52) || defined(ARCH_PORTDUINO) || defined(ARCH_RP2040) || defined(ARCH_STM32WL)
    _serial_gps->end();
    _serial_gps->begin(serialSpeed);
#else
    if (_serial_gps->baudRate() != serialSpeed) {
        LOG_DEBUG("Setting Baud to %i\n", serialSpeed);
        _serial_gps->updateBaudRate(serialSpeed);
    }
#endif

    memset(&info, 0, sizeof(struct uBloxGnssModelInfo));
    uint8_t buffer[768] = {0};
    delay(100);

    // Close all NMEA sentences, valid for L76K, ATGM336H (and likely other AT6558 devices)
    _serial_gps->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(20);

    // Unicore UFirebirdII Series: UC6580, UM620, UM621, UM670A, UM680A, or UM681A
    PROBE_SIMPLE("UC6580", "$PDTINFO", "UC6580", GNSS_MODEL_UC6580, 500);
    PROBE_SIMPLE("UM600", "$PDTINFO", "UM600", GNSS_MODEL_UC6580, 500);
    PROBE_SIMPLE("ATGM336H", "$PCAS06,1*1A", "$GPTXT,01,01,02,HW=ATGM336H", GNSS_MODEL_ATGM336H, 500);
    /* ATGM332D series (-11(GPS), -21(BDS), -31(GPS+BDS), -51(GPS+GLONASS), -71-0(GPS+BDS+GLONASS))
    based on AT6558 */
    PROBE_SIMPLE("ATGM332D", "$PCAS06,1*1A", "$GPTXT,01,01,02,HW=ATGM332D", GNSS_MODEL_ATGM336H, 500);

    /* Airoha (Mediatek) AG3335A/M/S, A3352Q, Quectel L89 2.0, SimCom SIM65M */
    _serial_gps->write("$PAIR062,2,0*3C\r\n"); // GSA OFF to reduce volume
    _serial_gps->write("$PAIR062,3,0*3D\r\n"); // GSV OFF to reduce volume
    _serial_gps->write("$PAIR513*3D\r\n");     // save configuration
    PROBE_SIMPLE("AG3335", "$PAIR021*39", "$PAIR021,AG3335", GNSS_MODEL_AG3335, 500);
    PROBE_SIMPLE("AG3352", "$PAIR021*39", "$PAIR021,AG3352", GNSS_MODEL_AG3352, 500);
    PROBE_SIMPLE("LC86", "$PQTMVERNO*58", "$PQTMVERNO,LC86", GNSS_MODEL_AG3352, 500);

    PROBE_SIMPLE("L76K", "$PCAS06,0*1B", "$GPTXT,01,01,02,SW=", GNSS_MODEL_MTK, 500);

    // Close all NMEA sentences, valid for L76B MTK platform (Waveshare Pico GPS)
    _serial_gps->write("$PMTK514,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2E\r\n");
    delay(20);

    PROBE_SIMPLE("L76B", "$PMTK605*31", "Quectel-L76B", GNSS_MODEL_MTK_L76B, 500);

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
#if defined(ARCH_NRF52) || defined(ARCH_PORTDUINO) || defined(ARCH_RP2040) || defined(ARCH_STM32WL)
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

    GpioVirtPin *virtPin = new GpioVirtPin();
    new_gps->enablePin = virtPin; // Always at least populate a virtual pin
    if (_en_gpio) {
        GpioPin *p = new GpioHwPin(_en_gpio);

        if (!GPS_EN_ACTIVE) { // Need to invert the pin before hardware
            new GpioNotTransformer(
                virtPin, p); // We just leave this created object on the heap so it can stay watching virtPin and driving en_gpio
        } else {
            new GpioUnaryTransformer(
                virtPin, p); // We just leave this created object on the heap so it can stay watching virtPin and driving en_gpio
        }
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

    // Make sure the GPS is awake before performing any init.
    new_gps->up();

#ifdef PIN_GPS_RESET
    pinMode(PIN_GPS_RESET, OUTPUT);
    digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE); // assert for 10ms
    delay(10);
    digitalWrite(PIN_GPS_RESET, !GPS_RESET_MODE);
#endif

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
    } else if (gnssModel == GNSS_MODEL_MTK) {
        // send the CAS10 to perform a factory restart of the device (and other device that support PCAS statements)
        LOG_INFO("GNSS Factory Reset via PCAS10,3\n");
        _serial_gps->write("$PCAS10,3*1F\r\n");
        delay(100);
    } else if (gnssModel == GNSS_MODEL_ATGM336H) {
        LOG_INFO("Factory Reset via CAS-CFG-RST\n");
        uint8_t msglen = makeCASPacket(0x06, 0x02, sizeof(_message_CAS_CFG_RST_FACTORY), _message_CAS_CFG_RST_FACTORY);
        _serial_gps->write(UBXscratch, msglen);
        delay(100);
    } else {
        // fire this for good measure, if we have an L76B - won't harm other devices.
        _serial_gps->write("$PMTK104*37\r\n");
        // No PMTK_ACK for this command.
        delay(100);
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

#ifdef GNSS_AIROHA
    uint8_t fix = reader.fixQuality();
    uint32_t now = millis();
    if (fix > 0) {
        if (lastFixStartMsec > 0) {
            if ((now - lastFixStartMsec) < GPS_FIX_HOLD_TIME) {
                return false;
            } else {
                clearBuffer();
            }
        } else {
            lastFixStartMsec = now;
            return false;
        }
    } else {
        return false;
    }
#endif
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
#ifdef GNSS_AIROHA
    if ((config.position.gps_update_interval * 1000) >= (GPS_FIX_HOLD_TIME * 2)) {
        uint8_t fix = reader.fixQuality();
        uint32_t now = millis();
        if (fix > 0) {
            if (lastFixStartMsec > 0) {
                if ((now - lastFixStartMsec) < GPS_FIX_HOLD_TIME) {
                    return false;
                } else {
                    clearBuffer();
                }
            } else {
                lastFixStartMsec = now;
                return false;
            }
        } else {
            return false;
        }
    }
#endif
    // By default, TinyGPS++ does not parse GPGSA lines, which give us
    //   the 2D/3D fixType (see NMEAGPS.h)
    // At a minimum, use the fixQuality indicator in GPGGA (FIXME?)
    fixQual = reader.fixQuality();

#ifndef TINYGPS_OPTION_NO_STATISTICS
    if (reader.failedChecksum() > lastChecksumFailCount) {
        LOG_WARN("%u new GPS checksum failures, for a total of %u.\n", reader.failedChecksum() - lastChecksumFailCount,
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
    if (!reader.location.isUpdated() && !reader.altitude.isUpdated())
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
    p.timestamp = gm_mktime(&t);

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

bool GPS::whileActive()
{
    unsigned int charsInBuf = 0;
    bool isValid = false;
    if (powerState != GPS_ACTIVE) {
        clearBuffer();
        return false;
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
    // Clear the old scheduling info (reset the lock-time prediction)
    scheduling.reset();

    enabled = true;
    setInterval(GPS_THREAD_INTERVAL);

    scheduling.informSearching();
    setPowerState(GPS_ACTIVE);
}

int32_t GPS::disable()
{
    enabled = false;
    setInterval(INT32_MAX);
    setPowerState(GPS_OFF);

    return INT32_MAX;
}

void GPS::toggleGpsMode()
{
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
        LOG_INFO("User toggled GpsMode. Now DISABLED.\n");
#ifdef GNSS_AIROHA
        if (powerState == GPS_ACTIVE) {
            LOG_DEBUG("User power Off GPS\n");
            digitalWrite(PIN_GPS_EN, LOW);
        }
#endif
        disable();
    } else if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED) {
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
        LOG_INFO("User toggled GpsMode. Now ENABLED\n");
        enable();
    }
}
#endif // Exclude GPS