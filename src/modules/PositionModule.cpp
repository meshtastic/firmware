#if !MESHTASTIC_EXCLUDE_GPS
#include "PositionModule.h"
#include "Default.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PositionPrecision.h"
#include "RTC.h"
#include "Router.h"
#include "TransmitHistory.h"
#include "TypeConversions.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "main.h"
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

    // Seed throttle timer from persisted transmit history so we don't re-broadcast immediately after reboot
    if (transmitHistory) {
        uint32_t restored = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_POSITION_APP);
        if (restored != 0) {
            lastGpsSend = restored;
            LOG_INFO("Position: restored lastGpsSend from transmit history");
        }
    }

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

    const auto transport = mp.transport_mechanism;
    if (isFromUs(&mp) && !IS_ONE_OF(transport, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL,
                                    meshtastic_MeshPacket_TransportMechanism_TRANSPORT_API)) {
        LOG_WARN("Ignoring packet supposedly from us over external transport");
        return true;
    }

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
    precision = getPositionPrecisionForChannel(mp.channel);

    return false; // Let others look at this message also if they want
}

void PositionModule::alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_Position *p)
{
    // Phone position packets need to be truncated to the channel precision
    if (isFromUs(&mp)) {
        if (precision == 0)
            LOG_DEBUG("Strip phone position due to channel precision 0");
        else if (precision < 32)
            LOG_DEBUG("Truncate phone position to channel precision %i", precision);
        applyPositionPrecision(*p, precision);
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

    const meshtastic_NodeInfoLite *node = service->refreshLocalMeshNode(); // should guarantee there is now a position

    // configuration of POSITION packet
    //   consider making this a function argument?
    uint32_t pos_flags = config.position.position_flags;

    // Populate a Position struct with ONLY the requested fields
    meshtastic_Position p = meshtastic_Position_init_default; //   Start with an empty structure
    // if localPosition is totally empty, put our last saved position (lite) in there
    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0) {
        meshtastic_PositionLite cachedSelf;
        if (nodeDB->copyNodePosition(node->num, cachedSelf))
            nodeDB->setLocalPosition(TypeConversions::ConvertToPosition(cachedSelf));
    }
    localPosition.seq_number++;

    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0) {
        LOG_WARN("Skip position send because lat/lon are zero!");
        return nullptr;
    }

    // lat/lon are unconditionally included - IF AVAILABLE!
    LOG_DEBUG("Send location with precision %i", precision);
    p.latitude_i = localPosition.latitude_i;
    p.longitude_i = localPosition.longitude_i;
    p.has_latitude_i = true;
    p.has_longitude_i = true;
    applyPositionPrecision(p, precision);
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
    LOG_INFO("Send TAK V2 PLI packet");
    meshtastic_MeshPacket *mp = allocDataPacket();
    mp->decoded.portnum = meshtastic_PortNum_ATAK_PLUGIN_V2;

    meshtastic_TAKPacketV2 takPacket = meshtastic_TAKPacketV2_init_zero;
    takPacket.cot_type_id = meshtastic_CotType_CotType_a_f_G_U_C;

    // Use TAK config for team/role if configured, otherwise use defaults (Cyan/TeamMember)
    if (moduleConfig.has_tak && moduleConfig.tak.team != meshtastic_Team_Unspecifed_Color) {
        takPacket.team = moduleConfig.tak.team;
    } else {
        takPacket.team = meshtastic_Team_Cyan;
    }

    if (moduleConfig.has_tak && moduleConfig.tak.role != meshtastic_MemberRole_Unspecifed) {
        takPacket.role = moduleConfig.tak.role;
    } else {
        takPacket.role = meshtastic_MemberRole_TeamMember;
    }
    takPacket.latitude_i = localPosition.latitude_i;
    takPacket.longitude_i = localPosition.longitude_i;
    takPacket.altitude = localPosition.altitude_hae;
    takPacket.speed = localPosition.ground_speed;
    // ground_track is stored as degrees * 1e5, course field expects degrees * 100
    int32_t course = localPosition.ground_track / 1000;
    if (course < 0)
        course = 0;
    else if (course > 36000)
        course = 36000;
    takPacket.course = static_cast<uint16_t>(course);
    takPacket.battery = powerStatus->getBatteryChargePercent();

    // Map position source to CoT how/geo_src/alt_src
    if (config.position.fixed_position || localPosition.location_source == meshtastic_Position_LocSource_LOC_MANUAL) {
        takPacket.how = meshtastic_CotHow_CotHow_h_e;
        takPacket.geo_src = meshtastic_GeoPointSource_GeoPointSource_USER;
        takPacket.alt_src = meshtastic_GeoPointSource_GeoPointSource_USER;
    } else {
        takPacket.how = meshtastic_CotHow_CotHow_m_g;
        takPacket.geo_src = meshtastic_GeoPointSource_GeoPointSource_GPS;
        takPacket.alt_src = meshtastic_GeoPointSource_GeoPointSource_GPS;
    }

    // Callsign - stored as plain string (no compression, apps handle that)
    strncpy(takPacket.callsign, owner.long_name, sizeof(takPacket.callsign) - 1);
    takPacket.callsign[sizeof(takPacket.callsign) - 1] = '\0';
    strncpy(takPacket.device_callsign, owner.long_name, sizeof(takPacket.device_callsign) - 1);
    takPacket.device_callsign[sizeof(takPacket.device_callsign) - 1] = '\0';

    // CoT uid — ATAK drops PLI entities with empty uid; derive stable "!<nodenum>" id.
    snprintf(takPacket.uid, sizeof(takPacket.uid), "!%08x", nodeDB->getNodeNum());

    // Encode TAKPacketV2 protobuf, leaving room for flags byte prefix
    uint8_t protobuf_bytes[sizeof(mp->decoded.payload.bytes) - 1];
    size_t proto_size = pb_encode_to_bytes(protobuf_bytes, sizeof(protobuf_bytes), &meshtastic_TAKPacketV2_msg, &takPacket);

    if (proto_size == 0) {
        LOG_ERROR("Failed to encode TAK V2 PLI packet");
        packetPool.release(mp);
        return nullptr;
    }

    // Wire format: [flags byte][protobuf bytes]
    // Flags byte 0xFF is a reserved sentinel meaning the remainder is an
    // uncompressed raw protobuf payload (that is, no zstd dictionary ID is
    // encoded in the flags byte). Decoders must check for this sentinel before
    // applying any dictionary-ID masking/interpretation.
    mp->decoded.payload.bytes[0] = 0xFF;
    memcpy(mp->decoded.payload.bytes + 1, protobuf_bytes, proto_size);
    mp->decoded.payload.size = proto_size + 1;

    LOG_DEBUG("TAK V2 PLI payload: %zu bytes (1 flags + %zu protobuf)", mp->decoded.payload.size, proto_size);
    return mp;
}

void PositionModule::sendOurPosition()
{
    bool requestReplies = currentGeneration != radioGeneration;
    currentGeneration = radioGeneration;

    // If we changed channels, ask everyone else for their latest info
    LOG_INFO("Send pos@%x:6 to mesh (wantReplies=%d)", localPosition.timestamp, requestReplies);
    for (uint8_t channelNum = 0; channelNum < 8; channelNum++) {
        if (getPositionPrecisionForChannel(channelNum) != 0) {
            sendOurPosition(NODENUM_BROADCAST, requestReplies, channelNum);
            return;
        }
    }
}

void PositionModule::sendOurPosition(NodeNum dest, bool wantReplies, uint8_t channel)
{
    if (!config.position.fixed_position && !nodeDB->hasLocalPositionSinceBoot()) {
        LOG_DEBUG("Skip position send; no fresh position since boot");
        return;
    }

    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service->cancelSending(prevPacketId);

    // Set the class precision value for this particular packet.
    precision = getPositionPrecisionForChannel(channel);

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

bool PositionModule::positionUnchangedSinceLastSend(const meshtastic_PositionLite &selfPos, bool useConfiguredPrecision)
{
    if (lastGpsLatitude == 0 && lastGpsLongitude == 0)
        return false; // no prior broadcast to compare against

    // Broadcast channel = the one sendOurPosition() would pick (first with non-zero on-wire
    // precision). Default nodes gauge movement at that on-wire (public-clamped) resolution;
    // trackers use their own configured (unclamped) precision so finer moves still count.
    uint32_t precisionBits = 0;
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (getPositionPrecisionForChannel(ch) == 0)
            continue;
        precisionBits =
            useConfiguredPrecision ? getPositionPrecisionForChannel(channels.getByIndex(ch)) : getPositionPrecisionForChannel(ch);
        break;
    }

    return positionWithinPrecisionCell(selfPos.latitude_i, selfPos.longitude_i, lastGpsLatitude, lastGpsLongitude, precisionBits);
}

bool PositionModule::positionWithinPrecisionCell(int32_t aLat, int32_t aLon, int32_t bLat, int32_t bLon, uint32_t precision)
{
    if (precision == 0 || precision >= 32)
        return false; // sharing disabled or full precision: no coarse cell to hold within

    return truncateCoordinate(aLat, precision) == truncateCoordinate(bLat, precision) &&
           truncateCoordinate(aLon, precision) == truncateCoordinate(bLon, precision);
}

uint32_t PositionModule::effectiveBroadcastIntervalMs(uint32_t configuredIntervalMs, bool stationary, uint32_t stationaryFloorMs)
{
    if (stationary && stationaryFloorMs > configuredIntervalMs)
        return stationaryFloorMs;
    return configuredIntervalMs;
}

int32_t PositionModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(config.position.position_broadcast_secs);
        LOG_DEBUG("Sleep for %ims, then awaking to send position again", nightyNightMs);
        doDeepSleep(nightyNightMs, false, false);
    }

    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (node == nullptr)
        return RUNONCE_INTERVAL;

    // We limit our GPS broadcasts to a max rate
    uint32_t now = millis();
    uint32_t intervalMs = Default::getConfiguredOrDefaultMsScaled(
        config.position.position_broadcast_secs, default_broadcast_interval_secs, numOnlineNodes, TrafficType::POSITION);
    uint32_t msSinceLastSend = now - lastGpsSend;
    // Only send packets if the channel util. is less than 25% utilized or we're a tracker with less than 40% utilized.
    if (!airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER &&
                                         config.device.role != meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)) {
        return RUNONCE_INTERVAL;
    }

    bool waitingForFreshPosition = (lastGpsSend == 0) && !config.position.fixed_position && !nodeDB->hasLocalPositionSinceBoot();

    // Hold to the 12h floor when fixed_position (every role: pinning yourself forfeits the
    // exception) or when stationary. A real move still goes out early via smart-broadcast below.
    // Not-fixed exceptions: lost-and-found broadcasts freely; trackers judge movement at their
    // own (unclamped) precision rather than the on-wire one (useConfiguredPrecision).
    const auto role = config.device.role;
    bool stationary = config.position.fixed_position;
    if (!stationary && role != meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND && nodeDB->hasValidPosition(node)) {
        const bool isTracker =
            IS_ONE_OF(role, meshtastic_Config_DeviceConfig_Role_TRACKER, meshtastic_Config_DeviceConfig_Role_TAK_TRACKER);
        meshtastic_PositionLite selfPos;
        if (nodeDB->copyNodePosition(node->num, selfPos))
            stationary = positionUnchangedSinceLastSend(selfPos, /*useConfiguredPrecision=*/isTracker);
    }
    uint32_t effectiveIntervalMs =
        effectiveBroadcastIntervalMs(intervalMs, stationary, (uint32_t)default_position_stationary_broadcast_secs * 1000UL);

    if (lastGpsSend == 0 || msSinceLastSend >= effectiveIntervalMs) {
        if (waitingForFreshPosition) {
#ifdef GPS_DEBUG
            LOG_DEBUG("Skip initial position send; no fresh position since boot");
#endif
        } else if (nodeDB->hasValidPosition(node)) {
            lastGpsSend = now;

            meshtastic_PositionLite selfPos;
            if (nodeDB->copyNodePosition(node->num, selfPos)) {
                lastGpsLatitude = selfPos.latitude_i;
                lastGpsLongitude = selfPos.longitude_i;
            }

            if (transmitHistory)
                transmitHistory->setLastSentToMesh(meshtastic_PortNum_POSITION_APP);
            sendOurPosition();
            if (config.device.role == meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND) {
                sendLostAndFoundText();
            }
        }
    } else if (config.position.position_broadcast_smart_enabled) {
        const meshtastic_NodeInfoLite *node2 = service->refreshLocalMeshNode(); // should guarantee there is now a position

        if (nodeDB->hasValidPosition(node2)) {
            // The minimum time (in seconds) that would pass before we are able to send a new position packet.

            meshtastic_PositionLite selfPos;
            if (!nodeDB->copyNodePosition(node->num, selfPos))
                return RUNONCE_INTERVAL; // Defensive: hasValidPosition should imply this is non-null
            auto smartPosition = getDistanceTraveledSinceLastSend(selfPos);
            msSinceLastSend = now - lastGpsSend;

            if (smartPosition.hasTraveledOverThreshold &&
                Throttle::execute(
                    &lastGpsSend, minimumTimeThreshold, []() { positionModule->sendOurPosition(); },
                    []() {
#ifdef GPS_DEBUG
                        LOG_DEBUG("Skip send smart broadcast due to time throttling");
#endif
                    })) {

                LOG_DEBUG("Sent smart pos@%x:6 to mesh (distanceTraveled=%fm, minDistanceThreshold=%im, timeElapsed=%ims, "
                          "minTimeInterval=%ims)",
                          localPosition.timestamp, smartPosition.distanceTraveled, smartPosition.distanceThreshold,
                          msSinceLastSend, minimumTimeThreshold);

                // Set the current coords as our last ones, after we've compared distance with current and decided to send
                lastGpsLatitude = selfPos.latitude_i;
                lastGpsLongitude = selfPos.longitude_i;
            }
        }
    }

    return RUNONCE_INTERVAL; // to save power only wake for our callback occasionally
}

void PositionModule::sendLostAndFoundText()
{
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    char message[128];
    int written = snprintf(message, sizeof(message), "🚨I'm lost! Lat / Lon: %f, %f\a", (lastGpsLatitude * 1e-7),
                           (lastGpsLongitude * 1e-7));
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->want_ack = false;
    if (written < 0) {
        // snprintf encoding error — send an empty payload rather than uninitialized bytes.
        p->decoded.payload.size = 0;
    } else {
        // Clamp to buffer capacity (snprintf returns "would-have-written" which can exceed the buffer).
        const size_t msg_len = std::min(static_cast<size_t>(written), sizeof(message) - 1);
        p->decoded.payload.size = msg_len;
        if (msg_len > 0) {
            memcpy(p->decoded.payload.bytes, message, msg_len);
        }
    }

    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

// Helper: return imprecise (truncated + centered) lat/lon as int32 using current precision
static inline void computeImpreciseLatLon(int32_t inLat, int32_t inLon, uint8_t precisionBits, int32_t &outLat, int32_t &outLon)
{
    if (precisionBits > 0 && precisionBits < 32) {
        // Build mask for top 'precisionBits' bits of a 32-bit unsigned field
        const uint32_t mask = (precisionBits == 32) ? UINT32_MAX : (UINT32_MAX << (32 - precisionBits));
        // Note: latitude_i/longitude_i are stored as signed 32-bit in meshtastic code but
        // the bitmask logic used previously operated as unsigned—preserve that behavior by
        // casting to uint32_t for masking, then back to int32_t.
        uint32_t lat_u = static_cast<uint32_t>(inLat) & mask;
        uint32_t lon_u = static_cast<uint32_t>(inLon) & mask;

        // Add the "center of cell" offset used elsewhere:
        // The code previously added (1 << (31 - precision)) to produce the middle of the possible location.
        uint32_t center_offset = (1u << (31 - precisionBits));
        lat_u += center_offset;
        lon_u += center_offset;

        outLat = static_cast<int32_t>(lat_u);
        outLon = static_cast<int32_t>(lon_u);
    } else {
        // full precision: return input unchanged
        outLat = inLat;
        outLon = inLon;
    }
}

struct SmartPosition PositionModule::getDistanceTraveledSinceLastSend(meshtastic_PositionLite currentPosition)
{
    const uint32_t distanceTravelThreshold =
        Default::getConfiguredOrDefault(config.position.broadcast_smart_minimum_distance, 100);

    int32_t lastLatImprecise, lastLonImprecise;
    int32_t currentLatImprecise, currentLonImprecise;

    computeImpreciseLatLon(lastGpsLatitude, lastGpsLongitude, precision, lastLatImprecise, lastLonImprecise);
    computeImpreciseLatLon(currentPosition.latitude_i, currentPosition.longitude_i, precision, currentLatImprecise,
                           currentLonImprecise);

    float distMeters = GeoCoord::latLongToMeter(lastLatImprecise * 1e-7, lastLonImprecise * 1e-7, currentLatImprecise * 1e-7,
                                                currentLonImprecise * 1e-7);

    float distanceTraveled = fabsf(distMeters);

    return SmartPosition{.distanceTraveled = distanceTraveled,
                         .distanceThreshold = distanceTravelThreshold,
                         .hasTraveledOverThreshold = distanceTraveled >= distanceTravelThreshold};
}

void PositionModule::handleNewPosition()
{
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    const meshtastic_NodeInfoLite *node2 = service->refreshLocalMeshNode(); // should guarantee there is now a position
    // We limit our GPS broadcasts to a max rate
    if (nodeDB->hasValidPosition(node2)) {
        meshtastic_PositionLite selfPos;
        if (!nodeDB->copyNodePosition(node->num, selfPos))
            return;
        auto smartPosition = getDistanceTraveledSinceLastSend(selfPos);
        uint32_t msSinceLastSend = millis() - lastGpsSend;
        if (smartPosition.hasTraveledOverThreshold &&
            Throttle::execute(
                &lastGpsSend, minimumTimeThreshold, []() { positionModule->sendOurPosition(); },
                []() {
#ifdef GPS_DEBUG
                    LOG_DEBUG("Skip send smart broadcast due to time throttling");
#endif
                })) {
            LOG_DEBUG("Sent smart pos@%x:6 to mesh (distanceTraveled=%fm, minDistanceThreshold=%im, timeElapsed=%ims, "
                      "minTimeInterval=%ims)",
                      localPosition.timestamp, smartPosition.distanceTraveled, smartPosition.distanceThreshold, msSinceLastSend,
                      minimumTimeThreshold);

            // Set the current coords as our last ones, after we've compared distance with current and decided to send
            lastGpsLatitude = selfPos.latitude_i;
            lastGpsLongitude = selfPos.longitude_i;
        }
    }
}

#endif
