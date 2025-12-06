#if !MESHTASTIC_EXCLUDE_GPS
#include "PositionModule.h"
#include "Default.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "TypeConversions.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "main.h"
#include "mesh/compression/unishox2.h"
#include "meshUtils.h"
#include "meshtastic/atak.pb.h"
#include "sleep.h"
#include "target_specific.h"
#include <Throttle.h>

PositionModule *positionModule;

PositionModule::PositionModule()
    : ProtobufModule("position", meshtastic_PortNum_POSITION_APP, &meshtastic_Position_msg), concurrency::OSThread("Position")
{
    precision = 0;        // safe starting value
    isPromiscuous = true; // We always want to update our nodedb, even if we are sniffing on others
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);

    if (config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) {
        setIntervalFromNow(setStartDelay());
    }

    // Power saving trackers should clear their position on startup to avoid waking up and sending a stale position
    if ((config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
         config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) &&
        config.power.is_power_saving) {
        LOG_DEBUG("Clear position on startup for sleepy tracker (ー。ー) zzz");
        nodeDB->clearLocalPosition();
    }
}

bool PositionModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Position *pptr)
{
    auto p = *pptr;

    // If inbound message is a replay (or spoof!) of our own messages, we shouldn't process
    // (why use second-hand sources for our own data?)

    // FIXME this can in fact happen with packets sent from EUD (src=RX_SRC_USER)
    // to set fixed location, EUD-GPS location or just the time (see also issue #900)
    bool isLocal = false;
    if (isFromUs(&mp)) {
        isLocal = true;
        if (config.position.fixed_position) {
            LOG_DEBUG("Ignore incoming position update from myself except for time, because position.fixed_position is true");

#ifdef T_WATCH_S3
            // Since we return early if position.fixed_position is true, set the T-Watch's RTC to the time received from the
            // client device here
            if (p.time && channels.getByIndex(mp.channel).role == meshtastic_Channel_Role_PRIMARY) {
                trySetRtc(p, isLocal, true);
            }
#endif

            nodeDB->setLocalPosition(p, true);
            return false;
        } else {
            LOG_DEBUG("Incoming update from MYSELF");
            nodeDB->setLocalPosition(p);
        }
    }

    // Log packet size and data fields
    LOG_DEBUG("POSITION node=%08x l=%d lat=%d lon=%d msl=%d hae=%d geo=%d pdop=%d hdop=%d vdop=%d siv=%d fxq=%d fxt=%d pts=%d "
              "time=%d",
              getFrom(&mp), mp.decoded.payload.size, p.latitude_i, p.longitude_i, p.altitude, p.altitude_hae,
              p.altitude_geoidal_separation, p.PDOP, p.HDOP, p.VDOP, p.sats_in_view, p.fix_quality, p.fix_type, p.timestamp,
              p.time);

    if (p.time && channels.getByIndex(mp.channel).role == meshtastic_Channel_Role_PRIMARY) {
        bool force = false;

#ifdef T_WATCH_S3
        // The T-Watch appears to "pause" its RTC when shut down, such that the time it reads upon powering on is the same as when
        // it was shut down. So we need to force the update here, since otherwise RTC::perhapsSetRTC will ignore it because it
        // will always be an equivalent or lesser RTCQuality (RTCQualityNTP or RTCQualityNet).
        force = true;
#endif
        // Set from phone RTC Quality to RTCQualityNTP since it should be approximately so
        trySetRtc(p, isLocal, force);
    }

    nodeDB->updatePosition(getFrom(&mp), p);
    if (channels.getByIndex(mp.channel).settings.has_module_settings) {
        precision = channels.getByIndex(mp.channel).settings.module_settings.position_precision;
    } else if (channels.getByIndex(mp.channel).role == meshtastic_Channel_Role_PRIMARY) {
        precision = 32;
    } else {
        precision = 0;
    }

    return false; // Let others look at this message also if they want
}

void PositionModule::alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_Position *p)
{
    // Phone position packets need to be truncated to the channel precision
    if (isFromUs(&mp) && (precision < 32 && precision > 0)) {
        LOG_DEBUG("Truncate phone position to channel precision %i", precision);
        p->latitude_i = p->latitude_i & (UINT32_MAX << (32 - precision));
        p->longitude_i = p->longitude_i & (UINT32_MAX << (32 - precision));

        // We want the imprecise position to be the middle of the possible location, not
        p->latitude_i += (1 << (31 - precision));
        p->longitude_i += (1 << (31 - precision));

        mp.decoded.payload.size =
            pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes), &meshtastic_Position_msg, p);
    }
}

void PositionModule::trySetRtc(meshtastic_Position p, bool isLocal, bool forceUpdate)
{
    if (hasQualityTimesource() && !isLocal) {
        LOG_DEBUG("Ignore time from mesh because we have a GPS, RTC, or Phone/NTP time source in the past day");
        return;
    }
    if (!isLocal && p.location_source < meshtastic_Position_LocSource_LOC_INTERNAL) {
        LOG_DEBUG("Ignore time from mesh because it has a unknown or manual source");
        return;
    }
    struct timeval tv;
    uint32_t secs = p.time;

    tv.tv_sec = secs;
    tv.tv_usec = 0;

    perhapsSetRTC(isLocal ? RTCQualityNTP : RTCQualityFromNet, &tv, forceUpdate);
}

bool PositionModule::hasQualityTimesource()
{
    bool setFromPhoneOrNtpToday =
        lastSetFromPhoneNtpOrGps == 0 ? false : Throttle::isWithinTimespanMs(lastSetFromPhoneNtpOrGps, SEC_PER_DAY * 1000UL);
#if MESHTASTIC_EXCLUDE_GPS
    bool hasGpsOrRtc = (rtc_found.address != ScanI2C::ADDRESS_NONE.address);
#else
    bool hasGpsOrRtc = hasGPS() || (rtc_found.address != ScanI2C::ADDRESS_NONE.address);
#endif
    return hasGpsOrRtc || setFromPhoneOrNtpToday;
}

bool PositionModule::hasGPS()
{
#if MESHTASTIC_EXCLUDE_GPS
    return false;
#else
    return gps && gps->isConnected();
#endif
}

// Allocate a packet with our position data if we have one
meshtastic_MeshPacket *PositionModule::allocPositionPacket()
{
    if (precision == 0) {
        LOG_DEBUG("Skip location send because precision is set to 0!");
        return nullptr;
    }

    meshtastic_NodeInfoLite *node = service->refreshLocalMeshNode(); // should guarantee there is now a position
    assert(node->has_position);

    // configuration of POSITION packet
    //   consider making this a function argument?
    uint32_t pos_flags = config.position.position_flags;

    // Populate a Position struct with ONLY the requested fields
    meshtastic_Position p = meshtastic_Position_init_default; //   Start with an empty structure
    // if localPosition is totally empty, put our last saved position (lite) in there
    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0) {
        nodeDB->setLocalPosition(TypeConversions::ConvertToPosition(node->position));
    }
    localPosition.seq_number++;

    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0) {
        LOG_WARN("Skip position send because lat/lon are zero!");
        return nullptr;
    }

    // lat/lon are unconditionally included - IF AVAILABLE!
    LOG_DEBUG("Send location with precision %i", precision);
    if (precision < 32 && precision > 0) {
        p.latitude_i = localPosition.latitude_i & (UINT32_MAX << (32 - precision));
        p.longitude_i = localPosition.longitude_i & (UINT32_MAX << (32 - precision));

        // We want the imprecise position to be the middle of the possible location, not
        p.latitude_i += (1 << (31 - precision));
        p.longitude_i += (1 << (31 - precision));
    } else {
        p.latitude_i = localPosition.latitude_i;
        p.longitude_i = localPosition.longitude_i;
    }
    p.precision_bits = precision;
    p.has_latitude_i = true;
    p.has_longitude_i = true;
    // Always use NTP / GPS time if available
    if (getValidTime(RTCQualityNTP) > 0) {
        p.time = getValidTime(RTCQualityNTP);
    } else if (rtc_found.address != ScanI2C::ADDRESS_NONE.address) {
        LOG_INFO("Use RTC time for position");
        p.time = getValidTime(RTCQualityDevice);
    } else if (getRTCQuality() < RTCQualityNTP) {
        LOG_INFO("Strip low RTCQuality (%d) time from position", getRTCQuality());
        p.time = 0;
    }

    if (config.position.fixed_position) {
        p.location_source = meshtastic_Position_LocSource_LOC_MANUAL;
    } else {
        p.location_source = localPosition.location_source;
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE) {
        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL) {
            p.altitude = localPosition.altitude;
            p.has_altitude = true;
        } else {
            p.altitude_hae = localPosition.altitude_hae;
            p.has_altitude_hae = true;
        }

        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION) {
            p.altitude_geoidal_separation = localPosition.altitude_geoidal_separation;
            p.has_altitude_geoidal_separation = true;
        }
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_DOP) {
        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_HVDOP) {
            p.HDOP = localPosition.HDOP;
            p.VDOP = localPosition.VDOP;
        } else
            p.PDOP = localPosition.PDOP;
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW)
        p.sats_in_view = localPosition.sats_in_view;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_TIMESTAMP)
        p.timestamp = localPosition.timestamp;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SEQ_NO)
        p.seq_number = localPosition.seq_number;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_HEADING) {
        p.ground_track = localPosition.ground_track;
        p.has_ground_track = true;
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SPEED) {
        p.ground_speed = localPosition.ground_speed;
        p.has_ground_speed = true;
    }

    LOG_INFO("Position packet: time=%i lat=%i lon=%i", p.time, p.latitude_i, p.longitude_i);

#ifndef MESHTASTIC_EXCLUDE_ATAK
    // TAK Tracker devices should send their position in a TAK packet over the ATAK port
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)
        return allocAtakPli();
#endif

    return allocDataProtobuf(p);
}

meshtastic_MeshPacket *PositionModule::allocReply()
{
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND && lastSentReply &&
        Throttle::isWithinTimespanMs(lastSentReply, 3 * 60 * 1000)) {
        LOG_DEBUG("Skip Position reply since we sent a reply <3min ago");
        ignoreRequest = true; // Mark it as ignored for MeshModule
        return nullptr;
    }

    meshtastic_MeshPacket *reply = allocPositionPacket();
    if (reply) {
        lastSentReply = millis(); // Track when we sent this reply
    }
    return reply;
}

meshtastic_MeshPacket *PositionModule::allocAtakPli()
{
    LOG_INFO("Send TAK PLI packet");
    meshtastic_MeshPacket *mp = allocDataPacket();
    mp->decoded.portnum = meshtastic_PortNum_ATAK_PLUGIN;

    meshtastic_TAKPacket takPacket = {.is_compressed = true,
                                      .has_contact = true,
                                      .contact = meshtastic_Contact_init_default,
                                      .has_group = true,
                                      .group = {meshtastic_MemberRole_TeamMember, meshtastic_Team_Cyan},
                                      .has_status = true,
                                      .status =
                                          {
                                              .battery = powerStatus->getBatteryChargePercent(),
                                          },
                                      .which_payload_variant = meshtastic_TAKPacket_pli_tag,
                                      .payload_variant = {.pli = {
                                                              .latitude_i = localPosition.latitude_i,
                                                              .longitude_i = localPosition.longitude_i,
                                                              .altitude = localPosition.altitude_hae,
                                                              .speed = localPosition.ground_speed,
                                                              .course = static_cast<uint16_t>(localPosition.ground_track),
                                                          }}};

    auto length = unishox2_compress_lines(owner.long_name, strlen(owner.long_name), takPacket.contact.device_callsign,
                                          sizeof(takPacket.contact.device_callsign) - 1, USX_PSET_DFLT, NULL);
    LOG_DEBUG("Uncompressed device_callsign '%s' - %d bytes", owner.long_name, strlen(owner.long_name));
    LOG_DEBUG("Compressed device_callsign '%s' - %d bytes", takPacket.contact.device_callsign, length);
    length = unishox2_compress_lines(owner.long_name, strlen(owner.long_name), takPacket.contact.callsign,
                                     sizeof(takPacket.contact.callsign) - 1, USX_PSET_DFLT, NULL);
    mp->decoded.payload.size =
        pb_encode_to_bytes(mp->decoded.payload.bytes, sizeof(mp->decoded.payload.bytes), &meshtastic_TAKPacket_msg, &takPacket);
    return mp;
}

void PositionModule::sendOurPosition()
{
    bool requestReplies = currentGeneration != radioGeneration;
    currentGeneration = radioGeneration;

    // If we changed channels, ask everyone else for their latest info
    LOG_INFO("Send pos@%x:6 to mesh (wantReplies=%d)", localPosition.timestamp, requestReplies);
    for (uint8_t channelNum = 0; channelNum < 8; channelNum++) {
        if (channels.getByIndex(channelNum).settings.has_module_settings &&
            channels.getByIndex(channelNum).settings.module_settings.position_precision != 0) {
            sendOurPosition(NODENUM_BROADCAST, requestReplies, channelNum);
            return;
        }
    }
}

void PositionModule::sendOurPosition(NodeNum dest, bool wantReplies, uint8_t channel)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service->cancelSending(prevPacketId);

    // Set's the class precision value for this particular packet
    if (channels.getByIndex(channel).settings.has_module_settings) {
        precision = channels.getByIndex(channel).settings.module_settings.position_precision;
    }

    meshtastic_MeshPacket *p = allocPositionPacket();
    if (p == nullptr) {
        LOG_DEBUG("allocPositionPacket returned a nullptr");
        return;
    }

    p->to = dest;
    p->decoded.want_response = config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ? false : wantReplies;
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    else
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    prevPacketId = p->id;

    if (channel > 0)
        p->channel = channel;

    service->sendToMesh(p, RX_SRC_LOCAL, true);

    if (IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_TRACKER,
                  meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) &&
        config.power.is_power_saving) {
        meshtastic_ClientNotification *notification = clientNotificationPool.allocZeroed();
        notification->level = meshtastic_LogRecord_Level_INFO;
        notification->time = getValidTime(RTCQualityFromNet);
        sprintf(notification->message, "Sending position and sleeping for %us interval in a moment",
                Default::getConfiguredOrDefaultMs(config.position.position_broadcast_secs, default_broadcast_interval_secs) /
                    1000U);
        service->sendClientNotification(notification);
        sleepOnNextExecution = true;
        LOG_DEBUG("Start next execution in 5s, then sleep");
        setIntervalFromNow(FIVE_SECONDS_MS);
    }
}

#define RUNONCE_INTERVAL 5000;

int32_t PositionModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(config.position.position_broadcast_secs);
        LOG_DEBUG("Sleep for %ims, then awaking to send position again", nightyNightMs);
        doDeepSleep(nightyNightMs, false, false);
    }

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (node == nullptr)
        return RUNONCE_INTERVAL;

    // We limit our GPS broadcasts to a max rate
    uint32_t now = millis();
    uint32_t intervalMs = Default::getConfiguredOrDefaultMsScaled(config.position.position_broadcast_secs,
                                                                  default_broadcast_interval_secs, numOnlineNodes);
    uint32_t msSinceLastSend = now - lastGpsSend;
    // Only send packets if the channel util. is less than 25% utilized or we're a tracker with less than 40% utilized.
    if (!airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER &&
                                         config.device.role != meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)) {
        return RUNONCE_INTERVAL;
    }

    if (lastGpsSend == 0 || msSinceLastSend >= intervalMs) {
        if (nodeDB->hasValidPosition(node)) {
            lastGpsSend = now;

            lastGpsLatitude = node->position.latitude_i;
            lastGpsLongitude = node->position.longitude_i;

            sendOurPosition();
            if (config.device.role == meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND) {
                sendLostAndFoundText();
            }
        }
    } else if (config.position.position_broadcast_smart_enabled) {
        const meshtastic_NodeInfoLite *node2 = service->refreshLocalMeshNode(); // should guarantee there is now a position

        if (nodeDB->hasValidPosition(node2)) {
            // The minimum time (in seconds) that would pass before we are able to send a new position packet.

            auto smartPosition = getDistanceTraveledSinceLastSend(node->position);
            msSinceLastSend = now - lastGpsSend;

            if (smartPosition.hasTraveledOverThreshold &&
                Throttle::execute(
                    &lastGpsSend, minimumTimeThreshold, []() { positionModule->sendOurPosition(); },
                    []() { LOG_DEBUG("Skip send smart broadcast due to time throttling"); })) {

                LOG_DEBUG("Sent smart pos@%x:6 to mesh (distanceTraveled=%fm, minDistanceThreshold=%im, timeElapsed=%ims, "
                          "minTimeInterval=%ims)",
                          localPosition.timestamp, smartPosition.distanceTraveled, smartPosition.distanceThreshold,
                          msSinceLastSend, minimumTimeThreshold);

                // Set the current coords as our last ones, after we've compared distance with current and decided to send
                lastGpsLatitude = node->position.latitude_i;
                lastGpsLongitude = node->position.longitude_i;
            }
        }
    }

    return RUNONCE_INTERVAL; // to save power only wake for our callback occasionally
}

void PositionModule::sendLostAndFoundText()
{
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    char *message = new char[60];
    sprintf(message, "🚨I'm lost! Lat / Lon: %f, %f\a", (lastGpsLatitude * 1e-7), (lastGpsLongitude * 1e-7));
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    service->sendToMesh(p, RX_SRC_LOCAL, true);
    delete[] message;
}

struct SmartPosition PositionModule::getDistanceTraveledSinceLastSend(meshtastic_PositionLite currentPosition)
{
    // The minimum distance to travel before we are able to send a new position packet.
    const uint32_t distanceTravelThreshold =
        Default::getConfiguredOrDefault(config.position.broadcast_smart_minimum_distance, 100);

    // Determine the distance in meters between two points on the globe
    float distanceTraveledSinceLastSend = GeoCoord::latLongToMeter(
        lastGpsLatitude * 1e-7, lastGpsLongitude * 1e-7, currentPosition.latitude_i * 1e-7, currentPosition.longitude_i * 1e-7);

    return SmartPosition{.distanceTraveled = abs(distanceTraveledSinceLastSend),
                         .distanceThreshold = distanceTravelThreshold,
                         .hasTraveledOverThreshold = abs(distanceTraveledSinceLastSend) >= distanceTravelThreshold};
}

void PositionModule::handleNewPosition()
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    const meshtastic_NodeInfoLite *node2 = service->refreshLocalMeshNode(); // should guarantee there is now a position
    // We limit our GPS broadcasts to a max rate
    if (nodeDB->hasValidPosition(node2)) {
        auto smartPosition = getDistanceTraveledSinceLastSend(node->position);
        uint32_t msSinceLastSend = millis() - lastGpsSend;
        if (smartPosition.hasTraveledOverThreshold &&
            Throttle::execute(
                &lastGpsSend, minimumTimeThreshold, []() { positionModule->sendOurPosition(); },
                []() { LOG_DEBUG("Skip send smart broadcast due to time throttling"); })) {
            LOG_DEBUG("Sent smart pos@%x:6 to mesh (distanceTraveled=%fm, minDistanceThreshold=%im, timeElapsed=%ims, "
                      "minTimeInterval=%ims)",
                      localPosition.timestamp, smartPosition.distanceTraveled, smartPosition.distanceThreshold, msSinceLastSend,
                      minimumTimeThreshold);

            // Set the current coords as our last ones, after we've compared distance with current and decided to send
            lastGpsLatitude = node->position.latitude_i;
            lastGpsLongitude = node->position.longitude_i;
        }
    }
}

#endif