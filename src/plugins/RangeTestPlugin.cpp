#include "RangeTestPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>
#include <assert.h>
//#include <iostream>
//#include <sstream>
//#include <string>

//#undef str


#define RANGETESTPLUGIN_ENABLED 1
#define RANGETESTPLUGIN_SENDER 20000

RangeTestPlugin *rangeTestPlugin;
RangeTestPluginRadio *rangeTestPluginRadio;

RangeTestPlugin::RangeTestPlugin() : concurrency::OSThread("RangeTestPlugin") {}

// char serialStringChar[Constants_DATA_PAYLOAD_LEN];

int32_t RangeTestPlugin::runOnce()
{
#ifndef NO_ESP32

    if (RANGETESTPLUGIN_ENABLED) {

        if (firstTime) {

            // Interface with the serial peripheral from in here.
            DEBUG_MSG("Initializing Range Test Plugin\n");

            rangeTestPluginRadio = new RangeTestPluginRadio();

            firstTime = 0;

        } else {

            // If sender
            if (RANGETESTPLUGIN_SENDER) {

                return (RANGETESTPLUGIN_SENDER);

                // Otherwise, we're a receiver.
            } else {

                rangeTestPluginRadio->sendPayload();
                return (500);
            }
            // TBD
        }

    } else {
        DEBUG_MSG("Range Test Plugin - Disabled\n");

        return (INT32_MAX);
    }

#endif
}

MeshPacket *RangeTestPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void RangeTestPluginRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = true;

    uint16_t packetSequence = 123456;
    static char heartbeatString[20];
    snprintf(heartbeatString, sizeof(heartbeatString), "Seq: %d%%", packetSequence);

    p->decoded.data.payload.size = strlen(heartbeatString); // You must specify how many bytes are in the reply
    memcpy(p->decoded.data.payload.bytes, heartbeatString, p->decoded.data.payload.size);

    service.sendToMesh(p);
}

bool RangeTestPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (RANGETESTPLUGIN_ENABLED) {

        auto &p = mp.decoded.data;
        // DEBUG_MSG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n",
        //          nodeDB.getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

        if (mp.from != nodeDB.getNodeNum()) {

            // DEBUG_MSG("* * Message came from the mesh\n");
            // Serial2.println("* * Message came from the mesh");
            Serial2.printf("%s", p.payload.bytes);
            p.payload.size;
            gpsStatus->getLatitude();
            gpsStatus->getLongitude();
            gpsStatus->getHasLock();
            gpsStatus->getDOP();
            mp.rx_snr;
            mp.hop_limit;
            mp.decoded.position.latitude_i;
            mp.decoded.position.longitude_i;
        }

    } else {
        DEBUG_MSG("Range Test Plugin Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
