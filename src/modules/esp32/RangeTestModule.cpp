#include "RangeTestModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include <Arduino.h>
#include <FSCommon.h>
//#include <assert.h>

/*
    As a sender, I can send packets every n-seonds. These packets include an incramented PacketID.
    As a receiver, I can receive packets from multiple senders. These packets can be saved to the Filesystem.
*/

RangeTestModule *rangeTestModule;
RangeTestModuleRadio *rangeTestModuleRadio;

RangeTestModule::RangeTestModule() : concurrency::OSThread("RangeTestModule") {}

uint32_t packetSequence = 0;

#define SEC_PER_DAY 86400
#define SEC_PER_HOUR 3600
#define SEC_PER_MIN 60

int32_t RangeTestModule::runOnce()
{
#ifdef ARCH_ESP32

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    //moduleConfig.range_test.enabled = 1;
    //moduleConfig.range_test.sender = 30;
    // moduleConfig.range_test.save = 1;

    // Fixed position is useful when testing indoors.
    // radioConfig.preferences.fixed_position = 1;

    uint32_t senderHeartbeat = moduleConfig.range_test.sender * 1000;

    if (moduleConfig.range_test.enabled) {

        if (firstTime) {
            rangeTestModuleRadio = new RangeTestModuleRadio();

            firstTime = 0;

            if (moduleConfig.range_test.sender) {
                DEBUG_MSG("Initializing Range Test Module -- Sender\n");
                return (5000); // Sending first message 5 seconds after initilization.
            } else {
                DEBUG_MSG("Initializing Range Test Module -- Receiver\n");
                return (500);
            }

        } else {

            if (moduleConfig.range_test.sender) {
                // If sender
                DEBUG_MSG("Range Test Module - Sending heartbeat every %d ms\n", (senderHeartbeat));

                DEBUG_MSG("gpsStatus->getLatitude()     %d\n", gpsStatus->getLatitude());
                DEBUG_MSG("gpsStatus->getLongitude()    %d\n", gpsStatus->getLongitude());
                DEBUG_MSG("gpsStatus->getHasLock()      %d\n", gpsStatus->getHasLock());
                DEBUG_MSG("gpsStatus->getDOP()          %d\n", gpsStatus->getDOP());
                DEBUG_MSG("gpsStatus->getHasLock()      %d\n", gpsStatus->getHasLock());
                DEBUG_MSG("pref.fixed_position()        %d\n", config.position.fixed_position);

                // Only send packets if the channel is less than 25% utilized.
                if (airTime->channelUtilizationPercent() < 25) {
                    rangeTestModuleRadio->sendPayload();
                } else {
                    DEBUG_MSG("rangeTest - Channel utilization is >25 percent. Skipping this opportunity to send.\n");
                }

                return (senderHeartbeat);
            } else {
                // Otherwise, we're a receiver.

                return (500);
            }
            // TBD
        }

    } else {
        DEBUG_MSG("Range Test Module - Disabled\n");
    }

#endif
    return (INT32_MAX);
}

MeshPacket *RangeTestModuleRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void RangeTestModuleRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = true;

    packetSequence++;

    static char heartbeatString[MAX_RHPACKETLEN];
    snprintf(heartbeatString, sizeof(heartbeatString), "seq %u", packetSequence);

    p->decoded.payload.size = strlen(heartbeatString); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, heartbeatString, p->decoded.payload.size);

    service.sendToMesh(p);

    // TODO: Handle this better. We want to keep the phone awake otherwise it stops sending.
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);
}

ProcessMessage RangeTestModuleRadio::handleReceived(const MeshPacket &mp)
{
#ifdef ARCH_ESP32

    if (moduleConfig.range_test.enabled) {

        /*
            auto &p = mp.decoded;
            DEBUG_MSG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n",
                  nodeDB.getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);
        */

        if (getFrom(&mp) != nodeDB.getNodeNum()) {

            if (moduleConfig.range_test.save) {
                appendFile(mp);
            }

            /*
            NodeInfo *n = nodeDB.getNode(getFrom(&mp));

            DEBUG_MSG("-----------------------------------------\n");
            DEBUG_MSG("p.payload.bytes  \"%s\"\n", p.payload.bytes);
            DEBUG_MSG("p.payload.size   %d\n", p.payload.size);
            DEBUG_MSG("---- Received Packet:\n");
            DEBUG_MSG("mp.from          %d\n", mp.from);
            DEBUG_MSG("mp.rx_snr        %f\n", mp.rx_snr);
            DEBUG_MSG("mp.hop_limit     %d\n", mp.hop_limit);
            DEBUG_MSG("mp.decoded.position.latitude_i     %d\n", mp.decoded.position.latitude_i); // Depricated
            DEBUG_MSG("mp.decoded.position.longitude_i    %d\n", mp.decoded.position.longitude_i); // Depricated
            DEBUG_MSG("---- Node Information of Received Packet (mp.from):\n");
            DEBUG_MSG("n->user.long_name         %s\n", n->user.long_name);
            DEBUG_MSG("n->user.short_name        %s\n", n->user.short_name);
            DEBUG_MSG("n->user.macaddr           %X\n", n->user.macaddr);
            DEBUG_MSG("n->has_position           %d\n", n->has_position);
            DEBUG_MSG("n->position.latitude_i    %d\n", n->position.latitude_i);
            DEBUG_MSG("n->position.longitude_i   %d\n", n->position.longitude_i);
            DEBUG_MSG("---- Current device location information:\n");
            DEBUG_MSG("gpsStatus->getLatitude()     %d\n", gpsStatus->getLatitude());
            DEBUG_MSG("gpsStatus->getLongitude()    %d\n", gpsStatus->getLongitude());
            DEBUG_MSG("gpsStatus->getHasLock()      %d\n", gpsStatus->getHasLock());
            DEBUG_MSG("gpsStatus->getDOP()          %d\n", gpsStatus->getDOP());
            DEBUG_MSG("-----------------------------------------\n");
            */
        }

    } else {
        DEBUG_MSG("Range Test Module Disabled\n");
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool RangeTestModuleRadio::appendFile(const MeshPacket &mp)
{
    auto &p = mp.decoded;

    NodeInfo *n = nodeDB.getNode(getFrom(&mp));
    /*
        DEBUG_MSG("-----------------------------------------\n");
        DEBUG_MSG("p.payload.bytes  \"%s\"\n", p.payload.bytes);
        DEBUG_MSG("p.payload.size   %d\n", p.payload.size);
        DEBUG_MSG("---- Received Packet:\n");
        DEBUG_MSG("mp.from          %d\n", mp.from);
        DEBUG_MSG("mp.rx_snr        %f\n", mp.rx_snr);
        DEBUG_MSG("mp.hop_limit     %d\n", mp.hop_limit);
        // DEBUG_MSG("mp.decoded.position.latitude_i     %d\n", mp.decoded.position.latitude_i);  // Depricated
        // DEBUG_MSG("mp.decoded.position.longitude_i    %d\n", mp.decoded.position.longitude_i); // Depricated
        DEBUG_MSG("---- Node Information of Received Packet (mp.from):\n");
        DEBUG_MSG("n->user.long_name         %s\n", n->user.long_name);
        DEBUG_MSG("n->user.short_name        %s\n", n->user.short_name);
        DEBUG_MSG("n->user.macaddr           %X\n", n->user.macaddr);
        DEBUG_MSG("n->has_position           %d\n", n->has_position);
        DEBUG_MSG("n->position.latitude_i    %d\n", n->position.latitude_i);
        DEBUG_MSG("n->position.longitude_i   %d\n", n->position.longitude_i);
        DEBUG_MSG("---- Current device location information:\n");
        DEBUG_MSG("gpsStatus->getLatitude()     %d\n", gpsStatus->getLatitude());
        DEBUG_MSG("gpsStatus->getLongitude()    %d\n", gpsStatus->getLongitude());
        DEBUG_MSG("gpsStatus->getHasLock()      %d\n", gpsStatus->getHasLock());
        DEBUG_MSG("gpsStatus->getDOP()          %d\n", gpsStatus->getDOP());
        DEBUG_MSG("-----------------------------------------\n");
    */
    if (!FSBegin()) {
        DEBUG_MSG("An Error has occurred while mounting the filesystem\n");
        return 0;
    }

    if (FSCom.totalBytes() - FSCom.usedBytes() < 51200) {
        DEBUG_MSG("Filesystem doesn't have enough free space. Aborting write.\n");
        return 0;
    }

    // If the file doesn't exist, write the header.
    if (!FSCom.exists("/static/rangetest.csv")) {
        //--------- Write to file
        File fileToWrite = FSCom.open("/static/rangetest.csv", FILE_WRITE);

        if (!fileToWrite) {
            DEBUG_MSG("There was an error opening the file for writing\n");
            return 0;
        }

        // Print the CSV header
        if (fileToWrite.println(
                "time,from,sender name,sender lat,sender long,rx lat,rx long,rx elevation,rx snr,distance,hop limit,payload")) {
            DEBUG_MSG("File was written\n");
        } else {
            DEBUG_MSG("File write failed\n");
        }

        fileToWrite.close();
    }

    //--------- Append content to file
    File fileToAppend = FSCom.open("/static/rangetest.csv", FILE_APPEND);

    if (!fileToAppend) {
        DEBUG_MSG("There was an error opening the file for appending\n");
        return 0;
    }

    struct timeval tv;
    if (!gettimeofday(&tv, NULL)) {
        long hms = tv.tv_sec % SEC_PER_DAY;
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        int hour = hms / SEC_PER_HOUR;
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN

        fileToAppend.printf("%02d:%02d:%02d,", hour, min, sec); // Time
    } else {
        fileToAppend.printf("??:??:??,"); // Time
    }

    fileToAppend.printf("%d,", getFrom(&mp));                     // From
    fileToAppend.printf("%s,", n->user.long_name);                // Long Name
    fileToAppend.printf("%f,", n->position.latitude_i * 1e-7);    // Sender Lat
    fileToAppend.printf("%f,", n->position.longitude_i * 1e-7);   // Sender Long
    fileToAppend.printf("%f,", gpsStatus->getLatitude() * 1e-7);  // RX Lat
    fileToAppend.printf("%f,", gpsStatus->getLongitude() * 1e-7); // RX Long
    fileToAppend.printf("%d,", gpsStatus->getAltitude());         // RX Altitude

    fileToAppend.printf("%f,", mp.rx_snr); // RX SNR

    if (n->position.latitude_i && n->position.longitude_i && gpsStatus->getLatitude() && gpsStatus->getLongitude()) {
        float distance = GeoCoord::latLongToMeter(n->position.latitude_i * 1e-7, n->position.longitude_i * 1e-7,
                                                  gpsStatus->getLatitude() * 1e-7, gpsStatus->getLongitude() * 1e-7);
        fileToAppend.printf("%f,", distance); // Distance in meters
    } else {
        fileToAppend.printf("0,");
    }

    fileToAppend.printf("%d,", mp.hop_limit); // Packet Hop Limit

    // TODO: If quotes are found in the payload, it has to be escaped.
    fileToAppend.printf("\"%s\"\n", p.payload.bytes);
    fileToAppend.close();

    return 1;
}
