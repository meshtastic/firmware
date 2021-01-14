#include "SerialPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

#include <assert.h>

/*
    Designed for lora32 v1.0
        Manufacture Info: http://www.lilygo.cn/prod_view.aspx?TypeId=50003&Id=1133&FId=t3:50003:3
        Pin Mapping:      http://ae01.alicdn.com/kf/HTB1fLBcxkSWBuNjSszdq6zeSpXaJ.jpg
*/

#define RXD2 16
#define TXD2 17
#define SERIALPLUGIN_RX_BUFFER 128
#define SERIALPLUGIN_STRING_MAX Constants_DATA_PAYLOAD_LEN
#define SERIALPLUGIN_TIMEOUT 250
#define SERIALPLUGIN_BAUD 38400
#define SERIALPLUGIN_ENABLED 0
#define SERIALPLUGIN_ECHO 1

SerialPlugin *serialPlugin;
SerialPluginRadio *serialPluginRadio;

SerialPlugin::SerialPlugin() : concurrency::OSThread("SerialPlugin") {}

char serialStringChar[Constants_DATA_PAYLOAD_LEN];

int32_t SerialPlugin::runOnce()
{

#if SERIALPLUGIN_ENABLED == 0

    if (firstTime) {

        // Interface with the serial peripheral from in here.
        DEBUG_MSG("Initilizing serial peripheral interface\n");

        Serial2.begin(SERIALPLUGIN_BAUD, SERIAL_8N1, RXD2, TXD2);
        Serial2.setTimeout(SERIALPLUGIN_TIMEOUT); // Number of MS to wait to set the timeout for the string.
        Serial2.setRxBufferSize(SERIALPLUGIN_RX_BUFFER);

        serialPluginRadio = new SerialPluginRadio();

        DEBUG_MSG("Initilizing serial peripheral interface - Done\n");

        firstTime = 0;

    } else {
        // Interface with the serial peripheral from in here.
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

    p->decoded.data.payload.size = strlen(serialStringChar); // You must specify how many bytes are in the reply
    memcpy(p->decoded.data.payload.bytes, serialStringChar, p->decoded.data.payload.size);

    service.sendToMesh(p);
}

bool SerialPluginRadio::handleReceived(const MeshPacket &mp)
{
    auto &p = mp.decoded.data;
    DEBUG_MSG("* * * * * * * * * * Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n", nodeDB.getNodeNum(),
              mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

    /*
     * If SERIALPLUGIN_ECHO is true, then echo the packets that are sent out back to the TX
     * of the serial interface.
     */
    if (mp.from == nodeDB.getNodeNum()) {

        if (SERIALPLUGIN_ECHO) {

            // For some reason, we get the packet back twice when we send out of the radio.
            //   TODO: need to find out why.
            if (lastRxID != mp.id) {
                lastRxID = mp.id;
                DEBUG_MSG("* * Message came this device\n");
                Serial2.println("* * Message came this device");
            }
        }

    } else {
        DEBUG_MSG("* * Message came from the mesh\n");
        Serial2.println("* * Message came from the mesh");
    }

    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    // devicestate.rx_text_message = mp;
    // devicestate.has_rx_text_message = true;

    // Serial2.print(p.payload.bytes);

    // TODO: If packet came from this device, don't echo locally.

    return true; // Let others look at this message also if they want
}
