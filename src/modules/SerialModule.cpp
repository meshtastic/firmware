#include "SerialModule.h"
#include "GeoCoord.h"
#include "MeshService.h"
#include "NMEAWPL.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "gps/RTC.h"
#include <Arduino.h>
#include <Throttle.h>

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

#if defined(USE_SERIAL_PACKET_IO)

// Require pin definitions at compile time if not set in moduleConfig
#ifndef SERIAL_PACKET_IO_RXD
#error "SERIAL_PACKET_IO_RXD must be defined in platformio.ini build_flags (e.g., -DSERIAL_PACKET_IO_RXD=15)"
#endif
#ifndef SERIAL_PACKET_IO_TXD
#error "SERIAL_PACKET_IO_TXD must be defined in platformio.ini build_flags (e.g., -DSERIAL_PACKET_IO_TXD=16)"
#endif

/*

    This module has been totally rewritten to function as a serial 'interface'
    for the router to send packets over the serial link similar to the MQTT interface.

    The serial link is simply an alternate path for packets other than the air.

    This is not a module, it does not source new packets or sink packets

    This is intended for the WisMesh starter kit (19007 board+ 4630) + RS485 which uses Serial1

    This module will wait not transmit over the link if the RX is currently busy.

*/

#define TIMEOUT 250
#define BAUD 38400
#define ACK 1

// API: Defaulting to the formerly removed phone_timeout_secs value of 15 minutes
#define SERIAL_CONNECTION_TIMEOUT (15 * 60) * 1000UL

#define PACKET_FLAGS_ENCRYPTED_MASK PACKET_FLAGS_VIA_MQTT_MASK

SerialModule *serialModule;
SerialModuleRadio *serialModuleRadio;

meshtastic_serialPacket outPacket;
meshtastic_serialPacket inPacket;
char tmpbuf[250]; // for debug only

SerialModule::SerialModule() : StreamAPI(&Serial1), concurrency::OSThread("Serial") {}
static Print *serialPrint = &Serial1;

#define headerByte1 0xaa
#define headerByte2 0x55

size_t serialPayloadSize;

uint32_t computeCrc32(const uint8_t *buf, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFF;        // Initial value
    const uint32_t poly = 0xEDB88320; // CRC-32 polynomial

    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint8_t)buf[i];          // XOR with the current byte
        for (int j = 7; j >= 0; j--) {   // Perform 8 bitwise operations
            if (crc & 0x80000000) {      // Check if the MSB is set
                crc = (crc << 1) ^ poly; // Shift and XOR with polynomial
            } else {
                crc <<= 1; // Shift if MSB is not set
            }
        }
    }
    return ~crc; // Return the final CRC value
}

void meshPacketToSerialPacket(meshtastic_MeshPacket *p, meshtastic_serialPacket *sp)
{
    sp->header.hbyte1 = headerByte1;
    sp->header.hbyte2 = headerByte2;
    sp->header.crc = 0;

    if (p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        sp->header.size = sizeof(SerialPacketHeader) + p->encrypted.size;
        memcpy(sp->payload, p->encrypted.bytes, p->encrypted.size);
    } else {
        sp->header.size = sizeof(SerialPacketHeader) + p->decoded.payload.size;
        memcpy(sp->payload, p->decoded.payload.bytes, p->decoded.payload.size);
    }
    sp->header.from = p->from;
    sp->header.to = p->to;
    sp->header.id = p->id;
    sp->header.channel = p->channel;

    sp->header.hop_limit = p->hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK;
    sp->header.hop_start = p->hop_start & PACKET_FLAGS_HOP_START_MASK;
    if (sp->header.hop_start == 0) {
        sp->header.hop_start = sp->header.hop_limit; // default to hop_limit if not set
    }
    sp->header.flags = 0x20 | (p->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0) |
                       ((p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) ? PACKET_FLAGS_ENCRYPTED_MASK : 0);

    sp->header.crc = computeCrc32((const uint8_t *)sp, sp->header.size);
}

void insertSerialPacketToMesh(meshtastic_serialPacket *sp)
{

    UniquePacketPoolPacket p = packetPool.allocUniqueZeroed();

    p->from = sp->header.from;
    p->to = sp->header.to;
    p->id = sp->header.id;
    p->channel = sp->header.channel;
    // assert(HOP_MAX <= PACKET_FLAGS_HOP_LIMIT_MASK); // If hopmax changes, carefully check this code
    p->hop_limit = sp->header.hop_limit;
    p->hop_start = sp->header.hop_start;
    p->want_ack = !!(sp->header.flags & PACKET_FLAGS_WANT_ACK_MASK);
    p->via_mqtt = 0;
    uint16_t payloadLen = sp->header.size - sizeof(SerialPacketHeader);
    if (!!(sp->header.flags & PACKET_FLAGS_ENCRYPTED_MASK)) {
        p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
        memcpy(p->encrypted.bytes, sp->payload, payloadLen);
        p->encrypted.size = payloadLen;
    } else {
        p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        memcpy(p->decoded.payload.bytes, sp->payload, payloadLen);
        p->decoded.payload.size = payloadLen;
    }

    LOG_DEBUG("Serial Module RX  from=0x%0x, to=0x%0x, packet_id=0x%0x", p->from, p->to, p->id);

    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        memcpy(tmpbuf, p->decoded.payload.bytes, p->decoded.payload.size);
        tmpbuf[p->decoded.payload.size + 1] = 0;
        LOG_DEBUG("Serial Module RX packet of %d bytes, msg: %s", sp->header.size, tmpbuf);
    }

    router->enqueueReceivedMessage(p.release());
}

// check if this recieved serial packet is valid
bool checkIfValidPacket(meshtastic_serialPacket *sp)
{

    if (sp->header.hbyte1 != headerByte1 || sp->header.hbyte2 != headerByte2) {
        LOG_DEBUG("SerialModule:: valid packet check fail, header bytes");
        return false;
    }
    if (sp->header.size == 0 || sp->header.size > sizeof(meshtastic_serialPacket)) {
        LOG_DEBUG("SerialModule:: valid packet check fail, invalid size");
        return false;
    }

    uint32_t received_crc = sp->header.crc;
    sp->header.crc = 0; // need to set to zero for computing CRC
    if (computeCrc32((const uint8_t *)sp, sp->header.size) != received_crc) {
        LOG_DEBUG("SerialModule:: valid packet check fail, invalid crc");
        sp->header.crc = received_crc; // restore
        return false;
    }
    sp->header.crc = received_crc; // restore
    return true;
}

SerialModuleRadio::SerialModuleRadio() : MeshModule("SerialModuleRadio")
{
    ourPortNum = meshtastic_PortNum_SERIAL_APP;
}

// define a simple verion of SerialModule that does not have all of the other crap in it
// This is intended for the WisMesh starter kit + RS485 which uses Serial1
//

int32_t SerialModule::runOnce()
{

    moduleConfig.serial.enabled = true;
    // Set pins: Priority 1 = moduleConfig (user config), Priority 2 = compile-time defines
    if (!moduleConfig.serial.rxd) {
        moduleConfig.serial.rxd = SERIAL_PACKET_IO_RXD;
    }
    if (!moduleConfig.serial.txd) {
        moduleConfig.serial.txd = SERIAL_PACKET_IO_TXD;
    }
    moduleConfig.serial.override_console_serial_port = false;
    moduleConfig.serial.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT;
    moduleConfig.serial.timeout = TIMEOUT;
    moduleConfig.serial.echo = 0;
    // No default, use value from config
    // moduleConfig.serial.baud = meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_19200;

    if (!moduleConfig.serial.enabled)
        return disable();

    if (firstTime) {
        // Interface with the serial peripheral from in here.
        LOG_INFO("Init serial peripheral interface");

        uint32_t baud = getBaudRate();
        Serial1.setPins(moduleConfig.serial.rxd, moduleConfig.serial.txd);
        Serial1.begin(baud, SERIAL_8N1);
        Serial1.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
        serialModuleRadio = new SerialModuleRadio();
        firstTime = 0;
    } else {
        int currentBufferCount = Serial1.available();
        if (currentBufferCount) {
            // data in the buffer.
            if (lastBufferCount != currentBufferCount) {
                // we have data, will wait until next poll to read it
                // in case more data arrives
                lastBufferCount = currentBufferCount;
            } else {
                // no data arrived since last poll, read this data
                // read one packet
                // stream.cpp/readBytes  arduinofruit library
                serialPayloadSize = Serial1.readBytes((uint8_t *)&inPacket, sizeof(meshtastic_serialPacket));
                if (!checkIfValidPacket(&inPacket)) {
                    LOG_DEBUG("Serial Module failed CRC on RX, numbytes: %d", serialPayloadSize);
                } else {
                    // checks passed, pass this packet on
                    LOG_DEBUG("Serial Module RX insert packet to mesh, numbytes: %d", serialPayloadSize);
                    insertSerialPacketToMesh(&inPacket);
                }
                lastBufferCount = 0; // zero out the lastBuffer count
            }
        } else {
            serialModuleRadio->checkTxQueue();
        }
    }
    return (50);
}

bool SerialModule::isValidConfig(const meshtastic_ModuleConfig_SerialConfig &config)
{
    return true;
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
    // return Throttle::isWithinTimespanMs(lastContactMsec, SERIAL_CONNECTION_TIMEOUT);
    //  we are not going to be able to determine if connected to another radio or not
    //  just always return true
    //  not sure where this function is called
    return true;
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

bool SerialModuleRadio::wantPacket(const meshtastic_MeshPacket *p)
{
    // never accept packets from module handler as we are relying on sampling the RX input
    return false;
}

void SerialModuleRadio::sendPacketOverSerial(meshtastic_MeshPacket *p)
{
    meshPacketToSerialPacket(p, &outPacket);
    // debug check
    if (!checkIfValidPacket(&outPacket)) {
        LOG_DEBUG("Serial Module failed CRC on TX");
    } else {
        if (Serial1.availableForWrite()) {
            LOG_DEBUG("Serial Module onSend TX packet of %d bytes", outPacket.header.size);
            Serial1.write((uint8_t *)&outPacket, outPacket.header.size);
        }
    }
}

void SerialModuleRadio::checkTxQueue()
{
    if (txQueue.empty())
        return; // nothing to do
    meshtastic_MeshPacket *p = txQueue.dequeue();
    LOG_DEBUG("Serial Module Onsend pulled packet from txQueue   from=0x%0x, to=0x%0x, packet_id=0x%0x", p->from, p->to, p->id);
    LOG_DEBUG("Serial Module num in txQueue: %d", txQueue.getMaxLen() - txQueue.getFree());
    sendPacketOverSerial(p);
    // free this packet
    packetPool.release(p);
}

/*
 Called from Router.cpp/Router::send
 Send this over the link
*/
void SerialModuleRadio::onSend(meshtastic_MeshPacket *p)
{

    LOG_DEBUG("Serial Module Onsend TX   from=0x%0x, to=0x%0x, packet_id=0x%0x", p->from, p->to, p->id);
    // check if RX buffer has data
    if (Serial1.peek() != -1) {
        // there is data in the buffer, could be that RX is active
        // copy the packet. Need to enqueue
        bool dropped = false;
        meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p);
        ErrorCode res = txQueue.enqueue(tosend, &dropped) ? ERRNO_OK : ERRNO_UNKNOWN;

        if (dropped) {
            txDrop++;
            LOG_DEBUG("Serial Module new drop, total dropped packets in txQueue: %d", txDrop);
        }
        if (res != ERRNO_OK) {
            // we weren't able to queue it, so we must drop it to prevent leaks
            // this packet was not sent
            LOG_DEBUG("Serial Module unable to send packet, txQueue error");
            packetPool.release(tosend);
        } else {
            LOG_DEBUG("Serial Module added packet to txQueue, num in txQueue: %d", txQueue.getMaxLen() - txQueue.getFree());
        }
        return;
    }
    sendPacketOverSerial(p);
}

/**
 * Handle a received mesh packet.
 *
 * @param mp The received mesh packet.
 * @return The processed message.
 */
ProcessMessage SerialModuleRadio::handleReceived(const meshtastic_MeshPacket &mp)
{
    // we are never going to handle packets when called from the Module handler
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

#else

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
    if (!p)
        return;
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
        if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO) {
            // in API mode we don't care about stuff from radio.
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
                meshtastic_Position *decoded = NULL;
                if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.portnum == ourPortNum) {
                    memset(&scratch, 0, sizeof(scratch));
                    if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Position_msg, &scratch)) {
                        decoded = &scratch;
                    }
                    // send position packet as WPL to the serial port
                    {
                        meshtastic_NodeInfoLite *senderNode = nodeDB->getMeshNode(getFrom(&mp));
                        const char *senderName = senderNode ? senderNode->long_name : "";
                        printWPL(outbuf, sizeof(outbuf), *decoded, senderName,
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
    if (gotwind && !Throttle::isWithinTimespanMs(lastAveraged, averageIntervalMillis) && velCount > 0 && dirCount > 0) {
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
#endif
#endif
