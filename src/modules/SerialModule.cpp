#include "SerialModule.h"
#include "Channels.h"
#include "GeoCoord.h"
#include "MeshService.h"
#include "MeshTypes.h"
#include "NMEAWPL.h"
#include "NodeDB.h"
#include "RTC.h"
#include "RedirectablePrint.h"
#include "Router.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <Arduino.h>
#include <Throttle.h>

/*
    SerialModule
        A simple interface to send messages over the mesh network by sending strings
        over a serial port.

        There are no PIN defaults, you have to enable the second serial port yourself.

    Need help with this module? Post your question on the Meshtastic Discourse:
       https://meshtastic.discourse.group

    Basic Usage:

        1) Enable the module by setting enabled to 1.
        2) Set the pins (rxd / rxd) for your preferred RX and TX GPIO pins.
           On tbeam, recommend to use:
                RXD 35
                TXD 15
        3) Set timeout to the amount of time to wait before we consider
           your packet as "done".
        4) not applicable any more
        5) Connect to your device over the serial interface at 38400 8N1.
        6) Send a packet up to 240 bytes in length. This will get relayed over the mesh network.
        7) (Optional) Set echo to 1 and any message you send out will be echoed back
           to your device.

    TODO (in this order):
        * Define a verbose RX mode to report on mesh and packet information.
            - This won't happen any time soon.

    KNOWN PROBLEMS
        * Until the module is initialized by the startup sequence, the TX pin is in a floating
          state. Device connected to that pin may see this as "noise".
        * Will not work on Linux device targets.


*/
#ifdef HELTEC_MESH_SOLAR
#include "meshSolarApp.h"
#endif

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040) || defined(ARCH_STM32WL)) &&                             \
    !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)

#define RX_BUFFER 256
#define TIMEOUT 250
#define BAUD 38400
#define ACK 1

// API: Defaulting to the formerly removed phone_timeout_secs value of 15 minutes
#define SERIAL_CONNECTION_TIMEOUT (15 * 60) * 1000UL

SerialModule *serialModule;
SerialModuleRadio *serialModuleRadio;

#ifndef SERIAL_PRINT_PORT
#define SERIAL_PRINT_PORT 2
#endif

#if SERIAL_PRINT_PORT == 0
#define SERIAL_PRINT_OBJECT Serial
#elif SERIAL_PRINT_PORT == 1
#define SERIAL_PRINT_OBJECT Serial1
#elif SERIAL_PRINT_PORT == 2
#define SERIAL_PRINT_OBJECT Serial2
#else
#error "Unsupported SERIAL_PRINT_PORT value. Allowed values are 0, 1, or 2."
#endif

SerialModule::SerialModule() : StreamAPI(&SERIAL_PRINT_OBJECT), concurrency::OSThread("Serial")
{
    api_type = TYPE_SERIAL;
}
static Print *serialPrint = &SERIAL_PRINT_OBJECT;

char serialBytes[512];
size_t serialPayloadSize;

bool SerialModule::isValidConfig(const meshtastic_ModuleConfig_SerialConfig &config)
{
    if (config.override_console_serial_port && !IS_ONE_OF(config.mode, meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA,
                                                          meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO,
                                                          meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MS_CONFIG,
                                                          meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOG,
                                                          meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOGTEXT)) {
        const char *warning = "Invalid Serial config: override console serial port is only supported in NMEA, CalTopo, "
                              "MS_CONFIG, LOG, and LOGTEXT output-only modes.";
        LOG_ERROR(warning);
#if !IS_RUNNING_TESTS
        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        cn->level = meshtastic_LogRecord_Level_ERROR;
        cn->time = getValidTime(RTCQualityFromNet);
        snprintf(cn->message, sizeof(cn->message), "%s", warning);
        service->sendClientNotification(cn);
#endif
        return false;
    }

    return true;
}

SerialModuleRadio::SerialModuleRadio() : MeshModule("SerialModuleRadio")
{
    switch (moduleConfig.serial.mode) {
    case meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG:
        ourPortNum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        break;
    case meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOGTEXT:
        // For text-only log mode, observe text messages
        if (textMessageModule) {
            observe(textMessageModule);
        }
        ourPortNum = meshtastic_PortNum_SERIAL_APP;
        break;
    case meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA:
    case meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO:
        ourPortNum = meshtastic_PortNum_POSITION_APP;
        break;
    default:
        ourPortNum = meshtastic_PortNum_SERIAL_APP;
        // restrict to the serial channel for rx
        boundChannel = Channels::serialChannel;
        break;
    }
}

/**
 * @brief Checks if the serial connection is established.
 *
 * @return true if the serial connection is established, false otherwise.
 *
 * For the serial2 port we can't really detect if any client is on the other side, so instead just look for recent messages
 */
bool SerialModule::checkIsConnected()
{
    return Throttle::isWithinTimespanMs(lastContactMsec, SERIAL_CONNECTION_TIMEOUT);
}

int32_t SerialModule::runOnce()
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.serial.enabled = true;
    // moduleConfig.serial.rxd = 35;
    // moduleConfig.serial.txd = 15;
    // moduleConfig.serial.override_console_serial_port = true;
    // moduleConfig.serial.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO;
    // moduleConfig.serial.timeout = 1000;
    // moduleConfig.serial.echo = 1;

    if (!moduleConfig.serial.enabled)
        return disable();

    if (moduleConfig.serial.override_console_serial_port || (moduleConfig.serial.rxd && moduleConfig.serial.txd)) {
        if (firstTime) {
            // Interface with the serial peripheral from in here.
            LOG_INFO("Init serial peripheral interface");

            uint32_t baud = getBaudRate();

            if (moduleConfig.serial.override_console_serial_port) {
#ifdef RP2040_SLOW_CLOCK
                Serial2.flush();
                serialPrint = &Serial2;
#else
                Serial.flush();
                serialPrint = &Serial;
#endif
                // Give it a chance to flush out 💩
                delay(10);
            }
#if defined(CONFIG_IDF_TARGET_ESP32C6)
            if (moduleConfig.serial.rxd && moduleConfig.serial.txd) {
                Serial1.setRxBufferSize(RX_BUFFER);
                Serial1.begin(baud, SERIAL_8N1, moduleConfig.serial.rxd, moduleConfig.serial.txd);
            } else {
                Serial.begin(baud);
                Serial.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
            }
#elif defined(ARCH_STM32WL)
#ifndef RAK3172
            HardwareSerial *serialInstance = &Serial2;
#else
            HardwareSerial *serialInstance = &Serial1;
#endif
            if (moduleConfig.serial.rxd && moduleConfig.serial.txd) {
                serialInstance->setTx(moduleConfig.serial.txd);
                serialInstance->setRx(moduleConfig.serial.rxd);
            }
            serialInstance->begin(baud);
            serialInstance->setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
#elif defined(ARCH_ESP32)

            if (moduleConfig.serial.rxd && moduleConfig.serial.txd) {
                Serial2.setRxBufferSize(RX_BUFFER);
                Serial2.begin(baud, SERIAL_8N1, moduleConfig.serial.rxd, moduleConfig.serial.txd);
            } else {
                Serial.begin(baud);
                Serial.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
            }
#elif SERIAL_PRINT_PORT != 0

            if (moduleConfig.serial.rxd && moduleConfig.serial.txd) {
#ifdef ARCH_RP2040
                Serial2.setFIFOSize(RX_BUFFER);
                Serial2.setPinout(moduleConfig.serial.txd, moduleConfig.serial.rxd);
#else
                Serial2.setPins(moduleConfig.serial.rxd, moduleConfig.serial.txd);
#endif
                Serial2.begin(baud, SERIAL_8N1);
                Serial2.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
            } else {
#ifdef RP2040_SLOW_CLOCK
                Serial2.begin(baud, SERIAL_8N1);
                Serial2.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
#else
                Serial.begin(baud, SERIAL_8N1);
                Serial.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
#endif
            }
#else
            Serial.begin(baud, SERIAL_8N1);
            Serial.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
#endif
            serialModuleRadio = new SerialModuleRadio();

            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOG) {
                RedirectablePrint::uartLogDestination = serialPrint;
                LOG_INFO("SerialModule: Packet Log Mode (LOG) enabled");
                serialPrint->printf("\n=== Meshtastic Packet Log Mode (LOG) ===\n");
                serialPrint->printf("Packet logs will be logged to UART\n");
                serialPrint->printf("Time: %u seconds since boot\n\n", millis() / 1000);
            } else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOGTEXT) {
                RedirectablePrint::uartLogDestination = nullptr;
                LOG_INFO("SerialModule: Text-Only Log Mode (LOGTEXT) enabled");
                serialPrint->printf("\n=== Meshtastic Text-Only Log Mode (LOGTEXT) ===\n");
                serialPrint->printf("Only text messages with metadata will be logged to UART\n");
                serialPrint->printf("Format: [HH:MM:SS] FROM:0xXXXX (name) TO:BROADCAST/DM CH:channelname (index) MSG:message\n");
                serialPrint->printf("Time: %u seconds since boot\n\n", millis() / 1000);
            } else {
                RedirectablePrint::uartLogDestination = nullptr;
            }

            firstTime = 0;

            // in API mode send rebooted sequence
            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO) {
                emitRebooted();
            }
        } else {
            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO) {
                return runOncePart();
            } else if ((moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA) && HAS_GPS) {
                // in NMEA mode send out GGA every 2 seconds, Don't read from Port
                if (!Throttle::isWithinTimespanMs(lastNmeaTime, 2000)) {
                    lastNmeaTime = millis();
                    printGGA(outbuf, sizeof(outbuf), localPosition);
                    serialPrint->printf("%s", outbuf);
                }
            } else if ((moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO) && HAS_GPS) {
                if (!Throttle::isWithinTimespanMs(lastNmeaTime, 10000)) {
                    lastNmeaTime = millis();
                    uint32_t readIndex = 0;
                    const meshtastic_NodeInfoLite *tempNodeInfo = nodeDB->readNextMeshNode(readIndex);
                    while (tempNodeInfo != NULL) {
                        if (tempNodeInfo->has_user && nodeDB->hasValidPosition(tempNodeInfo)) {
                            printWPL(outbuf, sizeof(outbuf), tempNodeInfo->position, tempNodeInfo->user.long_name, true);
                            serialPrint->printf("%s", outbuf);
                        }
                        tempNodeInfo = nodeDB->readNextMeshNode(readIndex);
                    }
                }
            } else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOG ||
                       moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOGTEXT) {
                // These are output-only modes, no input processing needed
            }

#if SERIAL_PRINT_PORT != 0
            else if ((moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_WS85)) {
                processWXSerial();

            }
#if defined(HELTEC_MESH_SOLAR)
            else if ((moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MS_CONFIG)) {
                serialPayloadSize = Serial.readBytes(serialBytes, sizeof(serialBytes) - 1);
                // If the parsing fails, the following parsing will be performed.
                if ((serialPayloadSize > 0) && (meshSolarCmdHandle(serialBytes) != 0)) {
                    return runOncePart(serialBytes, serialPayloadSize);
                }
            }
#endif
            else {
#if defined(CONFIG_IDF_TARGET_ESP32C6)
                while (Serial1.available()) {
                    serialPayloadSize = Serial1.readBytes(serialBytes, meshtastic_Constants_DATA_PAYLOAD_LEN);
#else
#ifndef RAK3172
                HardwareSerial *serialInstance = &Serial2;
#else
                HardwareSerial *serialInstance = &Serial1;
#endif
                while (serialInstance->available()) {
                    serialPayloadSize = serialInstance->readBytes(serialBytes, meshtastic_Constants_DATA_PAYLOAD_LEN);
#endif
                    serialModuleRadio->sendPayload();
                }
            }
#endif
        }
        return (10);
    } else {
        return disable();
    }
}

/**
 * Sends telemetry packet over the mesh network.
 *
 * @param m The telemetry data to be sent
 *
 * @return void
 *
 * @throws None
 */
void SerialModule::sendTelemetry(meshtastic_Telemetry m)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_Telemetry_msg, &m);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR) {
        p->want_ack = true;
        p->priority = meshtastic_MeshPacket_Priority_HIGH;
    } else {
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    }
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

/**
 * Allocates a new mesh packet for use as a reply to a received packet.
 *
 * @return A pointer to the newly allocated mesh packet.
 */
meshtastic_MeshPacket *SerialModuleRadio::allocReply()
{
    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

/**
 * Sends a payload to a specified destination node.
 *
 * @param dest The destination node number.
 * @param wantReplies Whether or not to request replies from the destination node.
 */
void SerialModuleRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    const meshtastic_Channel *ch = (boundChannel != NULL) ? &channels.getByName(boundChannel) : NULL;
    meshtastic_MeshPacket *p = allocReply();
    p->to = dest;
    if (ch != NULL) {
        p->channel = ch->index;
    }
    p->decoded.want_response = wantReplies;

    p->want_ack = ACK;

    p->decoded.payload.size = serialPayloadSize; // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, serialBytes, p->decoded.payload.size);

    service->sendToMesh(p);
}

/**
 * Handle a received mesh packet.
 *
 * @param mp The received mesh packet.
 * @return The processed message.
 */
ProcessMessage SerialModuleRadio::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (moduleConfig.serial.enabled) {
        if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO) {
            // in API mode we don't care about stuff from radio.
            return ProcessMessage::CONTINUE;
        }

        auto &p = mp.decoded;
        // LOG_DEBUG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s",
        //          nodeDB->getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

        if (isFromUs(&mp)) {

            /*
             * If moduleConfig.serial.echo is true, then echo the packets that are sent out
             * back to the TX of the serial interface.
             */
            if (moduleConfig.serial.echo) {

                // For some reason, we get the packet back twice when we send out of the radio.
                //   TODO: need to find out why.
                if (lastRxID != mp.id) {
                    lastRxID = mp.id;
                    // LOG_DEBUG("* * Message came this device");
                    // serialPrint->println("* * Message came this device");
                    serialPrint->printf("%s", p.payload.bytes);
                }
            }
        } else {

            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT ||
                moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_SIMPLE) {
                serialPrint->write(p.payload.bytes, p.payload.size);
            } else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG) {
                meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(getFrom(&mp));
                const char *sender = (node && node->has_user) ? node->user.short_name : "???";
                serialPrint->println();
                serialPrint->printf("%s: %s", sender, p.payload.bytes);
                serialPrint->println();
            } else if ((moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA ||
                        moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO) &&
                       HAS_GPS) {
                // Decode the Payload some more
                meshtastic_Position scratch;
                meshtastic_Position *decoded = NULL;
                if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.portnum == ourPortNum) {
                    memset(&scratch, 0, sizeof(scratch));
                    if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Position_msg, &scratch)) {
                        decoded = &scratch;
                    }
                    // send position packet as WPL to the serial port
                    printWPL(outbuf, sizeof(outbuf), *decoded, nodeDB->getMeshNode(getFrom(&mp))->user.long_name,
                             moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO);
                    serialPrint->printf("%s", outbuf);
                }
            }
        }
    }
    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

/**
 * @brief Returns the baud rate of the serial module from the module configuration.
 *
 * @return uint32_t The baud rate of the serial module.
 */
uint32_t SerialModule::getBaudRate()
{
    if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_110) {
        return 110;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_300) {
        return 300;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_600) {
        return 600;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_1200) {
        return 1200;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_2400) {
        return 2400;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_4800) {
        return 4800;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_9600) {
        return 9600;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_19200) {
        return 19200;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_38400) {
        return 38400;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_57600) {
        return 57600;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_115200) {
        return 115200;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_230400) {
        return 230400;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_460800) {
        return 460800;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_576000) {
        return 576000;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_921600) {
        return 921600;
    }
    return BAUD;
}

// Add this structure to help with parsing WindGust =       24.4 serial lines.
struct ParsedLine {
    char name[64];
    char value[128];
};

/**
 * Parse a line of format "Name = Value" into name/value pair
 * @param line Input line to parse
 * @return ParsedLine containing name and value, or empty strings if parse failed
 */
ParsedLine parseLine(const char *line)
{
    ParsedLine result = {"", ""};

    // Find equals sign
    const char *equals = strchr(line, '=');
    if (!equals) {
        return result;
    }

    // Extract name by copying substring
    char nameBuf[64]; // Temporary buffer
    size_t nameLen = equals - line;
    if (nameLen >= sizeof(nameBuf)) {
        nameLen = sizeof(nameBuf) - 1;
    }
    strncpy(nameBuf, line, nameLen);
    nameBuf[nameLen] = '\0';

    // Trim whitespace from name
    char *nameStart = nameBuf;
    while (*nameStart && isspace(*nameStart))
        nameStart++;
    char *nameEnd = nameStart + strlen(nameStart) - 1;
    while (nameEnd > nameStart && isspace(*nameEnd))
        *nameEnd-- = '\0';

    // Copy trimmed name
    strncpy(result.name, nameStart, sizeof(result.name) - 1);
    result.name[sizeof(result.name) - 1] = '\0';

    // Extract value part (after equals)
    const char *valueStart = equals + 1;
    while (*valueStart && isspace(*valueStart))
        valueStart++;
    strncpy(result.value, valueStart, sizeof(result.value) - 1);
    result.value[sizeof(result.value) - 1] = '\0';

    // Trim trailing whitespace from value
    char *valueEnd = result.value + strlen(result.value) - 1;
    while (valueEnd > result.value && isspace(*valueEnd))
        *valueEnd-- = '\0';

    return result;
}

/**
 * Process the received weather station serial data, extract wind, voltage, and temperature information,
 * calculate averages and send telemetry data over the mesh network.
 *
 * @return void
 */
void SerialModule::processWXSerial()
{
#if SERIAL_PRINT_PORT != 0 && !defined(ARCH_STM32WL) && !defined(CONFIG_IDF_TARGET_ESP32C6)

    static unsigned int lastAveraged = 0;
    static unsigned int averageIntervalMillis = 300000; // 5 minutes hard coded.
    static double dir_sum_sin = 0;
    static double dir_sum_cos = 0;
    static float velSum = 0;
    static float gust = 0;
    static float lull = -1;
    static int velCount = 0;
    static int dirCount = 0;
    static char windDir[4] = "xxx";   // Assuming windDir is 3 characters long + null terminator
    static char windVel[5] = "xx.x";  // Assuming windVel is 4 characters long + null terminator
    static char windGust[5] = "xx.x"; // Assuming windGust is 4 characters long + null terminator
    static char batVoltage[5] = "0.0V";
    static char capVoltage[5] = "0.0V";
    static char temperature[5] = "00.0";
    static float batVoltageF = 0;
    static float capVoltageF = 0;
    static float temperatureF = 0;

    static char rainStr[] = "5780860000";
    static int rainSum = 0;
    static float rain = 0;
    bool gotwind = false;

    while (Serial2.available()) {
        // clear serialBytes buffer
        memset(serialBytes, '\0', sizeof(serialBytes));
        // memset(formattedString, '\0', sizeof(formattedString));
        serialPayloadSize = Serial2.readBytes(serialBytes, 512);
        // check for a strings we care about
        // example output of serial data fields from the WS85
        // WindDir      = 79
        // WindSpeed    = 0.5
        // WindGust     = 0.6
        // GXTS04Temp   = 24.4
        // Temperature = 23.4 // WS80

        // RainIntSum     = 0
        // Rain           = 0.0
        if (serialPayloadSize > 0) {
            // Define variables for line processing
            int lineStart = 0;
            int lineEnd = -1;

            // Process each byte in the received data
            for (size_t i = 0; i < serialPayloadSize; i++) {
                // go until we hit the end of line and then process the line
                if (serialBytes[i] == '\n') {
                    lineEnd = i;
                    // Extract the current line
                    char line[meshtastic_Constants_DATA_PAYLOAD_LEN];
                    memset(line, '\0', sizeof(line));
                    if ((size_t)(lineEnd - lineStart) < sizeof(line) - 1) {
                        memcpy(line, &serialBytes[lineStart], lineEnd - lineStart);

                        ParsedLine parsed = parseLine(line);
                        if (strlen(parsed.name) > 0) {
                            if (strcmp(parsed.name, "WindDir") == 0) {
                                strlcpy(windDir, parsed.value, sizeof(windDir));
                                double radians = GeoCoord::toRadians(strtof(windDir, nullptr));
                                dir_sum_sin += sin(radians);
                                dir_sum_cos += cos(radians);
                                dirCount++;
                                gotwind = true;
                            } else if (strcmp(parsed.name, "WindSpeed") == 0) {
                                strlcpy(windVel, parsed.value, sizeof(windVel));
                                float newv = strtof(windVel, nullptr);
                                velSum += newv;
                                velCount++;
                                if (newv < lull || lull == -1) {
                                    lull = newv;
                                }
                                gotwind = true;
                            } else if (strcmp(parsed.name, "WindGust") == 0) {
                                strlcpy(windGust, parsed.value, sizeof(windGust));
                                float newg = strtof(windGust, nullptr);
                                if (newg > gust) {
                                    gust = newg;
                                }
                                gotwind = true;
                            } else if (strcmp(parsed.name, "BatVoltage") == 0) {
                                strlcpy(batVoltage, parsed.value, sizeof(batVoltage));
                                batVoltageF = strtof(batVoltage, nullptr);
                                break; // last possible data we want so break
                            } else if (strcmp(parsed.name, "CapVoltage") == 0) {
                                strlcpy(capVoltage, parsed.value, sizeof(capVoltage));
                                capVoltageF = strtof(capVoltage, nullptr);
                            } else if (strcmp(parsed.name, "GXTS04Temp") == 0 || strcmp(parsed.name, "Temperature") == 0) {
                                strlcpy(temperature, parsed.value, sizeof(temperature));
                                temperatureF = strtof(temperature, nullptr);
                            } else if (strcmp(parsed.name, "RainIntSum") == 0) {
                                strlcpy(rainStr, parsed.value, sizeof(rainStr));
                                rainSum = int(strtof(rainStr, nullptr));
                            } else if (strcmp(parsed.name, "Rain") == 0) {
                                strlcpy(rainStr, parsed.value, sizeof(rainStr));
                                rain = strtof(rainStr, nullptr);
                            }
                        }

                        // Update lineStart for the next line
                        lineStart = lineEnd + 1;
                    }
                }
            }
            break;
            // clear the input buffer
            while (Serial2.available() > 0) {
                Serial2.read(); // Read and discard the bytes in the input buffer
            }
        }
    }
    if (gotwind) {

        LOG_INFO("WS8X : %i %.1fg%.1f %.1fv %.1fv %.1fC rain: %.1f, %i sum", atoi(windDir), strtof(windVel, nullptr),
                 strtof(windGust, nullptr), batVoltageF, capVoltageF, temperatureF, rain, rainSum);
    }
    if (gotwind && !Throttle::isWithinTimespanMs(lastAveraged, averageIntervalMillis)) {
        // calculate averages and send to the mesh
        float velAvg = 1.0 * velSum / velCount;

        double avgSin = dir_sum_sin / dirCount;
        double avgCos = dir_sum_cos / dirCount;

        double avgRadians = atan2(avgSin, avgCos);
        float dirAvg = GeoCoord::toDegrees(avgRadians);

        if (dirAvg < 0) {
            dirAvg += 360.0;
        }
        lastAveraged = millis();

        // make a telemetry packet with the data
        meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
        m.which_variant = meshtastic_Telemetry_environment_metrics_tag;

        m.variant.environment_metrics.wind_speed = velAvg;
        m.variant.environment_metrics.has_wind_speed = true;

        m.variant.environment_metrics.wind_direction = dirAvg;
        m.variant.environment_metrics.has_wind_direction = true;

        m.variant.environment_metrics.temperature = temperatureF;
        m.variant.environment_metrics.has_temperature = true;

        m.variant.environment_metrics.voltage =
            capVoltageF > batVoltageF ? capVoltageF : batVoltageF; // send the larger of the two voltage values.
        m.variant.environment_metrics.has_voltage = true;

        m.variant.environment_metrics.wind_gust = gust;
        m.variant.environment_metrics.has_wind_gust = true;

        m.variant.environment_metrics.rainfall_24h = rainSum;
        m.variant.environment_metrics.has_rainfall_24h = true;

        // not sure if this value is actually the 1hr sum so needs to do some testing
        m.variant.environment_metrics.rainfall_1h = rain;
        m.variant.environment_metrics.has_rainfall_1h = true;

        if (lull == -1)
            lull = 0;
        m.variant.environment_metrics.wind_lull = lull;
        m.variant.environment_metrics.has_wind_lull = true;

        LOG_INFO("WS8X Transmit speed=%fm/s, direction=%d , lull=%f, gust=%f, voltage=%f temperature=%f",
                 m.variant.environment_metrics.wind_speed, m.variant.environment_metrics.wind_direction,
                 m.variant.environment_metrics.wind_lull, m.variant.environment_metrics.wind_gust,
                 m.variant.environment_metrics.voltage, m.variant.environment_metrics.temperature);

        sendTelemetry(m);

        // reset counters and gust/lull
        velSum = velCount = dirCount = 0;
        dir_sum_sin = dir_sum_cos = 0;
        gust = 0;
        lull = -1;
    }
#endif
    return;
}

/**
 * Observer callback for text messages
 * Uses the same logger function as LOG mode for consistency
 */
int SerialModuleRadio::onNotify(const meshtastic_MeshPacket *packet)
{
    if (moduleConfig.serial.enabled && moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOGTEXT) {
        SerialModule::logPacketClean(packet);
    }

    return 0; // Continue processing
}

// duplicate suppression - track recently logged packet IDs
#define MAX_RECENT_LOGGED_PACKETS 4
struct RecentLoggedPacket {
    NodeNum from;
    PacketId id;
    uint32_t timestamp;
};
static RecentLoggedPacket recentLoggedPackets[MAX_RECENT_LOGGED_PACKETS];
static uint8_t recentLoggedIndex = 0;

static bool wasLoggedRecently(const meshtastic_MeshPacket *p)
{
    NodeNum from = getFrom(p);
    PacketId id = p->id;
    uint32_t now = millis();
    uint32_t timeoutMs = 5000; // 5 second window for duplicates

    // Check if we've seen this (from, id) pair recently
    for (int i = 0; i < MAX_RECENT_LOGGED_PACKETS; i++) {
        if (recentLoggedPackets[i].from == from && recentLoggedPackets[i].id == id) {
            uint32_t age = now - recentLoggedPackets[i].timestamp;
            if (age < timeoutMs) {
                return true; // Duplicate found
            }
            // Expired, update with new timestamp
            recentLoggedPackets[i].timestamp = now;
            return false;
        }
    }

    // Not found, add it to the list
    recentLoggedPackets[recentLoggedIndex].from = from;
    recentLoggedPackets[recentLoggedIndex].id = id;
    recentLoggedPackets[recentLoggedIndex].timestamp = now;
    recentLoggedIndex = (recentLoggedIndex + 1) % MAX_RECENT_LOGGED_PACKETS;

    return false;
}

// Clean packet logger for LOG and LOGTEXT modes - shows time, to, from, packet ID, and contents
void SerialModule::logPacketClean(const meshtastic_MeshPacket *p)
{
    if (!moduleConfig.serial.enabled) {
        return;
    }

    bool isLogMode = (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOG);
    bool isLogTextOnlyMode = (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_LOGTEXT);

    if (!isLogMode && !isLogTextOnlyMode) {
        return;
    }

    // For LOGTEXT mode, only process text messages
    if (isLogTextOnlyMode && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        if (!MeshService::isTextPayload(p)) {
            return;
        }
    }

    // Suppress immediate duplicates
    if (wasLoggedRecently(p)) {
        return;
    }

    Print *uart = nullptr;
    if (isLogMode) {
        uart = RedirectablePrint::uartLogDestination;
    } else if (isLogTextOnlyMode) {
        // For LOGTEXT, use serialPrint directly
        uart = serialPrint;
    }

    if (uart == nullptr) {
        return;
    }

    // Get time
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    char timeStr[32] = "??:??:??";
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;
        int hour = hms / SEC_PER_HOUR;
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN;
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour, min, sec);
    }

    // Get sender info
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(getFrom(p));
    const char *fromName = (node && node->has_user) ? node->user.short_name : "???";
    NodeNum fromNode = getFrom(p);

    // Get destination info - show actual node number instead of "DM"
    char toInfo[32];
    if (isBroadcast(p->to)) {
        snprintf(toInfo, sizeof(toInfo), "BROADCAST");
    } else {
        snprintf(toInfo, sizeof(toInfo), "0x%x", p->to);
    }

    // Format: [HH:MM:SS] ID:0xXXXX FROM:0xXXXX (name) TO:0xXXXX or BROADCAST
    uart->printf("[%s] ID:0x%x FROM:0x%x (%s) TO:%s", timeStr, p->id, fromNode, fromName, toInfo);

    // Only show contents for decoded packets
    LOG_DEBUG("serialModule: Packet payload_variant=%d", p->which_payload_variant);
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        auto &decoded = p->decoded;

        // Text messages
        if (MeshService::isTextPayload(p)) {
            char messageText[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
            size_t msgLen = decoded.payload.size < sizeof(messageText) ? decoded.payload.size : sizeof(messageText) - 1;
            memcpy(messageText, decoded.payload.bytes, msgLen);
            messageText[msgLen] = '\0';
            uart->printf(" MSG:%s\n", messageText);
        }
        // Telemetry
        else if (decoded.portnum == meshtastic_PortNum_TELEMETRY_APP) {
            meshtastic_Telemetry telemetry;
            memset(&telemetry, 0, sizeof(telemetry));
            bool decodeOk =
                pb_decode_from_bytes(decoded.payload.bytes, decoded.payload.size, &meshtastic_Telemetry_msg, &telemetry);
            if (decodeOk) {
                if (telemetry.which_variant == meshtastic_Telemetry_environment_metrics_tag) {
                    const auto &m = telemetry.variant.environment_metrics;
                    uart->printf(" TELEMETRY:env");
                    if (m.has_temperature)
                        uart->printf(" temp=%.1fC", m.temperature);
                    if (m.has_relative_humidity)
                        uart->printf(" humidity=%.1f%%", m.relative_humidity);
                    if (m.barometric_pressure != 0)
                        uart->printf(" pressure=%.1f", m.barometric_pressure);
                    if (m.has_voltage)
                        uart->printf(" voltage=%.2fV", m.voltage);
                    if (m.has_wind_speed)
                        uart->printf(" wind=%.1fm/s@%.0fdeg", m.wind_speed, m.wind_direction);
                    if (m.has_iaq)
                        uart->printf(" IAQ=%d", m.iaq);
                    uart->printf("\n");
                } else if (telemetry.which_variant == meshtastic_Telemetry_device_metrics_tag) {
                    const auto &m = telemetry.variant.device_metrics;
                    uart->printf(" TELEMETRY:device");
                    if (m.has_battery_level)
                        uart->printf(" battery=%d%%", m.battery_level);
                    uart->printf(" voltage=%.2fV", m.voltage);
                    uart->printf(" ch_util=%.1f%%", m.channel_utilization);
                    uart->printf(" air_util_tx=%.1f%%", m.air_util_tx);
                    uart->printf(" uptime=%us\n", m.uptime_seconds);
                } else {
                    uart->printf(" TELEMETRY:other\n");
                }
            } else {
                uart->printf(" TELEMETRY:decode_failed\n");
            }
        }
        // Position packets
        else if (decoded.portnum == meshtastic_PortNum_POSITION_APP) {
            meshtastic_Position position;
            memset(&position, 0, sizeof(position));
            if (pb_decode_from_bytes(decoded.payload.bytes, decoded.payload.size, &meshtastic_Position_msg, &position)) {
                uart->printf(" POSITION lat=%d lon=%d alt=%d sats=%d\n", position.latitude_i, position.longitude_i,
                             position.altitude, position.sats_in_view);
            } else {
                uart->printf(" POSITION decode_failed\n");
            }
        }
        // NodeInfo packets
        else if (decoded.portnum == meshtastic_PortNum_NODEINFO_APP) {
            meshtastic_User user;
            memset(&user, 0, sizeof(user));
            if (pb_decode_from_bytes(decoded.payload.bytes, decoded.payload.size, &meshtastic_User_msg, &user)) {
                uart->printf(" NODEINFO short_name=%s long_name=%s\n", user.short_name, user.long_name);
            } else {
                uart->printf(" NODEINFO decode_failed\n");
            }
        }
        // Routing packets
        else if (decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
            meshtastic_Routing routing;
            memset(&routing, 0, sizeof(routing));
            if (pb_decode_from_bytes(decoded.payload.bytes, decoded.payload.size, &meshtastic_Routing_msg, &routing)) {
                uart->printf(" ROUTING variant=%d\n", routing.which_variant);
            } else {
                uart->printf(" ROUTING decode_failed\n");
            }
        }
        // Other packet types - just show portnum
        else {
            uart->printf(" PORT:%d\n", decoded.portnum);
        }
    } else {
        uart->printf(" ENCRYPTED\n");
    }
}
#endif