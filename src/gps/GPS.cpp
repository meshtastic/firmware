#include <cstring> // Include for strstr
#include <string>
#include <vector>

#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "Default.h"
#include "GPS.h"
#include "GpioLogic.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "RTC.h"
#include "Throttle.h"
#include "buzz.h"
#include "concurrency/Periodic.h"
#include "meshUtils.h"

#include "main.h" // pmu_found
#include "sleep.h"

#include "GPSUpdateScheduling.h"
#include "cas.h"
#include "ubx.h"

#ifdef ARCH_PORTDUINO
#include "PortduinoGlue.h"
#include "meshUtils.h"
#include <algorithm>
#include <ctime>
#endif

#ifndef GPS_RESET_MODE
#define GPS_RESET_MODE HIGH
#endif

// Not all platforms have std::size().
template <typename T, std::size_t N> std::size_t array_count(const T (&)[N])
{
    return N;
}

#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(ARCH_ESP32) || defined(ARCH_PORTDUINO) || defined(ARCH_STM32WL)
#if defined(GPS_SERIAL_PORT)
HardwareSerial *GPS::_serial_gps = &GPS_SERIAL_PORT;
#else
HardwareSerial *GPS::_serial_gps = &Serial1;
#endif
#elif defined(ARCH_RP2040)
SerialUART *GPS::_serial_gps = &Serial1;
#else
HardwareSerial *GPS::_serial_gps = nullptr;
#endif

GPS *gps = nullptr;

static GPSUpdateScheduling scheduling;

/// Multiple GPS instances might use the same serial port (in sequence), but we can
/// only init that port once.
static bool didSerialInit;

static struct uBloxGnssModelInfo {
    char swVersion[30];
    char hwVersion[10];
    uint8_t extensionNo;
    char extension[10][30];
    uint8_t protocol_version;
} ublox_info;

#define GPS_SOL_EXPIRY_MS 5000 // in millis. give 1 second time to combine different sentences. NMEA Frequency isn't higher anyway
#define NMEA_MSG_GXGSA "GNGSA" // GSA message (GPGSA, GNGSA etc)

// For logging
static const char *getGPSPowerStateString(GPSPowerState state)
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
        assert(false);  // Unhandled enum value..
        return "FALSE"; // to make new ESP-IDF happy
    }
}

#ifdef PIN_GPS_SWITCH
// If we have a hardware switch, define a periodic watcher outside of the GPS runOnce thread, since this can be sleeping
// idefinitely

int lastState = LOW;
bool firstrun = true;

static int32_t gpsSwitch()
{
    if (gps) {
        int currentState = digitalRead(PIN_GPS_SWITCH);

        // if the switch is set to zero, disable the GPS Thread
        if (firstrun)
            if (currentState == LOW)
                lastState = HIGH;

        if (currentState != lastState) {
            if (currentState == LOW) {
                config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
                if (!firstrun)
                    playGPSDisableBeep();
                gps->disable();
            } else {
                config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
                if (!firstrun)
                    playGPSEnableBeep();
                gps->enable();
            }
            lastState = currentState;
        }
        firstrun = false;
    }
    return 1000;
}

static concurrency::Periodic *gpsPeriodic;
#endif

static void UBXChecksum(uint8_t *message, size_t length)
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
static void CASChecksum(uint8_t *message, size_t length)
{
    uint32_t cksum = ((uint32_t)message[5] << 24); // Message ID
    cksum += ((uint32_t)message[4]) << 16;         // Class
    cksum += message[2];                           // Payload Len

    // Iterate over the payload as a series of uint32_t's and
    // accumulate the cksum
    for (size_t i = 0; i < (length - 10) / 4; i++) {
        uint32_t pl = 0;
        memcpy(&pl, (message + 6) + (i * sizeof(uint32_t)), sizeof(uint32_t)); // avoid pointer dereference
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
    LOG_DEBUG("CAS packet: ");
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
#ifdef GPS_DEBUG
    std::string debugmsg = "";
#endif
    while (millis() < startTimeout) {
        if (_serial_gps->available()) {
            b = _serial_gps->read();

#ifdef GPS_DEBUG
            debugmsg += vformat("%c", (b >= 32 && b <= 126) ? b : '.');
#endif
            buffer[bytesRead] = b;
            bytesRead++;
            if ((bytesRead == 767) || (b == '\r')) {
                if (strnstr((char *)buffer, message, bytesRead) != nullptr) {
#ifdef GPS_DEBUG
                    LOG_DEBUG("Found: %s", message); // Log the found message
#endif
                    return GNSS_RESPONSE_OK;
                } else {
                    bytesRead = 0;
#ifdef GPS_DEBUG
                    LOG_DEBUG(debugmsg.c_str());
#endif
                }
            }
        }
    }
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

    while (Throttle::isWithinTimespanMs(startTime, waitMillis)) {
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
                LOG_INFO("Got ACK for class %02X message %02X in %dms", class_id, msg_id, millis() - startTime);
#endif
                return GNSS_RESPONSE_OK;
            }

            // Check for an ACK-NACK for the specified class and message id
            if ((msg_cls == 0x05) && (msg_msg_id == 0x00) && payload_cls == class_id && payload_msg == msg_id) {
#ifdef GPS_DEBUG
                LOG_WARN("Got NACK for class %02X message %02X in %dms", class_id, msg_id, millis() - startTime);
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
#ifdef GPS_DEBUG
    std::string debugmsg = "";
#endif

    for (int j = 2; j < 6; j++) {
        buf[8] += buf[j];
        buf[9] += buf[8];
    }

    for (int j = 0; j < 2; j++) {
        buf[6 + j] = ackP[j];
        buf[8] += buf[6 + j];
        buf[9] += buf[8];
    }

    while (Throttle::isWithinTimespanMs(startTime, waitMillis)) {
        if (ack > 9) {
#ifdef GPS_DEBUG
            LOG_INFO("Got ACK for class %02X message %02X in %dms", class_id, msg_id, millis() - startTime);
#endif
            return GNSS_RESPONSE_OK; // ACK received
        }
        if (_serial_gps->available()) {
            b = _serial_gps->read();
            if (b == frame_errors[sCounter]) {
                sCounter++;
                if (sCounter == 26) {
#ifdef GPS_DEBUG

                    LOG_DEBUG(debugmsg.c_str());
#endif
                    return GNSS_RESPONSE_FRAME_ERRORS;
                }
            } else {
                sCounter = 0;
            }
#ifdef GPS_DEBUG
            debugmsg += vformat("%02X", b);
#endif
            if (b == buf[ack]) {
                ack++;
            } else {
                if (ack == 3 && b == 0x00) { // UBX-ACK-NAK message
#ifdef GPS_DEBUG
                    LOG_DEBUG(debugmsg.c_str());
#endif
                    LOG_WARN("Got NAK for class %02X message %02X", class_id, msg_id);
                    return GNSS_RESPONSE_NAK; // NAK received
                }
                ack = 0; // Reset the acknowledgement counter
            }
        }
    }
#ifdef GPS_DEBUG
    LOG_DEBUG(debugmsg.c_str());
    LOG_WARN("No response for class %02X message %02X", class_id, msg_id);
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
    uint16_t needRead = 0;

    while (Throttle::isWithinTimespanMs(startTime, waitMillis)) {
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
                    LOG_INFO("Got ACK for class %02X message %02X in %dms", requestedClass, requestedID, millis() - startTime);
#endif
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

#if GPS_BAUDRATE_FIXED
// if GPS_BAUDRATE is specified in variant, only try that.
static const int serialSpeeds[1] = {GPS_BAUDRATE};
static const int rareSerialSpeeds[1] = {GPS_BAUDRATE};
#else
static const int serialSpeeds[3] = {9600, 115200, 38400};
static const int rareSerialSpeeds[3] = {4800, 57600, GPS_BAUDRATE};
#endif

#ifndef GPS_PROBETRIES
#define GPS_PROBETRIES 2
#endif

/**
 * @brief  Setup the GPS based on the model detected.
 *  We detect the GPS by cycling through a set of baud rates, first common then rare.
 *  For each baud rate, we run GPS::Probe to send commands and match the responses
 *  to known GPS responses.
 * @retval Whether setup reached the end of its potential to configure the GPS.
 */
bool GPS::setup()
{
    if (!didSerialInit) {
        int msglen = 0;
        if (tx_gpio && gnssModel == GNSS_MODEL_UNKNOWN) {
#ifdef TRACKER_T1000_E
            // add power up/down strategy, improve ag3335 detection success
            digitalWrite(PIN_GPS_EN, LOW);
            delay(500);
            digitalWrite(GPS_VRTC_EN, LOW);
            delay(1000);
            digitalWrite(GPS_VRTC_EN, HIGH);
            delay(500);
            digitalWrite(PIN_GPS_EN, HIGH);
            delay(1000);
#endif
            if (probeTries < GPS_PROBETRIES) {
                LOG_DEBUG("Probe for GPS at %d", serialSpeeds[speedSelect]);
                gnssModel = probe(serialSpeeds[speedSelect]);
                if (gnssModel == GNSS_MODEL_UNKNOWN) {
                    if (++speedSelect == array_count(serialSpeeds)) {
                        speedSelect = 0;
                        ++probeTries;
                    }
                }
            }
            // Rare Serial Speeds
            if (probeTries == GPS_PROBETRIES) {
                LOG_DEBUG("Probe for GPS at %d", rareSerialSpeeds[speedSelect]);
                gnssModel = probe(rareSerialSpeeds[speedSelect]);
                if (gnssModel == GNSS_MODEL_UNKNOWN) {
                    if (++speedSelect == array_count(rareSerialSpeeds)) {
                        LOG_WARN("Give up on GPS probe and set to %d", GPS_BAUDRATE);
                        return true;
                    }
                }
            }
        }

        if (gnssModel != GNSS_MODEL_UNKNOWN) {
            setConnected();
        } else {
            return false;
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
        } else if (gnssModel == GNSS_MODEL_MTK_PA1010D) {
            // PA1010D is used in the Pimoroni GPS board.

            // Enable all constellations.
            _serial_gps->write("$PMTK353,1,1,1,1,1*2A\r\n");
            // Above command will reset the GPS and takes longer before it will accept new commands
            delay(1000);
            // Only ask for RMC and GGA (GNRMC and GNGGA)
            _serial_gps->write("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n");
            delay(250);
            // Enable SBAS / WAAS
            _serial_gps->write("$PMTK301,2*2E\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_MTK_PA1616S) {
            // PA1616S is used in some GPS breakout boards from Adafruit
            // PA1616S does not have GLONASS capability. PA1616D does, but is not implemented here.
            _serial_gps->write("$PMTK353,1,0,0,0,0*2A\r\n");
            // Above command will reset the GPS and takes longer before it will accept new commands
            delay(1000);
            // Only ask for RMC and GGA (GNRMC and GNGGA)
            _serial_gps->write("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n");
            delay(250);
            // Enable SBAS / WAAS
            _serial_gps->write("$PMTK301,2*2E\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_ATGM336H) {
            // Set the intial configuration of the device - these _should_ work for most AT6558 devices
            msglen = makeCASPacket(0x06, 0x07, sizeof(_message_CAS_CFG_NAVX_CONF), _message_CAS_CFG_NAVX_CONF);
            _serial_gps->write(UBXscratch, msglen);
            if (getACKCas(0x06, 0x07, 250) != GNSS_RESPONSE_OK) {
                LOG_WARN("ATGM336H: Could not set Config");
            }

            // Set the update frequence to 1Hz
            msglen = makeCASPacket(0x06, 0x04, sizeof(_message_CAS_CFG_RATE_1HZ), _message_CAS_CFG_RATE_1HZ);
            _serial_gps->write(UBXscratch, msglen);
            if (getACKCas(0x06, 0x04, 250) != GNSS_RESPONSE_OK) {
                LOG_WARN("ATGM336H: Could not set Update Frequency");
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
                    LOG_WARN("ATGM336H: Could not enable NMEA MSG: %d", fields[i]);
                }
            }
        } else if (gnssModel == GNSS_MODEL_UC6580) {
            // The Unicore UC6580 can use a lot of sat systems, enable it to
            // use GPS L1 & L5 + BDS B1I & B2a + GLONASS L1 + GALILEO E1 & E5a + SBAS + QZSS
            // This will reset the receiver, so wait a bit afterwards
            // The paranoid will wait for the OK*04 confirmation response after each command.
            _serial_gps->write("$CFGSYS,h35155\r\n");
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
        } else if (IS_ONE_OF(gnssModel, GNSS_MODEL_AG3335, GNSS_MODEL_AG3352)) {

            if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_IN ||
                config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_NP_865) {
                _serial_gps->write("$PAIR066,1,0,1,0,0,1*3B\r\n"); // Enable GPS+GALILEO+NAVIC
                // GPS GLONASS GALILEO BDS QZSS NAVIC
                //  1    0       1      0   0    1
            } else {
                _serial_gps->write("$PAIR066,1,1,1,1,0,0*3A\r\n"); // Enable GPS+GLONASS+GALILEO+BDS
                // GPS GLONASS GALILEO BDS QZSS NAVIC
                //  1    1       1      1   0    0
            }
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
        } else if (gnssModel == GNSS_MODEL_UBLOX6) {
            clearBuffer();
            SEND_UBX_PACKET(0x06, 0x02, _message_DISABLE_TXT_INFO, "disable text info messages", 500);
            SEND_UBX_PACKET(0x06, 0x39, _message_JAM_6_7, "enable interference resistance", 500);
            SEND_UBX_PACKET(0x06, 0x23, _message_NAVX5, "configure NAVX5 settings", 500);

            // Turn off unwanted NMEA messages, set update rate
            SEND_UBX_PACKET(0x06, 0x08, _message_1HZ, "set GPS update rate", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GLL, "disable NMEA GLL", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GSA, "enable NMEA GSA", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GSV, "disable NMEA GSV", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_VTG, "disable NMEA VTG", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_RMC, "enable NMEA RMC", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GGA, "enable NMEA GGA", 500);

            clearBuffer();
            SEND_UBX_PACKET(0x06, 0x11, _message_CFG_RXM_ECO, "enable powersave ECO mode for Neo-6", 500);
            SEND_UBX_PACKET(0x06, 0x3B, _message_CFG_PM2, "enable powersave details for GPS", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_AID, "disable UBX-AID", 500);

            msglen = makeUBXPacket(0x06, 0x09, sizeof(_message_SAVE), _message_SAVE);
            _serial_gps->write(UBXscratch, msglen);
            if (getACK(0x06, 0x09, 2000) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to save GNSS module config");
            } else {
                LOG_INFO("GNSS module config saved!");
            }
        } else if (IS_ONE_OF(gnssModel, GNSS_MODEL_UBLOX7, GNSS_MODEL_UBLOX8, GNSS_MODEL_UBLOX9)) {
            if (gnssModel == GNSS_MODEL_UBLOX7) {
                LOG_DEBUG("Set GPS+SBAS");
                msglen = makeUBXPacket(0x06, 0x3e, sizeof(_message_GNSS_7), _message_GNSS_7);
                _serial_gps->write(UBXscratch, msglen);
            } else { // 8,9
                msglen = makeUBXPacket(0x06, 0x3e, sizeof(_message_GNSS_8), _message_GNSS_8);
                _serial_gps->write(UBXscratch, msglen);
            }

            if (getACK(0x06, 0x3e, 800) == GNSS_RESPONSE_NAK) {
                // It's not critical if the module doesn't acknowledge this configuration.
                LOG_DEBUG("reconfigure GNSS - defaults maintained. Is this module GPS-only?");
            } else {
                if (gnssModel == GNSS_MODEL_UBLOX7) {
                    LOG_INFO("GPS+SBAS configured");
                } else { // 8,9
                    LOG_INFO("GPS+SBAS+GLONASS+Galileo configured");
                }
                // Documentation say, we need wait atleast 0.5s after reconfiguration of GNSS module, before sending next
                // commands for the M8 it tends to be more... 1 sec should be enough ;>)
                delay(1000);
            }

            // Disable Text Info messages //6,7,8,9
            clearBuffer();
            SEND_UBX_PACKET(0x06, 0x02, _message_DISABLE_TXT_INFO, "disable text info messages", 500);

            if (gnssModel == GNSS_MODEL_UBLOX8) { // 8
                clearBuffer();
                SEND_UBX_PACKET(0x06, 0x39, _message_JAM_8, "enable interference resistance", 500);

                clearBuffer();
                SEND_UBX_PACKET(0x06, 0x23, _message_NAVX5_8, "configure NAVX5_8 settings", 500);
            } else { // 6,7,9
                SEND_UBX_PACKET(0x06, 0x39, _message_JAM_6_7, "enable interference resistance", 500);
                SEND_UBX_PACKET(0x06, 0x23, _message_NAVX5, "configure NAVX5 settings", 500);
            }
            // Turn off unwanted NMEA messages, set update rate
            SEND_UBX_PACKET(0x06, 0x08, _message_1HZ, "set GPS update rate", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GLL, "disable NMEA GLL", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GSA, "enable NMEA GSA", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GSV, "disable NMEA GSV", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_VTG, "disable NMEA VTG", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_RMC, "enable NMEA RMC", 500);
            SEND_UBX_PACKET(0x06, 0x01, _message_GGA, "enable NMEA GGA", 500);

            if (ublox_info.protocol_version >= 18) {
                clearBuffer();
                SEND_UBX_PACKET(0x06, 0x86, _message_PMS, "enable powersave for GPS", 500);
                SEND_UBX_PACKET(0x06, 0x3B, _message_CFG_PM2, "enable powersave details for GPS", 500);

                // For M8 we want to enable NMEA vserion 4.10 so we can see the additional sats.
                if (gnssModel == GNSS_MODEL_UBLOX8) {
                    clearBuffer();
                    SEND_UBX_PACKET(0x06, 0x17, _message_NMEA, "enable NMEA 4.10", 500);
                }
            } else {
                SEND_UBX_PACKET(0x06, 0x11, _message_CFG_RXM_PSM, "enable powersave mode for GPS", 500);
                SEND_UBX_PACKET(0x06, 0x3B, _message_CFG_PM2, "enable powersave details for GPS", 500);
            }

            msglen = makeUBXPacket(0x06, 0x09, sizeof(_message_SAVE), _message_SAVE);
            _serial_gps->write(UBXscratch, msglen);
            if (getACK(0x06, 0x09, 2000) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to save GNSS module config");
            } else {
                LOG_INFO("GNSS module configuration saved!");
            }
        } else if (gnssModel == GNSS_MODEL_UBLOX10) {
            delay(1000);
            clearBuffer();
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_DISABLE_NMEA_RAM, "disable NMEA messages in M10 RAM", 300);
            delay(750);
            clearBuffer();
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_DISABLE_NMEA_BBR, "disable NMEA messages in M10 BBR", 300);
            delay(750);
            clearBuffer();
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_DISABLE_TXT_INFO_RAM, "disable Info messages for M10 GPS RAM", 300);
            delay(750);
            // Next disable Info txt messages in BBR layer
            clearBuffer();
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_DISABLE_TXT_INFO_BBR, "disable Info messages for M10 GPS BBR", 300);
            delay(750);
            // Do M10 configuration for Power Management.
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_PM_RAM, "enable powersave for M10 GPS RAM", 300);
            delay(750);
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_PM_BBR, "enable powersave for M10 GPS BBR", 300);
            delay(750);
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_ITFM_RAM, "enable jam detection M10 GPS RAM", 300);
            delay(750);
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_ITFM_BBR, "enable jam detection M10 GPS BBR", 300);
            delay(750);
            // Here is where the init commands should go to do further M10 initialization.
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_DISABLE_SBAS_RAM, "disable SBAS M10 GPS RAM", 300);
            delay(750); // will cause a receiver restart so wait a bit
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_DISABLE_SBAS_BBR, "disable SBAS M10 GPS BBR", 300);
            delay(750); // will cause a receiver restart so wait a bit

            // Done with initialization, Now enable wanted NMEA messages in BBR layer so they will survive a periodic
            // sleep.
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_ENABLE_NMEA_BBR, "enable messages for M10 GPS BBR", 300);
            delay(750);
            // Next enable wanted NMEA messages in RAM layer
            SEND_UBX_PACKET(0x06, 0x8A, _message_VALSET_ENABLE_NMEA_RAM, "enable messages for M10 GPS RAM", 500);
            delay(750);

            // As the M10 has no flash, the best we can do to preserve the config is to set it in RAM and BBR.
            // BBR will survive a restart, and power off for a while, but modules with small backup
            // batteries or super caps will not retain the config for a long power off time.
            msglen = makeUBXPacket(0x06, 0x09, sizeof(_message_SAVE_10), _message_SAVE_10);
            _serial_gps->write(UBXscratch, msglen);
            if (getACK(0x06, 0x09, 2000) != GNSS_RESPONSE_OK) {
                LOG_WARN("Unable to save GNSS module config");
            } else {
                LOG_INFO("GNSS module configuration saved!");
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
    LOG_INFO("GPS power state move from %s to %s", getGPSPowerStateString(oldState), getGPSPowerStateString(newState));

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
#ifdef GNSS_AIROHA
        lastFixStartMsec = 0;
#endif
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
    if ((HW_VENDOR == meshtastic_HardwareModel_RAK4631 || HW_VENDOR == meshtastic_HardwareModel_WISMESH_TAP) &&
        (rotaryEncoderInterruptImpl1 || upDownInterruptImpl1))
        return;

    // Write and log
    enablePin->set(on);
#ifdef GPS_DEBUG
    LOG_DEBUG("Pin EN %s", on == HIGH ? "HI" : "LOW");
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
#ifdef GPS_DEBUG
    LOG_DEBUG("Pin STANDBY %s", val == HIGH ? "HI" : "LOW");
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
#ifdef GPS_DEBUG
    LOG_DEBUG("PMU %s", on ? "on" : "off");
#endif
#endif
}

// Set UBLOX power, if relevant
void GPS::setPowerUBLOX(bool on, uint32_t sleepMs)
{
    // Abort: if not UBLOX hardware
    if (!IS_ONE_OF(gnssModel, GNSS_MODEL_UBLOX6, GNSS_MODEL_UBLOX7, GNSS_MODEL_UBLOX8, GNSS_MODEL_UBLOX9, GNSS_MODEL_UBLOX10))
        return;

    // If waking
    if (on) {
        gps->_serial_gps->write(0xFF);
        clearBuffer(); // This often returns old data, so drop it
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
        if (gnssModel != GNSS_MODEL_UBLOX10) {
            // Encode the sleep time in millis into the packet
            for (int i = 0; i < 4; i++)
                _message_PMREQ[0 + i] = sleepMs >> (i * 8);

            // Record the message length
            msglen = gps->makeUBXPacket(0x02, 0x41, sizeof(_message_PMREQ), _message_PMREQ);
        } else {
            // Encode the sleep time in millis into the packet
            for (int i = 0; i < 4; i++)
                _message_PMREQ_10[4 + i] = sleepMs >> (i * 8);

            // Record the message length
            msglen = gps->makeUBXPacket(0x02, 0x41, sizeof(_message_PMREQ_10), _message_PMREQ_10);
        }

        // Send the UBX packet
        gps->_serial_gps->write(gps->UBXscratch, msglen);
#ifdef GPS_DEBUG
        LOG_DEBUG("UBLOX: sleep for %dmS", sleepMs);
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

    LOG_DEBUG("%us until next search", sleepTime / 1000);

    // If update interval less than 10 seconds, no attempt to sleep
    if (updateInterval <= 10 * 1000UL || sleepTime == 0)
        setPowerState(GPS_IDLE);

    else {
// Check whether the GPS hardware is capable of GPS_SOFTSLEEP
// If not, fallback to GPS_HARDSLEEP instead
#ifdef PIN_GPS_STANDBY // L76B, L76K and clones have a standby pin
        bool softsleepSupported = true;
#else
        bool softsleepSupported = false;
#endif
        // U-blox is supported via PMREQ
        if (IS_ONE_OF(gnssModel, GNSS_MODEL_UBLOX6, GNSS_MODEL_UBLOX7, GNSS_MODEL_UBLOX8, GNSS_MODEL_UBLOX9, GNSS_MODEL_UBLOX10))
            softsleepSupported = true;

        if (softsleepSupported) {
            // How long does gps_update_interval need to be, for GPS_HARDSLEEP to become more efficient than
            // GPS_SOFTSLEEP? Heuristic equation. A compromise manually fitted to power observations from U-blox NEO-6M
            // and M10050 https://www.desmos.com/calculator/6gvjghoumr This is not particularly accurate, but probably an
            // improvement over a single, fixed threshold
            uint32_t hardsleepThreshold = (2750 * pow(predictedSearchDuration / 1000, 1.22));
            LOG_DEBUG("gps_update_interval >= %us needed to justify hardsleep", hardsleepThreshold / 1000);

            // If update interval too short: softsleep (if supported by hardware)
            if (updateInterval < hardsleepThreshold) {
                setPowerState(GPS_SOFTSLEEP, sleepTime);
                return;
            }
        }
        // If update interval long enough (or softsleep unsupported): hardsleep instead
        setPowerState(GPS_HARDSLEEP, sleepTime);
    }
}

void GPS::publishUpdate()
{
    if (shouldPublish) {
        shouldPublish = false;

        // In debug logs, identify position by @timestamp:stage (stage 2 = publish)
        LOG_DEBUG("Publish pos@%x:2, hasVal=%d, Sats=%d, GPSlock=%d", p.timestamp, hasValidLocation, p.sats_in_view, hasLock());

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
            LOG_INFO("GPS set to not-present. Skip probe");
            return disable();
        }
        if (!setup())
            return 2000; // Setup failed, re-run in two seconds

        // We have now loaded our saved preferences from flash
        if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
            return disable();
        }
        GPSInitFinished = true;
        publishUpdate();
    }

    // Repeaters have no need for GPS
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        return disable();
    }

    if (whileActive()) {
        // if we have received valid NMEA claim we are connected
        setConnected();
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
        LOG_DEBUG("hasValidLocation RISING EDGE");
        hasValidLocation = true;
        shouldPublish = true;
    }

    bool tooLong = scheduling.searchedTooLong();
    if (tooLong)
        LOG_WARN("Couldn't publish a valid location: didn't get a GPS lock in time");

    // Once we get a location we no longer desperately want an update
    if ((gotLoc && gotTime) || tooLong) {

        if (tooLong) {
            // we didn't get a location during this ack window, therefore declare loss of lock
            if (hasValidLocation) {
                LOG_DEBUG("hasValidLocation FALLING EDGE");
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

// clear the GPS rx/tx buffer as quickly as possible
void GPS::clearBuffer()
{
#ifdef ARCH_ESP32
    _serial_gps->flush(false);
#else
    int x = _serial_gps->available();
    while (x--)
        _serial_gps->read();
#endif
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareDeepSleep(void *unused)
{
    LOG_INFO("GPS deep sleep!");
    disable();
    return 0;
}

static const char *PROBE_MESSAGE = "Trying %s (%s)...";
static const char *DETECTED_MESSAGE = "%s detected";

#define PROBE_SIMPLE(CHIP, TOWRITE, RESPONSE, DRIVER, TIMEOUT, ...)                                                              \
    do {                                                                                                                         \
        LOG_DEBUG(PROBE_MESSAGE, TOWRITE, CHIP);                                                                                 \
        clearBuffer();                                                                                                           \
        _serial_gps->write(TOWRITE "\r\n");                                                                                      \
        if (getACK(RESPONSE, TIMEOUT) == GNSS_RESPONSE_OK) {                                                                     \
            LOG_INFO(DETECTED_MESSAGE, CHIP);                                                                                    \
            return DRIVER;                                                                                                       \
        }                                                                                                                        \
    } while (0)

#define PROBE_FAMILY(FAMILY_NAME, COMMAND, RESPONSE_MAP, TIMEOUT)                                                                \
    do {                                                                                                                         \
        LOG_DEBUG(PROBE_MESSAGE, COMMAND, FAMILY_NAME);                                                                          \
        clearBuffer();                                                                                                           \
        _serial_gps->write(COMMAND "\r\n");                                                                                      \
        GnssModel_t detectedDriver = getProbeResponse(TIMEOUT, RESPONSE_MAP);                                                    \
        if (detectedDriver != GNSS_MODEL_UNKNOWN) {                                                                              \
            return detectedDriver;                                                                                               \
        }                                                                                                                        \
    } while (0)

GnssModel_t GPS::probe(int serialSpeed)
{
#if defined(ARCH_NRF52) || defined(ARCH_PORTDUINO) || defined(ARCH_STM32WL)
    _serial_gps->end();
    _serial_gps->begin(serialSpeed);
#elif defined(ARCH_RP2040)
    _serial_gps->end();
    _serial_gps->setFIFOSize(256);
    _serial_gps->begin(serialSpeed);
#else
    if (_serial_gps->baudRate() != serialSpeed) {
        LOG_DEBUG("Set Baud to %i", serialSpeed);
        _serial_gps->updateBaudRate(serialSpeed);
    }
#endif

    memset(&ublox_info, 0, sizeof(ublox_info));
    uint8_t buffer[768] = {0};
    delay(100);

    // Close all NMEA sentences, valid for L76K, ATGM336H (and likely other AT6558 devices)
    _serial_gps->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(20);
    // Close NMEA sequences on Ublox
    _serial_gps->write("$PUBX,40,GLL,0,0,0,0,0,0*5C\r\n");
    _serial_gps->write("$PUBX,40,GSV,0,0,0,0,0,0*59\r\n");
    _serial_gps->write("$PUBX,40,VTG,0,0,0,0,0,0*5E\r\n");
    delay(20);

    // Unicore UFirebirdII Series: UC6580, UM620, UM621, UM670A, UM680A, or UM681A
    std::vector<ChipInfo> unicore = {{"UC6580", "UC6580", GNSS_MODEL_UC6580}, {"UM600", "UM600", GNSS_MODEL_UC6580}};
    PROBE_FAMILY("Unicore Family", "$PDTINFO", unicore, 500);

    std::vector<ChipInfo> atgm = {
        {"ATGM336H", "$GPTXT,01,01,02,HW=ATGM336H", GNSS_MODEL_ATGM336H},
        /* ATGM332D series (-11(GPS), -21(BDS), -31(GPS+BDS), -51(GPS+GLONASS), -71-0(GPS+BDS+GLONASS)) based on AT6558 */
        {"ATGM332D", "$GPTXT,01,01,02,HW=ATGM332D", GNSS_MODEL_ATGM336H}};
    PROBE_FAMILY("ATGM33xx Family", "$PCAS06,1*1A", atgm, 500);

    /* Airoha (Mediatek) AG3335A/M/S, A3352Q, Quectel L89 2.0, SimCom SIM65M */
    _serial_gps->write("$PAIR062,2,0*3C\r\n"); // GSA OFF to reduce volume
    _serial_gps->write("$PAIR062,3,0*3D\r\n"); // GSV OFF to reduce volume
    _serial_gps->write("$PAIR513*3D\r\n");     // save configuration
    std::vector<ChipInfo> airoha = {{"AG3335", "$PAIR021,AG3335", GNSS_MODEL_AG3335},
                                    {"AG3352", "$PAIR021,AG3352", GNSS_MODEL_AG3352},
                                    {"RYS3520", "$PAIR021,REYAX_RYS3520_V2", GNSS_MODEL_AG3352}};
    PROBE_FAMILY("Airoha Family", "$PAIR021*39", airoha, 1000);

    PROBE_SIMPLE("LC86", "$PQTMVERNO*58", "$PQTMVERNO,LC86", GNSS_MODEL_AG3352, 500);
    PROBE_SIMPLE("L76K", "$PCAS06,0*1B", "$GPTXT,01,01,02,SW=", GNSS_MODEL_MTK, 500);

    // Close all NMEA sentences, valid for MTK3333 and MTK3339 platforms
    _serial_gps->write("$PMTK514,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2E\r\n");
    delay(20);
    std::vector<ChipInfo> mtk = {{"L76B", "Quectel-L76B", GNSS_MODEL_MTK_L76B}, {"PA1010D", "1010D", GNSS_MODEL_MTK_PA1010D},
                                 {"PA1616S", "1616S", GNSS_MODEL_MTK_PA1616S},  {"LS20031", "MC-1513", GNSS_MODEL_MTK_L76B},
                                 {"L96", "Quectel-L96", GNSS_MODEL_MTK_L76B},   {"L80-R", "_3337_", GNSS_MODEL_MTK_L76B},
                                 {"L80", "_3339_", GNSS_MODEL_MTK_L76B}};

    PROBE_FAMILY("MTK Family", "$PMTK605*31", mtk, 500);

    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x00, 0x00};
    UBXChecksum(cfg_rate, sizeof(cfg_rate));
    clearBuffer();
    _serial_gps->write(cfg_rate, sizeof(cfg_rate));
    // Check that the returned response class and message ID are correct
    GPS_RESPONSE response = getACK(0x06, 0x08, 750);
    if (response == GNSS_RESPONSE_NONE) {
        LOG_WARN("No GNSS Module (baudrate %d)", serialSpeed);
        return GNSS_MODEL_UNKNOWN;
    } else if (response == GNSS_RESPONSE_FRAME_ERRORS) {
        LOG_INFO("UBlox Frame Errors (baudrate %d)", serialSpeed);
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
        uint16_t position = 0;
        for (int i = 0; i < 30; i++) {
            ublox_info.swVersion[i] = buffer[position];
            position++;
        }
        for (int i = 0; i < 10; i++) {
            ublox_info.hwVersion[i] = buffer[position];
            position++;
        }

        while (len >= position + 30) {
            for (int i = 0; i < 30; i++) {
                ublox_info.extension[ublox_info.extensionNo][i] = buffer[position];
                position++;
            }
            ublox_info.extensionNo++;
            if (ublox_info.extensionNo > 9)
                break;
        }

        LOG_DEBUG("Module Info : ");
        LOG_DEBUG("Soft version: %s", ublox_info.swVersion);
        LOG_DEBUG("Hard version: %s", ublox_info.hwVersion);
        LOG_DEBUG("Extensions:%d", ublox_info.extensionNo);
        for (int i = 0; i < ublox_info.extensionNo; i++) {
            LOG_DEBUG("  %s", ublox_info.extension[i]);
        }

        memset(buffer, 0, sizeof(buffer));

        // tips: extensionNo field is 0 on some 6M GNSS modules
        for (int i = 0; i < ublox_info.extensionNo; ++i) {
            if (!strncmp(ublox_info.extension[i], "MOD=", 4)) {
                strncpy((char *)buffer, &(ublox_info.extension[i][4]), sizeof(buffer));
            } else if (!strncmp(ublox_info.extension[i], "PROTVER", 7)) {
                char *ptr = nullptr;
                memset(buffer, 0, sizeof(buffer));
                strncpy((char *)buffer, &(ublox_info.extension[i][8]), sizeof(buffer));
                LOG_DEBUG("Protocol Version:%s", (char *)buffer);
                if (strlen((char *)buffer)) {
                    ublox_info.protocol_version = strtoul((char *)buffer, &ptr, 10);
                    LOG_DEBUG("ProtVer=%d", ublox_info.protocol_version);
                } else {
                    ublox_info.protocol_version = 0;
                }
            }
        }
        if (strncmp(ublox_info.hwVersion, "00040007", 8) == 0) {
            LOG_INFO(DETECTED_MESSAGE, "U-blox 6", "6");
            return GNSS_MODEL_UBLOX6;
        } else if (strncmp(ublox_info.hwVersion, "00070000", 8) == 0) {
            LOG_INFO(DETECTED_MESSAGE, "U-blox 7", "7");
            return GNSS_MODEL_UBLOX7;
        } else if (strncmp(ublox_info.hwVersion, "00080000", 8) == 0) {
            LOG_INFO(DETECTED_MESSAGE, "U-blox 8", "8");
            return GNSS_MODEL_UBLOX8;
        } else if (strncmp(ublox_info.hwVersion, "00190000", 8) == 0) {
            LOG_INFO(DETECTED_MESSAGE, "U-blox 9", "9");
            return GNSS_MODEL_UBLOX9;
        } else if (strncmp(ublox_info.hwVersion, "000A0000", 8) == 0) {
            LOG_INFO(DETECTED_MESSAGE, "U-blox 10", "10");
            return GNSS_MODEL_UBLOX10;
        }
    }
    LOG_WARN("No GNSS Module (baudrate %d)", serialSpeed);
    return GNSS_MODEL_UNKNOWN;
}

GnssModel_t GPS::getProbeResponse(unsigned long timeout, const std::vector<ChipInfo> &responseMap)
{
    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        if (_serial_gps->available()) {
            response += (char)_serial_gps->read();

            if (response.endsWith(",") || response.endsWith("\r\n")) {
#ifdef GPS_DEBUG
                LOG_DEBUG(response.c_str());
#endif
                // check if we can see our chips
                for (const auto &chipInfo : responseMap) {
                    if (strstr(response.c_str(), chipInfo.detectionString.c_str()) != nullptr) {
                        LOG_INFO("%s detected", chipInfo.chipName.c_str());
                        return chipInfo.driver;
                    }
                }
            }
            if (response.endsWith("\r\n")) {
                response.trim();
                response = ""; // Reset the response string for the next potential message
            }
        }
    }
#ifdef GPS_DEBUG
    LOG_DEBUG(response.c_str());
#endif
    return GNSS_MODEL_UNKNOWN; // Return empty string on timeout
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
                virtPin,
                p); // We just leave this created object on the heap so it can stay watching virtPin and driving en_gpio
        } else {
            new GpioUnaryTransformer(
                virtPin,
                p); // We just leave this created object on the heap so it can stay watching virtPin and driving en_gpio
        }
    }

#ifdef PIN_GPS_PPS
    // pulse per second
    pinMode(PIN_GPS_PPS, INPUT);
#endif

#ifdef PIN_GPS_SWITCH
    // toggle GPS via external GPIO switch
    pinMode(PIN_GPS_SWITCH, INPUT);
    gpsPeriodic = new concurrency::Periodic("GPSSwitch", gpsSwitch);
#endif

// Currently disabled per issue #525 (TinyGPS++ crash bug)
// when fixed upstream, can be un-disabled to enable 3D FixType and PDOP
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // see NMEAGPS.h
    gsafixtype.begin(reader, NMEA_MSG_GXGSA, 2);
    gsapdop.begin(reader, NMEA_MSG_GXGSA, 15);
    LOG_DEBUG("Use " NMEA_MSG_GXGSA " for 3DFIX and PDOP");
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
        LOG_DEBUG("Use GPIO%d for GPS RX", new_gps->rx_gpio);
        LOG_DEBUG("Use GPIO%d for GPS TX", new_gps->tx_gpio);
        _serial_gps->begin(GPS_BAUDRATE, SERIAL_8N1, new_gps->rx_gpio, new_gps->tx_gpio);
#elif defined(ARCH_RP2040)
        _serial_gps->setFIFOSize(256);
        _serial_gps->begin(GPS_BAUDRATE);
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
    if (fix > 0) {
        if (lastFixStartMsec > 0) {
            if (Throttle::isWithinTimespanMs(lastFixStartMsec, GPS_FIX_HOLD_TIME)) {
                return false;
            } else {
                clearBuffer();
            }
        } else {
            lastFixStartMsec = millis();
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
The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1,
1970 (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
*/
        struct tm t;
        t.tm_sec = ti.second() + round(ti.age() / 1000);
        t.tm_min = ti.minute();
        t.tm_hour = ti.hour();
        t.tm_mday = d.day();
        t.tm_mon = d.month() - 1;
        t.tm_year = d.year() - 1900;
        t.tm_isdst = false;
        if (t.tm_mon > -1) {
            LOG_DEBUG("NMEA GPS time %02d-%02d-%02d %02d:%02d:%02d age %d", d.year(), d.month(), t.tm_mday, t.tm_hour, t.tm_min,
                      t.tm_sec, ti.age());
            if (perhapsSetRTC(RTCQualityGPS, t) == RTCSetResultInvalidTime) {
                // Clear the GPS buffer if we got an invalid time
                clearBuffer();
            }
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
        if (fix > 0) {
            if (lastFixStartMsec > 0) {
                if (Throttle::isWithinTimespanMs(lastFixStartMsec, GPS_FIX_HOLD_TIME)) {
                    return false;
                } else {
                    clearBuffer();
                }
            } else {
                lastFixStartMsec = millis();
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
        LOG_WARN("%u new GPS checksum failures, for a total of %u", reader.failedChecksum() - lastChecksumFailCount,
                 reader.failedChecksum());
        lastChecksumFailCount = reader.failedChecksum();
    }
#endif

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    fixType = atoi(gsafixtype.value()); // will set to zero if no data
#endif

    // check if GPS has an acceptable lock
    if (!hasLock())
        return false;

#ifdef GPS_DEBUG
    LOG_DEBUG("AGE: LOC=%d FIX=%d DATE=%d TIME=%d", reader.location.age(),
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
              gsafixtype.age(),
#else
              0,
#endif
              reader.date.age(), reader.time.age());
#endif // GPS_DEBUG

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
        LOG_WARN("SOME data is TOO OLD: LOC %u, TIME %u, DATE %u", reader.location.age(), reader.time.age(), reader.date.age());
        return false;
    }

    // We know the solution is fresh and valid, so just read the data
    auto loc = reader.location.value();

    // Bail out EARLY to avoid overwriting previous good data (like #857)
    if (toDegInt(loc.lat) > 900000000) {
#ifdef GPS_DEBUG
        LOG_DEBUG("Bail out EARLY on LAT %i", toDegInt(loc.lat));
#endif
        return false;
    }
    if (toDegInt(loc.lng) > 1800000000) {
#ifdef GPS_DEBUG
        LOG_DEBUG("Bail out EARLY on LNG %i", toDegInt(loc.lng));
#endif
        return false;
    }

    p.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

    // Dilution of precision (an accuracy metric) is reported in 10^2 units, so we need to scale down when we use it
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    p.HDOP = reader.hdop.value();
    p.PDOP = TinyGPSPlus::parseDecimal(gsapdop.value());
#else
    // FIXME! naive PDOP emulation (assumes VDOP==HDOP)
    // correct formula is PDOP = SQRT(HDOP^2 + VDOP^2)
    p.HDOP = reader.hdop.value();
    p.PDOP = 1.41 * reader.hdop.value();
#endif

    // Discard incomplete or erroneous readings
    if (reader.hdop.value() == 0) {
        LOG_WARN("BOGUS hdop.value() REJECTED: %d", reader.hdop.value());
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
            LOG_WARN("BOGUS course.value() REJECTED: %d", reader.course.value());
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
#ifdef GPS_DEBUG
    std::string debugmsg = "";
#endif
    if (powerState != GPS_ACTIVE) {
        clearBuffer();
        return false;
    }
#ifdef SERIAL_BUFFER_SIZE
    if (_serial_gps->available() >= SERIAL_BUFFER_SIZE - 1) {
        LOG_WARN("GPS Buffer full with %u bytes waiting. Flush to avoid corruption", _serial_gps->available());
        clearBuffer();
    }
#endif
    // First consume any chars that have piled up at the receiver
    while (_serial_gps->available() > 0) {
        int c = _serial_gps->read();
        UBXscratch[charsInBuf] = c;
#ifdef GPS_DEBUG
        debugmsg += vformat("%c", (c >= 32 && c <= 126) ? c : '.');
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
#ifdef GPS_DEBUG
    if (debugmsg != "") {
        LOG_DEBUG(debugmsg.c_str());
    }
#endif
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
        LOG_INFO("User toggled GpsMode. Now DISABLED");
        playGPSDisableBeep();
#ifdef GNSS_AIROHA
        if (powerState == GPS_ACTIVE) {
            LOG_DEBUG("User power Off GPS");
            digitalWrite(PIN_GPS_EN, LOW);
        }
#endif
        disable();
    } else if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED) {
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
        LOG_INFO("User toggled GpsMode. Now ENABLED");
        playGPSEnableBeep();
        enable();
    }
}
#endif // Exclude GPS
