#include "SerialPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

#include <assert.h>

/*
    SerialPlugin
        An overly simplistic interface to send messages over the mesh network by sending strings
        over a serial port.

    Originally designed for lora32 v1.0
        Manufacture Info: http://www.lilygo.cn/prod_view.aspx?TypeId=50003&Id=1133&FId=t3:50003:3
        Pin Mapping:      http://ae01.alicdn.com/kf/HTB1fLBcxkSWBuNjSszdq6zeSpXaJ.jpg

    This will probably and most likely work on other esp32 devices, given possible change the RX/TX
        selection.

    Need help with this plugin? Post your question on the Meshtastic Discourse:
       https://meshtastic.discourse.group

    Basic Usage:

        1) Enable the plugin by setting SERIALPLUGIN_ENABLED to 1.
        2) Set the pins (RXD2 / TXD2) for your preferred RX and TX GPIO pins.
           On tbeam, recommend to use:
                #define RXD2 35
                #define TXD2 15
        3) Set SERIALPLUGIN_TIMEOUT to the amount of time to wait before we consider
           your packet as "done".
        4) (Optional) In SerialPlugin.h set the port to PortNum_TEXT_MESSAGE_APP if you want to
           send messages to/from the general text message channel.
        5) Connect to your device over the serial interface at 38400 8N1.
        6) Send a packet up to 240 bytes in length. This will get relayed over the mesh network.
        7) (Optional) Set SERIALPLUGIN_ECHO to 1 and any message you send out will be echoed back
           to your device.

    TODO (in this order):
        * Once protobufs regenerated with the new port, update SerialPlugin.h
        * Ensure this works on a tbeam
        * Define a verbose RX mode to report on mesh and packet infomration.
            - This won't happen any time soon.

    KNOWN PROBLEMS
        * Until the plugin is initilized by the startup sequence, the TX pin is in a floating
          state. Device connected to that pin may see this as "noise".
        * Will not work on NRF and the Linux device targets.


*/

#define RXD2 16
#define TXD2 17
#define SERIALPLUGIN_RX_BUFFER 128
#define SERIALPLUGIN_STRING_MAX Constants_DATA_PAYLOAD_LEN
#define SERIALPLUGIN_TIMEOUT 250
#define SERIALPLUGIN_BAUD 38400
#define SERIALPLUGIN_ENABLED 1
#define SERIALPLUGIN_ECHO 0
#define SERIALPLUGIN_ACK 0

SerialPlugin *serialPlugin;
SerialPluginRadio *serialPluginRadio;

SerialPlugin::SerialPlugin() : concurrency::OSThread("SerialPlugin") {}

char serialStringChar[Constants_DATA_PAYLOAD_LEN];

int32_t SerialPlugin::runOnce()
{

#if SERIALPLUGIN_ENABLED == 1

    if (firstTime) {

        // Interface with the serial peripheral from in here.
        DEBUG_MSG("Initilizing serial peripheral interface\n");

        Serial2.begin(SERIALPLUGIN_BAUD, SERIAL_8N1, RXD2, TXD2);
        Serial2.setTimeout(SERIALPLUGIN_TIMEOUT); // Number of MS to wait to set the timeout for the string.
        Serial2.setRxBufferSize(SERIALPLUGIN_RX_BUFFER);

        serialPluginRadio = new SerialPluginRadio();

        firstTime = 0;

    } else {
        String serialString;

        while (Serial2.available()) {
            serialString = Serial2.readString();
            serialString.toCharArray(serialStringChar, Constants_DATA_PAYLOAD_LEN);

            serialPluginRadio->sendPayload();

            DEBUG_MSG("Received: %s\n", serialStringChar);
        }
    }

    return (10);
#else
    DEBUG_MSG("Serial Plugin Disabled\n");

    return (INT32_MAX);
#endif
}

MeshPacket *SerialPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void SerialPluginRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = SERIALPLUGIN_ACK;

    p->decoded.data.payload.size = strlen(serialStringChar); // You must specify how many bytes are in the reply
    memcpy(p->decoded.data.payload.bytes, serialStringChar, p->decoded.data.payload.size);

    service.sendToMesh(p);
}

bool SerialPluginRadio::handleReceived(const MeshPacket &mp)
{
    auto &p = mp.decoded.data;
    // DEBUG_MSG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n", nodeDB.getNodeNum(),
    //          mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

    if (mp.from == nodeDB.getNodeNum()) {

        /*
         * If SERIALPLUGIN_ECHO is true, then echo the packets that are sent out back to the TX
         * of the serial interface.
         */
        if (SERIALPLUGIN_ECHO) {

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
        // DEBUG_MSG("* * Message came from the mesh\n");
        // Serial2.println("* * Message came from the mesh");
        Serial2.printf("%s", p.payload.bytes);
    }

    return true; // Let others look at this message also if they want
}
