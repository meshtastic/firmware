#include "configuration.h"
#include "SerialModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include <Arduino.h>

#include <assert.h>

/*
    SerialModule
        A simple interface to send messages over the mesh network by sending strings
        over a serial port.

        Default is to use RX GPIO 16 and TX GPIO 17.

    Need help with this module? Post your question on the Meshtastic Discourse:
       https://meshtastic.discourse.group

    Basic Usage:

        1) Enable the module by setting serialmodule_enabled to 1.
        2) Set the pins (serialmodule_rxd / serialmodule_rxd) for your preferred RX and TX GPIO pins.
           On tbeam, recommend to use:
                RXD 35
                TXD 15
        3) Set serialmodule_timeout to the amount of time to wait before we consider
           your packet as "done".
        4) (Optional) In SerialModule.h set the port to PortNum_TEXT_MESSAGE_APP if you want to
           send messages to/from the general text message channel.
        5) Connect to your device over the serial interface at 38400 8N1.
        6) Send a packet up to 240 bytes in length. This will get relayed over the mesh network.
        7) (Optional) Set serialmodule_echo to 1 and any message you send out will be echoed back
           to your device.

    TODO (in this order):
        * Define a verbose RX mode to report on mesh and packet infomration.
            - This won't happen any time soon.

    KNOWN PROBLEMS
        * Until the module is initilized by the startup sequence, the TX pin is in a floating
          state. Device connected to that pin may see this as "noise".
        * Will not work on NRF and the Linux device targets.


*/

#define RXD2 16
#define TXD2 17
#define SERIALMODULE_RX_BUFFER 128
#define SERIALMODULE_STRING_MAX Constants_DATA_PAYLOAD_LEN
#define SERIALMODULE_TIMEOUT 250
#define SERIALMODULE_BAUD 38400
#define SERIALMODULE_ACK 1

SerialModule *serialModule;
SerialModuleRadio *serialModuleRadio;

SerialModule::SerialModule() : concurrency::OSThread("SerialModule") {}

char serialStringChar[Constants_DATA_PAYLOAD_LEN];

SerialModuleRadio::SerialModuleRadio() : SinglePortPlugin("SerialModuleRadio", PortNum_SERIAL_APP)
{
    // restrict to the admin channel for rx
    boundChannel = Channels::serialChannel;
}

int32_t SerialModule::runOnce()
{
#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.serialmodule_enabled = 1;
    // radioConfig.preferences.serialmodule_rxd = 35;
    // radioConfig.preferences.serialmodule_txd = 15;
    // radioConfig.preferences.serialmodule_timeout = 1000;
    // radioConfig.preferences.serialmodule_echo = 1;

    if (radioConfig.preferences.serialmodule_enabled) {

        if (firstTime) {

            // Interface with the serial peripheral from in here.
            DEBUG_MSG("Initializing serial peripheral interface\n");

            if (radioConfig.preferences.serialmodule_rxd && radioConfig.preferences.serialmodule_txd) {
                Serial2.begin(SERIALMODULE_BAUD, SERIAL_8N1, radioConfig.preferences.serialmodule_rxd,
                              radioConfig.preferences.serialmodule_txd);

            } else {
                Serial2.begin(SERIALMODULE_BAUD, SERIAL_8N1, RXD2, TXD2);
            }

            if (radioConfig.preferences.serialmodule_timeout) {
                Serial2.setTimeout(
                    radioConfig.preferences.serialmodule_timeout); // Number of MS to wait to set the timeout for the string.

            } else {
                Serial2.setTimeout(SERIALMODULE_TIMEOUT); // Number of MS to wait to set the timeout for the string.
            }

            Serial2.setRxBufferSize(SERIALMODULE_RX_BUFFER);

            serialModuleRadio = new SerialModuleRadio();

            firstTime = 0;

        } else {
            String serialString;

            while (Serial2.available()) {
                serialString = Serial2.readString();
                serialString.toCharArray(serialStringChar, Constants_DATA_PAYLOAD_LEN);

                serialModuleRadio->sendPayload();

                DEBUG_MSG("Received: %s\n", serialStringChar);
            }
        }

        return (10);
    } else {
        DEBUG_MSG("Serial Module Disabled\n");

        return (INT32_MAX);
    }
#else
    return INT32_MAX;
#endif
}

MeshPacket *SerialModuleRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void SerialModuleRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = SERIALMODULE_ACK;

    p->decoded.payload.size = strlen(serialStringChar); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, serialStringChar, p->decoded.payload.size);

    service.sendToMesh(p);
}

ProcessMessage SerialModuleRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (radioConfig.preferences.serialmodule_enabled) {

        auto &p = mp.decoded;
        // DEBUG_MSG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n",
        //          nodeDB.getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

        if (getFrom(&mp) == nodeDB.getNodeNum()) {

            /*
             * If radioConfig.preferences.serialmodule_echo is true, then echo the packets that are sent out back to the TX
             * of the serial interface.
             */
            if (radioConfig.preferences.serialmodule_echo) {

                // For some reason, we get the packet back twice when we send out of the radio.
                //   TODO: need to find out why.
                if (lastRxID != mp.id) {
                    lastRxID = mp.id;
                    // DEBUG_MSG("* * Message came this device\n");
                    // Serial2.println("* * Message came this device");
                    Serial2.printf("%s", p.payload.bytes);
                }
            }

        } else {

            if (radioConfig.preferences.serialmodule_mode == 0 || radioConfig.preferences.serialmodule_mode == 1) {
                // DEBUG_MSG("* * Message came from the mesh\n");
                // Serial2.println("* * Message came from the mesh");
                Serial2.printf("%s", p.payload.bytes);

            } else if (radioConfig.preferences.serialmodule_mode == 10) {
                /*
                 @jobionekabnoi
                    Add code here to handle what gets sent out to the serial interface.
                    Format it the way you want.
                 */
            }
        }

    } else {
        DEBUG_MSG("Serial Module Disabled\n");
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
