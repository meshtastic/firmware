#include "SerialModule.h"
#include "MeshService.h"
#include "NMEAWPL.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

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
        * Define a verbose RX mode to report on mesh and packet infomration.
            - This won't happen any time soon.

    KNOWN PROBLEMS
        * Until the module is initilized by the startup sequence, the TX pin is in a floating
          state. Device connected to that pin may see this as "noise".
        * Will not work on T-Echo and the Linux device targets.


*/

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52)) && !defined(TTGO_T_ECHO) && !defined(CONFIG_IDF_TARGET_ESP32S2) &&              \
    !defined(CONFIG_IDF_TARGET_ESP32C3)

#define RX_BUFFER 128
#define TIMEOUT 250
#define BAUD 38400
#define ACK 1

// API: Defaulting to the formerly removed phone_timeout_secs value of 15 minutes
#define SERIAL_CONNECTION_TIMEOUT (15 * 60) * 1000UL

SerialModule *serialModule;
SerialModuleRadio *serialModuleRadio;

SerialModule::SerialModule() : StreamAPI(&Serial2), concurrency::OSThread("SerialModule") {}

char serialBytes[meshtastic_Constants_DATA_PAYLOAD_LEN];
size_t serialPayloadSize;

SerialModuleRadio::SerialModuleRadio() : MeshModule("SerialModuleRadio")
{

    switch (moduleConfig.serial.mode) {
    case meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG:
        ourPortNum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        break;
    case meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA:
        ourPortNum = meshtastic_PortNum_POSITION_APP;
        break;
    default:
        ourPortNum = meshtastic_PortNum_SERIAL_APP;
        // restrict to the serial channel for rx
        boundChannel = Channels::serialChannel;
        break;
    }
}

// For the serial2 port we can't really detect if any client is on the other side, so instead just look for recent messages
bool SerialModule::checkIsConnected()
{
    uint32_t now = millis();
    return (now - lastContactMsec) < SERIAL_CONNECTION_TIMEOUT;
}

int32_t SerialModule::runOnce()
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.serial.enabled = 1;
    // moduleConfig.serial.rxd = 35;
    // moduleConfig.serial.txd = 15;
    // moduleConfig.serial.timeout = 1000;
    // moduleConfig.serial.echo = 1;

    if (moduleConfig.serial.enabled && moduleConfig.serial.rxd && moduleConfig.serial.txd) {

        if (firstTime) {

            // Interface with the serial peripheral from in here.
            LOG_INFO("Initializing serial peripheral interface\n");

            uint32_t baud = 0;

            if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_DEFAULT) {
                baud = 38400;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_110) {
                baud = 110;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_300) {
                baud = 300;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_600) {
                baud = 600;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_1200) {
                baud = 1200;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_2400) {
                baud = 2400;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_4800) {
                baud = 4800;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_9600) {
                baud = 9600;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_19200) {
                baud = 19200;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_38400) {
                baud = 38400;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_57600) {
                baud = 57600;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_115200) {
                baud = 115200;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_230400) {
                baud = 230400;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_460800) {
                baud = 460800;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_576000) {
                baud = 576000;

            } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_921600) {
                baud = 921600;
            }

#ifdef ARCH_ESP32
            Serial2.setRxBufferSize(RX_BUFFER);

            if (moduleConfig.serial.rxd && moduleConfig.serial.txd) {
                Serial2.begin(baud, SERIAL_8N1, moduleConfig.serial.rxd, moduleConfig.serial.txd);
            }
#else
            if (moduleConfig.serial.rxd && moduleConfig.serial.txd)
                Serial2.setPins(moduleConfig.serial.rxd, moduleConfig.serial.txd);

            Serial2.begin(baud, SERIAL_8N1);

#endif
            if (moduleConfig.serial.timeout) {
                Serial2.setTimeout(moduleConfig.serial.timeout); // Number of MS to wait to set the timeout for the string.
            } else {
                Serial2.setTimeout(TIMEOUT); // Number of MS to wait to set the timeout for the string.
            }

            serialModuleRadio = new SerialModuleRadio();

            firstTime = 0;

            // in API mode send rebooted sequence
            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO) {
                emitRebooted();
            }

        } else {

            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO) {
                return runOncePart();
            } else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA) {
                // in NMEA mode send out GGA every 2 seconds, Don't read from Port
                if (millis() - lastNmeaTime > 2000) {
                    lastNmeaTime = millis();
                    printGGA(outbuf, sizeof(outbuf), nodeDB.getNode(myNodeInfo.my_node_num)->position);
                    Serial2.printf("%s", outbuf);
                }
            } else {
                while (Serial2.available()) {
                    serialPayloadSize = Serial2.readBytes(serialBytes, meshtastic_Constants_DATA_PAYLOAD_LEN);
                    serialModuleRadio->sendPayload();
                }
            }
        }

        return (10);
    } else {
        return disable();
    }
}

meshtastic_MeshPacket *SerialModuleRadio::allocReply()
{
    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void SerialModuleRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    meshtastic_Channel *ch = (boundChannel != NULL) ? &channels.getByName(boundChannel) : NULL;
    meshtastic_MeshPacket *p = allocReply();
    p->to = dest;
    if (ch != NULL) {
        p->channel = ch->index;
    }
    p->decoded.want_response = wantReplies;

    p->want_ack = ACK;

    p->decoded.payload.size = serialPayloadSize; // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, serialBytes, p->decoded.payload.size);

    service.sendToMesh(p);
}

ProcessMessage SerialModuleRadio::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (moduleConfig.serial.enabled) {
        if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO) {
            // in API mode we don't care about stuff from radio.
            return ProcessMessage::CONTINUE;
        }

        auto &p = mp.decoded;
        // LOG_DEBUG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n",
        //          nodeDB.getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

        if (getFrom(&mp) == nodeDB.getNodeNum()) {

            /*
             * If moduleConfig.serial.echo is true, then echo the packets that are sent out
             * back to the TX of the serial interface.
             */
            if (moduleConfig.serial.echo) {

                // For some reason, we get the packet back twice when we send out of the radio.
                //   TODO: need to find out why.
                if (lastRxID != mp.id) {
                    lastRxID = mp.id;
                    // LOG_DEBUG("* * Message came this device\n");
                    // Serial2.println("* * Message came this device");
                    Serial2.printf("%s", p.payload.bytes);
                }
            }

        } else {

            if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT ||
                moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_SIMPLE) {
                Serial2.printf("%s", p.payload.bytes);
            } else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG) {
                meshtastic_NodeInfo *node = nodeDB.getNode(getFrom(&mp));
                String sender = (node && node->has_user) ? node->user.short_name : "???";
                Serial2.println();
                Serial2.printf("%s: %s", sender, p.payload.bytes);
                Serial2.println();
            } else if (moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA) {
                // Decode the Payload some more
                meshtastic_Position scratch;
                meshtastic_Position *decoded = NULL;
                if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.portnum == ourPortNum) {
                    memset(&scratch, 0, sizeof(scratch));
                    if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Position_msg, &scratch)) {
                        decoded = &scratch;
                    }
                    // send position packet as WPL to the serial port
                    printWPL(outbuf, sizeof(outbuf), *decoded, nodeDB.getNode(getFrom(&mp))->user.long_name);
                    Serial2.printf("%s", outbuf);
                }
            }
        }
    }
    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
#endif
