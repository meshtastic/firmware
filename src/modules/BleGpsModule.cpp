#if !MESHTASTIC_EXCLUDE_GPS && !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "BleGpsModule.h"
#include "Default.h"
#include "GPS.h"
#include "GPSStatus.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "main.h"

BleGpsModule *bleGpsModule;

BleGpsModule::BleGpsModule()
    : ProtobufModule("blegps", meshtastic_PortNum_POSITION_APP, &meshtastic_Position_msg),
      concurrency::OSThread("BleGpsModule")
{
    // Set initial delay before first execution to allow system to initialize
    setIntervalFromNow(setStartDelay());
    
    LOG_INFO("BleGpsModule initialized - will send position to phone every %d ms", sendIntervalMs);
}

bool BleGpsModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Position *p)
{
    // We don't need to handle incoming position packets for this module
    // Let other modules (like PositionModule) handle them
    // Return false to allow other modules to process this message
    return false;
}

int32_t BleGpsModule::runOnce()
{
    // Check if enough time has passed since last send
    uint32_t now = millis();
    
    // Handle millis() wrap-around (happens after ~49 days)
    if (lastSentToPhone > now) {
        lastSentToPhone = 0;
    }
    
    // Check if it's time to send position update
    if ((lastSentToPhone == 0) || ((now - lastSentToPhone) >= sendIntervalMs)) {
        sendPositionToPhone();
        lastSentToPhone = now;
    }
    
    // Return interval until next execution
    return sendIntervalMs;
}

void BleGpsModule::sendPositionToPhone()
{
    // Check if we have a valid GPS position
#if HAS_GPS
    if (!gpsStatus || !gpsStatus->getHasLock()) {
        LOG_DEBUG("BleGpsModule: No GPS lock, skipping position send");
        return;
    }
#endif

    // Get current position
    meshtastic_Position position = getCurrentPosition();
    
    // Check if position is valid
    if (!position.has_latitude_i || !position.has_longitude_i) {
        LOG_DEBUG("BleGpsModule: No valid position data, skipping send");
        return;
    }

    // Check if phone is connected (queue is not full or empty)
    if (!service || service->isToPhoneQueueEmpty()) {
        // Phone might not be connected, but we can still queue the packet
        // It will be sent when phone connects
    }

    // Allocate packet with position data
    meshtastic_MeshPacket *p = allocDataProtobuf(position);
    if (!p) {
        LOG_ERROR("BleGpsModule: Failed to allocate position packet");
        return;
    }

    // Set packet properties
    p->to = NODENUM_BROADCAST; // Not required for sendToPhone, but set for consistency
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    // Send to phone via BLE
    service->sendToPhone(p);
    
    LOG_DEBUG("BleGpsModule: Sent position to phone - lat=%d, lon=%d, time=%u", 
              position.latitude_i, position.longitude_i, position.time);
}

meshtastic_Position BleGpsModule::getCurrentPosition()
{
    meshtastic_Position position = meshtastic_Position_init_default;
    
    // Get current node info from nodeDB
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (!node) {
        LOG_WARN("BleGpsModule: Could not get local node info");
        return position;
    }

    // Check if node has valid position
    if (!nodeDB->hasValidPosition(node)) {
        LOG_DEBUG("BleGpsModule: Node does not have valid position");
        return position;
    }

    // Copy position from nodeDB
    position = node->position;
    
    // Ensure we have latitude and longitude
    if (!position.has_latitude_i || !position.has_longitude_i) {
        LOG_DEBUG("BleGpsModule: Position missing lat/lon");
        return position;
    }

    // Update timestamp if not set - use best available time quality
    if (position.time == 0) {
        if (getValidTime(RTCQualityNTP) > 0) {
            position.time = getValidTime(RTCQualityNTP);
        } else if (getValidTime(RTCQualityDevice) > 0) {
            position.time = getValidTime(RTCQualityDevice);
        } else {
            position.time = getValidTime(RTCQualityFromNet);
        }
    }

    return position;
}

#endif // !MESHTASTIC_EXCLUDE_GPS && !MESHTASTIC_EXCLUDE_BLUETOOTH

