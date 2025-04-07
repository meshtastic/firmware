/**
 * @file RangeTestModule.cpp
 * @brief Implementation of the RangeTestModule class and RangeTestModuleRadio class.
 *
 * As a sender, this module sends packets every n seconds with an incremented PacketID.
 * As a receiver, this module receives packets from multiple senders and saves them to the Filesystem.
 *
 * The RangeTestModule class is an OSThread that runs the module.
 * The RangeTestModuleRadio class handles sending and receiving packets.
 */
#include "RangeTestModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "SPILock.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include <Arduino.h>
#include <Throttle.h>

RangeTestModule *rangeTestModule;
RangeTestModuleRadio *rangeTestModuleRadio;

RangeTestModule::RangeTestModule() : concurrency::OSThread("RangeTest") {}

uint32_t packetSequence = 0;

int32_t RangeTestModule::runOnce()
{
#if defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_PORTDUINO)

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.range_test.enabled = 1;
    // moduleConfig.range_test.sender = 30;
    // moduleConfig.range_test.save = 1;

    // Fixed position is useful when testing indoors.
    // config.position.fixed_position = 1;

    uint32_t senderHeartbeat = moduleConfig.range_test.sender * 1000;

    if (moduleConfig.range_test.enabled) {

        if (firstTime) {
            rangeTestModuleRadio = new RangeTestModuleRadio();

            firstTime = 0;

            if (moduleConfig.range_test.sender) {
                LOG_INFO("Init Range Test Module -- Sender");
                started = millis(); // make a note of when we started
                return (5000);      // Sending first message 5 seconds after initialization.
            } else {
                LOG_INFO("Init Range Test Module -- Receiver");
                return disable();
                // This thread does not need to run as a receiver
            }
        } else {

            if (moduleConfig.range_test.sender) {
                // If sender
                LOG_INFO("Range Test Module - Sending heartbeat every %d ms", (senderHeartbeat));

                LOG_INFO("gpsStatus->getLatitude()     %d", gpsStatus->getLatitude());
                LOG_INFO("gpsStatus->getLongitude()    %d", gpsStatus->getLongitude());
                LOG_INFO("gpsStatus->getHasLock()      %d", gpsStatus->getHasLock());
                LOG_INFO("gpsStatus->getDOP()          %d", gpsStatus->getDOP());
                LOG_INFO("fixed_position()             %d", config.position.fixed_position);

                // Only send packets if the channel is less than 25% utilized.
                if (airTime->isTxAllowedChannelUtil(true)) {
                    rangeTestModuleRadio->sendPayload();
                }

                // If we have been running for more than 8 hours, turn module back off
                if (!Throttle::isWithinTimespanMs(started, 28800000)) {
                    LOG_INFO("Range Test Module - Disable after 8 hours");
                    return disable();
                } else {
                    return (senderHeartbeat);
                }
            } else {
                return disable();
                // This thread does not need to run as a receiver
            }
        }
    } else {
        LOG_INFO("Range Test Module - Disabled");
    }

#endif
    return disable();
}

/**
 * Sends a payload to a specified destination node.
 *
 * @param dest The destination node number.
 * @param wantReplies Whether or not to request replies from the destination node.
 */
void RangeTestModuleRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->decoded.want_response = wantReplies;
    p->hop_limit = 0;
    p->want_ack = false;

    packetSequence++;

    static char heartbeatString[MAX_LORA_PAYLOAD_LEN + 1];
    snprintf(heartbeatString, sizeof(heartbeatString), "seq %u", packetSequence);

    p->decoded.payload.size = strlen(heartbeatString); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, heartbeatString, p->decoded.payload.size);

    service->sendToMesh(p);

    // TODO: Handle this better. We want to keep the phone awake otherwise it stops sending.
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);
}

ProcessMessage RangeTestModuleRadio::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_PORTDUINO)

    if (moduleConfig.range_test.enabled) {

        /*
            auto &p = mp.decoded;
            LOG_DEBUG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s",
                  LOG_INFO.getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);
        */

        if (!isFromUs(&mp)) {

            if (moduleConfig.range_test.save) {
                appendFile(mp);
            }

            /*
            NodeInfoLite *n = nodeDB->getMeshNode(getFrom(&mp));

            LOG_DEBUG("-----------------------------------------");
            LOG_DEBUG("p.payload.bytes  \"%s\"", p.payload.bytes);
            LOG_DEBUG("p.payload.size   %d", p.payload.size);
            LOG_DEBUG("---- Received Packet:");
            LOG_DEBUG("mp.from          %d", mp.from);
            LOG_DEBUG("mp.rx_snr        %f", mp.rx_snr);
            LOG_DEBUG("mp.hop_limit     %d", mp.hop_limit);
            LOG_DEBUG("---- Node Information of Received Packet (mp.from):");
            LOG_DEBUG("n->user.long_name         %s", n->user.long_name);
            LOG_DEBUG("n->user.short_name        %s", n->user.short_name);
            LOG_DEBUG("n->has_position           %d", n->has_position);
            LOG_DEBUG("n->position.latitude_i    %d", n->position.latitude_i);
            LOG_DEBUG("n->position.longitude_i   %d", n->position.longitude_i);
            LOG_DEBUG("---- Current device location information:");
            LOG_DEBUG("gpsStatus->getLatitude()     %d", gpsStatus->getLatitude());
            LOG_DEBUG("gpsStatus->getLongitude()    %d", gpsStatus->getLongitude());
            LOG_DEBUG("gpsStatus->getHasLock()      %d", gpsStatus->getHasLock());
            LOG_DEBUG("gpsStatus->getDOP()          %d", gpsStatus->getDOP());
            LOG_DEBUG("-----------------------------------------");
            */
        }
    } else {
        LOG_INFO("Range Test Module Disabled");
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool RangeTestModuleRadio::appendFile(const meshtastic_MeshPacket &mp)
{
#ifdef ARCH_ESP32
    auto &p = mp.decoded;

    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(getFrom(&mp));
    /*
        LOG_DEBUG("-----------------------------------------");
        LOG_DEBUG("p.payload.bytes  \"%s\"", p.payload.bytes);
        LOG_DEBUG("p.payload.size   %d", p.payload.size);
        LOG_DEBUG("---- Received Packet:");
        LOG_DEBUG("mp.from          %d", mp.from);
        LOG_DEBUG("mp.rx_snr        %f", mp.rx_snr);
        LOG_DEBUG("mp.hop_limit     %d", mp.hop_limit);
        LOG_DEBUG("---- Node Information of Received Packet (mp.from):");
        LOG_DEBUG("n->user.long_name         %s", n->user.long_name);
        LOG_DEBUG("n->user.short_name        %s", n->user.short_name);
        LOG_DEBUG("n->has_position           %d", n->has_position);
        LOG_DEBUG("n->position.latitude_i    %d", n->position.latitude_i);
        LOG_DEBUG("n->position.longitude_i   %d", n->position.longitude_i);
        LOG_DEBUG("---- Current device location information:");
        LOG_DEBUG("gpsStatus->getLatitude()     %d", gpsStatus->getLatitude());
        LOG_DEBUG("gpsStatus->getLongitude()    %d", gpsStatus->getLongitude());
        LOG_DEBUG("gpsStatus->getHasLock()      %d", gpsStatus->getHasLock());
        LOG_DEBUG("gpsStatus->getDOP()          %d", gpsStatus->getDOP());
        LOG_DEBUG("-----------------------------------------");
    */
    concurrency::LockGuard g(spiLock);
    if (!FSBegin()) {
        LOG_DEBUG("An Error has occurred while mounting the filesystem");
        return 0;
    }

    if (FSCom.totalBytes() - FSCom.usedBytes() < 51200) {
        LOG_DEBUG("Filesystem doesn't have enough free space. Aborting write");
        return 0;
    }

    FSCom.mkdir("/static");

    // If the file doesn't exist, write the header.
    if (!FSCom.exists("/static/rangetest.csv")) {
        //--------- Write to file
        File fileToWrite = FSCom.open("/static/rangetest.csv", FILE_WRITE);

        if (!fileToWrite) {
            LOG_ERROR("There was an error opening the file for writing");
            return 0;
        }

        // Print the CSV header
        if (fileToWrite.println(
                "time,from,sender name,sender lat,sender long,rx lat,rx long,rx elevation,rx snr,distance,hop limit,payload")) {
            LOG_INFO("File was written");
        } else {
            LOG_ERROR("File write failed");
        }
        fileToWrite.flush();
        fileToWrite.close();
    }

    //--------- Append content to file
    File fileToAppend = FSCom.open("/static/rangetest.csv", FILE_APPEND);

    if (!fileToAppend) {
        LOG_ERROR("There was an error opening the file for appending");
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

    fileToAppend.printf("%d,", getFrom(&mp));                   // From
    fileToAppend.printf("%s,", n->user.long_name);              // Long Name
    fileToAppend.printf("%f,", n->position.latitude_i * 1e-7);  // Sender Lat
    fileToAppend.printf("%f,", n->position.longitude_i * 1e-7); // Sender Long
    if (gpsStatus->getIsConnected() || config.position.fixed_position) {
        fileToAppend.printf("%f,", gpsStatus->getLatitude() * 1e-7);  // RX Lat
        fileToAppend.printf("%f,", gpsStatus->getLongitude() * 1e-7); // RX Long
        fileToAppend.printf("%d,", gpsStatus->getAltitude());         // RX Altitude
    } else {
        // When the phone API is in use, the node info will be updated with position
        meshtastic_NodeInfoLite *us = nodeDB->getMeshNode(nodeDB->getNodeNum());
        fileToAppend.printf("%f,", us->position.latitude_i * 1e-7);  // RX Lat
        fileToAppend.printf("%f,", us->position.longitude_i * 1e-7); // RX Long
        fileToAppend.printf("%d,", us->position.altitude);           // RX Altitude
    }

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
    fileToAppend.flush();
    fileToAppend.close();
#endif

    return 1;
}