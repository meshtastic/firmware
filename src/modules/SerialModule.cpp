#include "SerialModule.h"
#include "Default.h"
#include "GeoCoord.h"
#include "MeshService.h"
#include "NMEAWPL.h"
#include "NodeDB.h"
#include "NodeStatus.h"
#include "Router.h"
#include "TransmitHistory.h"
#include "UptimeClock.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "meshUtils.h"
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

// Outside the architecture guard on purpose: config validation, not serial I/O. See SerialModule.h.
bool serialConfigIsValid(const meshtastic_ModuleConfig_SerialConfig &config)
{
    if (config.override_console_serial_port && !IS_ONE_OF(config.mode, meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA,
                                                          meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO,
                                                          meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MS_CONFIG)) {
        const char *warning = "Invalid Serial config: override console serial port is only supported in NMEA, CalTopo, or MS "
                              "Config output-only modes.";
        LOG_ERROR(warning);
#ifndef PIO_UNIT_TESTING
        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        if (cn) {
            cn->level = meshtastic_LogRecord_Level_ERROR;
            cn->time = getValidTime(RTCQualityFromNet);
            snprintf(cn->message, sizeof(cn->message), "%s", warning);
            service->sendClientNotification(cn);
        }
#endif
        return false;
    }

    return true;
}

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

#if defined(ARCH_STM32WL) || SERIAL_PRINT_PORT != 0 || !MESHTASTIC_EXCLUDE_MODBUS
/// The UART the module drives when rxd/txd are set.
static HardwareSerial *serialModulePort()
{
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(RAK3172)
    return &Serial1;
#else
    return &Serial2;
#endif
}
#endif

#if !MESHTASTIC_EXCLUDE_MODBUS
#define MODBUS_POLL_MS 10000
#define MODBUS_SCAN_RETRY_MS 60000
#define MODBUS_MAX_FAILS 10
#define MODBUS_SESSION_MS 30000
#define MODBUS_SILENCE_MS 50
#define MODBUS_RAIN_HOUR_MS 3600000
// RX in the first half of serialBytes; the tunnel keeps its last response in the second half.
#define MODBUS_RX_MAX (sizeof(serialBytes) / 2)
#if defined(MODBUS_PWR_EN_PIN) && !defined(MODBUS_PWR_WARMUP_MS)
#define MODBUS_PWR_WARMUP_MS 2000
#endif
#endif

SerialModuleRadio::SerialModuleRadio() : SinglePortModule("SerialModuleRadio", meshtastic_PortNum_SERIAL_APP)
{
    switch (moduleConfig.serial.mode) {
    case meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG:
        ourPortNum = meshtastic_PortNum_TEXT_MESSAGE_APP;
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
            HardwareSerial *serialInstance = serialModulePort();
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

            firstTime = 0;

            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_WS85 ||
                moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MODBUS) {
                // First send after the shared periodic-broadcast start delay, like the telemetry modules
                telemetryStartAt = millis();
                telemetryStartDelay = serialModuleRadio->setStartDelay();
            }

#if !MESHTASTIC_EXCLUDE_MODBUS
            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MODBUS) {
#ifdef MODBUS_DE_PIN
                pinMode(MODBUS_DE_PIN, OUTPUT);
                digitalWrite(MODBUS_DE_PIN, LOW);
#endif
#ifdef MODBUS_PWR_EN_PIN
                pinMode(MODBUS_PWR_EN_PIN, OUTPUT);
                digitalWrite(MODBUS_PWR_EN_PIN, HIGH);
                mbCycleAt = millis();
                mbCycleMs = MODBUS_PWR_WARMUP_MS;
#endif
#ifdef MODBUS_SLAVE_ADDR
                mbSensor.addr = MODBUS_SLAVE_ADDR;
                mbSensor.caps = modbus::capsForAddress(MODBUS_SLAVE_ADDR);
#endif
            }
#endif

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
                        if (nodeInfoLiteHasUser(tempNodeInfo) && nodeDB->hasValidPosition(tempNodeInfo)) {
                            meshtastic_PositionLite pos;
                            if (nodeDB->copyNodePosition(tempNodeInfo->num, pos)) {
                                printWPL(outbuf, sizeof(outbuf), pos, tempNodeInfo->long_name, true);
                                serialPrint->printf("%s", outbuf);
                            }
                        }
                        tempNodeInfo = nodeDB->readNextMeshNode(readIndex);
                    }
                }
            }
#if !MESHTASTIC_EXCLUDE_MODBUS
            else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MODBUS) {
                return runModbus();
            }
#endif

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
                HardwareSerial *serialInstance = serialModulePort();
                while (serialInstance->available()) {
                    serialPayloadSize = serialInstance->readBytes(serialBytes, meshtastic_Constants_DATA_PAYLOAD_LEN);
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

// Own TransmitHistory slot, so a board with I2C environment sensors as well keeps both streams.
static constexpr uint16_t TX_HISTORY_KEY_SERIAL_TELEMETRY = 0x8006;

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
    if (!p)
        return;
    m.time = getTime();
    p->decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_Telemetry_msg, &m);
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    // Same as EnvironmentTelemetryModule: periodic telemetry must not outrank interactive traffic.
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR)
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    else
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    TransmitHistory::getInstance()->setLastSentToMesh(TX_HISTORY_KEY_SERIAL_TELEMETRY);
}

/// Whether telemetry may go out now: the cadence of EnvironmentTelemetryModule, carried across reboots.
bool SerialModule::telemetryDue()
{
    if (Throttle::isWithinTimespanMs(telemetryStartAt, telemetryStartDelay))
        return false;
    telemetryStartDelay = 0; // passed for good, also once millis() wraps
    uint32_t lastSend = TransmitHistory::getInstance()->getLastSentToMeshMillis(TX_HISTORY_KEY_SERIAL_TELEMETRY);
    uint32_t interval = Default::getConfiguredOrDefaultMsScaled(moduleConfig.telemetry.environment_update_interval,
                                                                default_telemetry_broadcast_interval_secs,
                                                                nodeStatus->getNumOnline(), TrafficType::TELEMETRY);
    return !(lastSend && Throttle::isWithinTimespanMs(lastSend, interval)) &&
           airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
           airTime->isTxAllowedAirUtil();
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
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p)
        return;
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
        if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO ||
            moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MODBUS) {
            // in API mode we don't care about stuff from radio, and nothing from the mesh may reach a Modbus bus.
            return ProcessMessage::CONTINUE;
        }

        auto &p = mp.decoded;
        // LOG_DEBUG("Received text msg self=0x%08x, from=0x%08x, to=0x%08x, id=%d, msg=%.*s",
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
                    serialPrint->printf("%.*s", (int)p.payload.size, p.payload.bytes);
                }
            }
        } else {

            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT ||
                moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_SIMPLE) {
                serialPrint->write(p.payload.bytes, p.payload.size);
            } else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG) {
                meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(getFrom(&mp));
                const char *sender = nodeInfoLiteHasUser(node) ? node->short_name : "???";
                serialPrint->println();
                serialPrint->printf("%s: %.*s", sender, (int)p.payload.size, p.payload.bytes);
                serialPrint->println();
            } else if ((moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA ||
                        moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO) &&
                       HAS_GPS) {
                // Decode the Payload some more
                meshtastic_Position scratch;
                if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.portnum == ourPortNum) {
                    memset(&scratch, 0, sizeof(scratch));
                    // A payload that fails to decode leaves nothing to report, so say nothing.
                    if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Position_msg, &scratch)) {
                        // send position packet as WPL to the serial port
                        const meshtastic_NodeInfoLite *senderNode = nodeDB->getMeshNode(getFrom(&mp));
                        const char *senderName = senderNode ? senderNode->long_name : "";
                        printWPL(outbuf, sizeof(outbuf), scratch, senderName,
                                 moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO);
                        serialPrint->printf("%s", outbuf);
                    }
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
#if !MESHTASTIC_EXCLUDE_MODBUS
    if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MODBUS)
        return 9600; // SenseCAP ONE default
#endif
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
                                double radians = GeoCoord::toRadians(parseDecimalFloat(windDir));
                                dir_sum_sin += sin(radians);
                                dir_sum_cos += cos(radians);
                                dirCount++;
                                gotwind = true;
                            } else if (strcmp(parsed.name, "WindSpeed") == 0) {
                                strlcpy(windVel, parsed.value, sizeof(windVel));
                                float newv = parseDecimalFloat(windVel);
                                velSum += newv;
                                velCount++;
                                if (newv < lull || lull == -1) {
                                    lull = newv;
                                }
                                gotwind = true;
                            } else if (strcmp(parsed.name, "WindGust") == 0) {
                                strlcpy(windGust, parsed.value, sizeof(windGust));
                                float newg = parseDecimalFloat(windGust);
                                if (newg > gust) {
                                    gust = newg;
                                }
                                gotwind = true;
                            } else if (strcmp(parsed.name, "BatVoltage") == 0) {
                                strlcpy(batVoltage, parsed.value, sizeof(batVoltage));
                                batVoltageF = parseDecimalFloat(batVoltage);
                                break; // last possible data we want so break
                            } else if (strcmp(parsed.name, "CapVoltage") == 0) {
                                strlcpy(capVoltage, parsed.value, sizeof(capVoltage));
                                capVoltageF = parseDecimalFloat(capVoltage);
                            } else if (strcmp(parsed.name, "GXTS04Temp") == 0 || strcmp(parsed.name, "Temperature") == 0) {
                                strlcpy(temperature, parsed.value, sizeof(temperature));
                                temperatureF = parseDecimalFloat(temperature);
                            } else if (strcmp(parsed.name, "RainIntSum") == 0) {
                                strlcpy(rainStr, parsed.value, sizeof(rainStr));
                                rainSum = int(parseDecimalFloat(rainStr));
                            } else if (strcmp(parsed.name, "Rain") == 0) {
                                strlcpy(rainStr, parsed.value, sizeof(rainStr));
                                rain = parseDecimalFloat(rainStr);
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

        LOG_INFO("WS8X : %i %.1fg%.1f %.1fv %.1fv %.1fC rain: %.1f, %i sum", atoi(windDir), parseDecimalFloat(windVel),
                 parseDecimalFloat(windGust), batVoltageF, capVoltageF, temperatureF, rain, rainSum);
    }
    if (gotwind && velCount > 0 && dirCount > 0 && telemetryDue()) {
        // calculate averages and send to the mesh
        float velAvg = 1.0 * velSum / velCount;

        double avgSin = dir_sum_sin / dirCount;
        double avgCos = dir_sum_cos / dirCount;

        double avgRadians = atan2(avgSin, avgCos);
        float dirAvg = GeoCoord::toDegrees(avgRadians);

        if (dirAvg < 0) {
            dirAvg += 360.0;
        }

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

#if !MESHTASTIC_EXCLUDE_MODBUS
/// MODBUS mode: master for one RS485 sensor, slave for the API tunnel. Never blocks waiting for the bus.
int32_t SerialModule::runModbus()
{
    HardwareSerial *port = serialModulePort();
    uint8_t *rx = (uint8_t *)serialBytes;
    while (port->available()) {
        int c = port->read();
        if (c < 0)
            break;
        // A response cannot start with another address; drop line turn-around noise before it.
        if (mbWaiting && !mbRxLen && c != mbAddr)
            continue;
        if (mbRxLen < MODBUS_RX_MAX)
            rx[mbRxLen++] = c;
#ifdef MODBUS_API_ADDR
        mbRxAt = millis();
#endif
    }

    if (mbWaiting) {
        modbus::Result r = modbus::checkResponse(rx, mbRxLen, mbAddr, modbus::READ_INPUT, mbCount);
        uint32_t timeout = moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT;
        if (r == modbus::RESP_INCOMPLETE && Throttle::isWithinTimespanMs(mbSentAt, timeout))
            return 10;
        mbWaiting = false;
        mbRxLen = 0;
        modbusPollDone(r);
        return 10;
    }

#ifdef MODBUS_API_ADDR
    if (mbRxLen) {
        size_t need = mbTunnel.frameLength(rx, mbRxLen);
        bool complete = need && mbRxLen >= need;
        if (!complete && !Throttle::hasElapsed(mbRxAt, MODBUS_SILENCE_MS))
            return 10; // a frame is still arriving, keep off the bus
        modbusTunnel(complete ? need : mbRxLen);
        mbRxLen = 0;
    }
    runOncePart(nullptr, 0); // pull the next API frame into the tunnel's retained slot
    if (mbTunnelAt && Throttle::isWithinTimespanMs(mbTunnelAt, MODBUS_SESSION_MS))
        return 10; // provisioning session, sensor polling paused
#else
    mbRxLen = 0;
#endif

    if (!mbBusy) {
        if (!Throttle::hasElapsed(mbCycleAt, mbCycleMs))
            return 10;
        mbBusy = true;
        mbStep = 0;
        mbCycleAt = millis();
        mbCycleMs = MODBUS_POLL_MS;
    }
    if (!modbusNextRequest()) {
        modbusCycleDone();
        return 10;
    }
    // The bus has been idle since at least the previous call, 10 ms ago: more than the 3.5 character gap.
    uint8_t req[8];
    size_t reqLen = modbus::buildRead(req, mbAddr, modbus::READ_INPUT, mbReg, mbCount);
    modbusSend(req, reqLen);
    mbSentAt = millis();
    mbWaiting = true;
    return 10;
}

/// Fill in the request for the current step of the poll or scan cycle; false once the cycle is complete.
bool SerialModule::modbusNextRequest()
{
    if (mbSensor.addr) {
        mbAddr = mbSensor.addr;
        return modbus::nextRead(mbSensor.caps, mbSensor.split, mbStep, mbReg, mbCount);
    }
    if (mbStep >= modbus::profileCount)
        return false;
    mbAddr = modbus::profiles[mbStep].addr;
    mbReg = 0;
    mbCount = 2;
    return true;
}

void SerialModule::modbusPollDone(modbus::Result r)
{
    if (r == modbus::RESP_OK) {
        if (!mbSensor.addr) {
            LOG_INFO("Modbus sensor found at address %u", mbAddr);
            mbSensor = {mbAddr, modbus::capsForAddress(mbAddr)};
            mbBusy = false;
            mbCycleMs = 0;
            return;
        }
        modbus::storeRegisters(mbRaw, mbReg, (const uint8_t *)serialBytes + 3, serialBytes[2]);
        if (!mbSensor.split && mbStep == 0)
            mbSensor.fullOk = true;
        mbStep++;
        return;
    }
    if (!mbSensor.addr) {
        mbStep++; // next scan candidate
        return;
    }
    LOG_WARN("Modbus poll of address %u register 0x%x failed (%u)", mbAddr, mbReg, r);
    // A sensor that never answered the full base block gets the short reads, which skip light and rain.
    if (!mbSensor.split && mbStep == 0 && !mbSensor.fullOk) {
        mbSensor.split = true;
        mbSensor.caps &= ~(modbus::CAP_LIGHT | modbus::CAP_RAIN);
    }
    mbBusy = false;
#ifndef MODBUS_SLAVE_ADDR
    if (++mbSensor.fails >= MODBUS_MAX_FAILS)
        mbSensor = {};
#endif
}

void SerialModule::modbusCycleDone()
{
    mbBusy = false;
    if (!mbSensor.addr) {
        mbCycleMs = MODBUS_SCAN_RETRY_MS;
        return;
    }
    mbSensor.fails = 0;
    if (!mbSensor.split && mbRaw[0x1E / 2]) // the short reads do not cover the tilt register
        LOG_WARN("Modbus sensor tipped over");
    if (Throttle::hasElapsed(mbRainHourAt, MODBUS_RAIN_HOUR_MS)) {
        mbAgg.nextHour();
        mbRainHourAt = millis();
    }
    mbAgg.add(mbRaw);

    if (!telemetryDue())
        return;

    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    m.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    mbAgg.environment(m.variant.environment_metrics, mbSensor.caps);
    sendTelemetry(m);
    if (mbSensor.caps & (modbus::CAP_PM | modbus::CAP_CO2)) {
        m.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
        m.variant.air_quality_metrics = meshtastic_AirQualityMetrics_init_zero;
        mbAgg.airQuality(m.variant.air_quality_metrics, mbSensor.caps);
        sendTelemetry(m);
    }
    mbAgg.reset();
}

void SerialModule::modbusSend(const uint8_t *buf, size_t len)
{
    HardwareSerial *port = serialModulePort();
#ifdef MODBUS_DE_PIN
    digitalWrite(MODBUS_DE_PIN, HIGH);
#endif
    port->write(buf, len);
#ifdef MODBUS_DE_PIN
    port->flush();
    digitalWrite(MODBUS_DE_PIN, LOW);
#endif
}

#ifdef MODBUS_API_ADDR
/// Answer one host frame from the first len bytes of serialBytes.
void SerialModule::modbusTunnel(size_t len)
{
    if (!config.security.serial_enabled)
        return;
    const uint8_t *in;
    size_t inLen;
    uint8_t *resp = (uint8_t *)serialBytes + MODBUS_RX_MAX;
    size_t n = mbTunnel.handle((const uint8_t *)serialBytes, len, resp, in, inLen);
    if (!n)
        return;
    mbTunnelAt = lastContactMsec = Time::stampMillis();
    // Acknowledge first: handling the input can take longer than the host waits for an answer.
    modbusSend(resp, n);
    if (inLen)
        runOncePart((char *)in, inLen);
}

bool SerialModule::writeFrame(uint8_t *buf, size_t len, bool bestEffort)
{
    if (moduleConfig.serial.mode != meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MODBUS)
        return StreamAPI::writeFrame(buf, len, bestEffort);
    // Log records stay off the bus; required frames wait in buf until the host has read them.
    if (bestEffort || !len || mbTunnel.frame)
        return false;
    mbTunnel.frameLen = buildFrameHeader(buf, len);
    mbTunnel.frame = buf;
    return false; // retained, so writeStream() stops dequeuing
}

bool SerialModule::finishPendingFrame()
{
    return moduleConfig.serial.mode != meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MODBUS || !mbTunnel.frame;
}
#endif
#endif
#endif
