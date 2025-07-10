#include "TraceRouteModule.h"
#include "MeshService.h"
#include "meshUtils.h"
#include "NodeDB.h"
#include "Router.h"
#include <pb_encode.h>
#if HAS_SCREEN
#include "graphics/Screen.h"
#endif

TraceRouteModule *traceRouteModule;

bool TraceRouteModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r)
{
    LOG_INFO("TraceRoute packet: request_id=%u, from=0x%x, to=0x%x, ourNode=0x%x", 
             mp.decoded.request_id, mp.from, mp.to, nodeDB->getNodeNum());
    
    // Additional debug: check route arrays to understand packet direction
    LOG_INFO("Route debug: route_count=%d, route_back_count=%d", r->route_count, r->route_back_count);
    
    if (mp.decoded.request_id && 
        mp.from != nodeDB->getNodeNum() && 
        mp.to == nodeDB->getNodeNum()) {
        
        LOG_INFO("Displaying trace route result - this is a response to our request");
        displayTraceRouteResult(&mp, r);
    } else {
        LOG_INFO("NOT displaying trace route - this is not a response to our request");
    }
    
    return false; 
}

void TraceRouteModule::alterReceivedProtobuf(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r)
{
    const meshtastic_Data &incoming = p.decoded;

    // Insert unknown hops if necessary
    insertUnknownHops(p, r, !incoming.request_id);

    // Append ID and SNR. If the last hop is to us, we only need to append the SNR
    appendMyIDandSNR(r, p.rx_snr, !incoming.request_id, isToUs(&p));
    if (!incoming.request_id)
        printRoute(r, p.from, p.to, true);
    else
        printRoute(r, p.to, p.from, false);

    // Set updated route to the payload of the to be flooded packet
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_RouteDiscovery_msg, r);
}

void TraceRouteModule::insertUnknownHops(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r, bool isTowardsDestination)
{
    pb_size_t *route_count;
    uint32_t *route;
    pb_size_t *snr_count;
    int8_t *snr_list;

    // Pick the correct route array and SNR list
    if (isTowardsDestination) {
        route_count = &r->route_count;
        route = r->route;
        snr_count = &r->snr_towards_count;
        snr_list = r->snr_towards;
    } else {
        route_count = &r->route_back_count;
        route = r->route_back;
        snr_count = &r->snr_back_count;
        snr_list = r->snr_back;
    }

    // Only insert unknown hops if hop_start is valid
    if (p.hop_start != 0 && p.hop_limit <= p.hop_start) {
        uint8_t hopsTaken = p.hop_start - p.hop_limit;
        int8_t diff = hopsTaken - *route_count;
        for (uint8_t i = 0; i < diff; i++) {
            if (*route_count < ROUTE_SIZE) {
                route[*route_count] = NODENUM_BROADCAST; // This will represent an unknown hop
                *route_count += 1;
            }
        }
        // Add unknown SNR values if necessary
        diff = *route_count - *snr_count;
        for (uint8_t i = 0; i < diff; i++) {
            if (*snr_count < ROUTE_SIZE) {
                snr_list[*snr_count] = INT8_MIN; // This will represent an unknown SNR
                *snr_count += 1;
            }
        }
    }
}

void TraceRouteModule::appendMyIDandSNR(meshtastic_RouteDiscovery *updated, float snr, bool isTowardsDestination, bool SNRonly)
{
    pb_size_t *route_count;
    uint32_t *route;
    pb_size_t *snr_count;
    int8_t *snr_list;

    // Pick the correct route array and SNR list
    if (isTowardsDestination) {
        route_count = &updated->route_count;
        route = updated->route;
        snr_count = &updated->snr_towards_count;
        snr_list = updated->snr_towards;
    } else {
        route_count = &updated->route_back_count;
        route = updated->route_back;
        snr_count = &updated->snr_back_count;
        snr_list = updated->snr_back;
    }

    if (*snr_count < ROUTE_SIZE) {
        snr_list[*snr_count] = (int8_t)(snr * 4); // Convert SNR to 1 byte
        *snr_count += 1;
    }
    if (SNRonly)
        return;

    // Length of route array can normally not be exceeded due to the max. hop_limit of 7
    if (*route_count < ROUTE_SIZE) {
        route[*route_count] = myNodeInfo.my_node_num;
        *route_count += 1;
    } else {
        LOG_WARN("Route exceeded maximum hop limit!"); // Are you bridging networks?
    }
}

void TraceRouteModule::printRoute(meshtastic_RouteDiscovery *r, uint32_t origin, uint32_t dest, bool isTowardsDestination)
{
#ifdef DEBUG_PORT
    std::string route = "Route traced:\n";
    route += vformat("0x%x --> ", origin);
    for (uint8_t i = 0; i < r->route_count; i++) {
        if (i < r->snr_towards_count && r->snr_towards[i] != INT8_MIN)
            route += vformat("0x%x (%.2fdB) --> ", r->route[i], (float)r->snr_towards[i] / 4);
        else
            route += vformat("0x%x (?dB) --> ", r->route[i]);
    }
    // If we are the destination, or it has already reached the destination, print it
    if (dest == nodeDB->getNodeNum() || !isTowardsDestination) {
        if (r->snr_towards_count > 0 && r->snr_towards[r->snr_towards_count - 1] != INT8_MIN)
            route += vformat("0x%x (%.2fdB)", dest, (float)r->snr_towards[r->snr_towards_count - 1] / 4);

        else
            route += vformat("0x%x (?dB)", dest);
    } else
        route += "...";

    // If there's a route back (or we are the destination as then the route is complete), print it
    if (r->route_back_count > 0 || origin == nodeDB->getNodeNum()) {
        route += "\n";
        if (r->snr_towards_count > 0 && origin == nodeDB->getNodeNum())
            route += vformat("(%.2fdB) 0x%x <-- ", (float)r->snr_back[r->snr_back_count - 1] / 4, origin);
        else
            route += "...";

        for (int8_t i = r->route_back_count - 1; i >= 0; i--) {
            if (i < r->snr_back_count && r->snr_back[i] != INT8_MIN)
                route += vformat("(%.2fdB) 0x%x <-- ", (float)r->snr_back[i] / 4, r->route_back[i]);
            else
                route += vformat("(?dB) 0x%x <-- ", r->route_back[i]);
        }
        route += vformat("0x%x", dest);
    }
    LOG_INFO(route.c_str());
#endif
}

meshtastic_MeshPacket *TraceRouteModule::allocReply()
{
    assert(currentRequest);

    // Ignore multi-hop broadcast requests
    if (isBroadcast(currentRequest->to) && currentRequest->hop_limit < currentRequest->hop_start) {
        ignoreRequest = true;
        return NULL;
    }

    // Copy the payload of the current request
    auto req = *currentRequest;
    const auto &p = req.decoded;
    meshtastic_RouteDiscovery scratch;
    meshtastic_RouteDiscovery *updated = NULL;
    memset(&scratch, 0, sizeof(scratch));
    pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_RouteDiscovery_msg, &scratch);
    updated = &scratch;

    // Create a MeshPacket with this payload and set it as the reply
    meshtastic_MeshPacket *reply = allocDataProtobuf(*updated);

    return reply;
}

TraceRouteModule::TraceRouteModule()
    : ProtobufModule("traceroute", meshtastic_PortNum_TRACEROUTE_APP, &meshtastic_RouteDiscovery_msg)
{
    ourPortNum = meshtastic_PortNum_TRACEROUTE_APP;
    isPromiscuous = true; // We need to update the route even if it is not destined to us
}

void TraceRouteModule::displayTraceRouteResult(const meshtastic_MeshPacket *mp, meshtastic_RouteDiscovery *r)
{
#if HAS_SCREEN
    LOG_INFO("SNR Debug - snr_towards_count: %d, snr_back_count: %d", r->snr_towards_count, r->snr_back_count);

    String routeText = "";
    
    // Get node names for origin and destination
    auto getNodeName = [](NodeNum nodeNum) -> String {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);
        if (node && node->has_user && strlen(node->user.short_name) > 0) {
            return String(node->user.short_name);
        } else if (node && node->has_user && strlen(node->user.long_name) > 0) {
            return String(node->user.long_name);
        } else {
            char hex[16];
            snprintf(hex, sizeof(hex), "!%08x", nodeNum);
            return String(hex);
        }
    };
    
    // Forward route: OUR_NODE -> ... -> DESTINATION
    // mp->from is the destination (response sender)
    NodeNum destination = mp->from;
    NodeNum origin = nodeDB->getNodeNum();
    
    routeText += getNodeName(origin);
    
    // For direct connection (no intermediate hops in route array)
    if (r->route_count == 0) {
        routeText += " > ";
        routeText += getNodeName(destination);
        
        // For direct connection, the first SNR in snr_towards should be the forward SNR
        if (r->snr_towards_count > 0 && r->snr_towards[0] != INT8_MIN) {
            routeText += "(";
            routeText += String((float)r->snr_towards[0] / 4.0, 1);
            routeText += "dB)";
        }
    } else {
        // Add intermediate hops from the route array with SNR
        for (uint8_t i = 0; i < r->route_count; i++) {
            routeText += " > ";
            routeText += getNodeName(r->route[i]);
            // Add SNR if available
            if (i < r->snr_towards_count && r->snr_towards[i] != INT8_MIN) {
                routeText += "(";
                routeText += String((float)r->snr_towards[i] / 4.0, 1);
                routeText += "dB)";
            }
        }
        
        // Add final destination with SNR
        routeText += " > ";
        routeText += getNodeName(destination);
        // Add final hop SNR
        if (r->snr_towards_count > 0 && r->snr_towards[r->snr_towards_count - 1] != INT8_MIN) {
            routeText += "(";
            routeText += String((float)r->snr_towards[r->snr_towards_count - 1] / 4.0, 1);
            routeText += "dB)";
        }
    }
    
    // Add return route if available
    if (r->route_back_count > 0 || r->snr_back_count > 0 || r->route_count == 0) {
        routeText += "\n";
        routeText += getNodeName(destination);
        
        // For direct connection return
        if (r->route_back_count == 0) {
            routeText += " > ";
            routeText += getNodeName(origin);
            
            // For direct connection, try multiple sources for return SNR
            if (r->snr_back_count > 0 && r->snr_back[0] != INT8_MIN) {
                routeText += "(";
                routeText += String((float)r->snr_back[0] / 4.0, 1);
                routeText += "dB)";
            } else if (mp->rx_snr != 0) {
                // Fallback to packet rx_snr if no snr_back data
                routeText += "(";
                routeText += String(mp->rx_snr, 1);
                routeText += "dB)";
            } else if (r->snr_back_count > 0 && r->snr_back[r->snr_back_count - 1] != INT8_MIN) {
                // Try last SNR in snr_back array
                routeText += "(";
                routeText += String((float)r->snr_back[r->snr_back_count - 1] / 4.0, 1);
                routeText += "dB)";
            }
        } else {
            // Add intermediate return hops with SNR
            for (uint8_t i = 0; i < r->route_back_count; i++) {
                routeText += " > ";
                routeText += getNodeName(r->route_back[i]);
                // Add SNR if available
                if (i < r->snr_back_count && r->snr_back[i] != INT8_MIN) {
                    routeText += "(";
                    routeText += String((float)r->snr_back[i] / 4.0, 1);
                    routeText += "dB)";
                }
            }
            
            // Add final hop back to our node with SNR
            routeText += " > ";
            routeText += getNodeName(origin);
            // The return SNR should be at the last index of snr_back array
            if (r->snr_back_count > 0 && r->snr_back[r->snr_back_count - 1] != INT8_MIN) {
                routeText += "(";
                routeText += String((float)r->snr_back[r->snr_back_count - 1] / 4.0, 1);
                routeText += "dB)";
            }
        }
    }

    LOG_INFO("Trace route result: %s", routeText.c_str());

    if (screen) {
        graphics::BannerOverlayOptions bannerOptions;
        bannerOptions.message = routeText.c_str();
        bannerOptions.durationMs = 5000; // Show for 8 seconds (longer for more complex routes)
        bannerOptions.notificationType = graphics::notificationTypeEnum::text_banner;
        screen->showOverlayBanner(bannerOptions);
    }
#endif
}

bool TraceRouteModule::sendTraceRoute(NodeNum nodeNum)
{
    if (nodeNum == 0 || nodeNum == nodeDB->getNodeNum()) {
        // Can't trace route to ourselves or invalid node
        LOG_WARN("Invalid trace route target: 0x%08X", nodeNum);
        return false;
    }

    // Create empty RouteDiscovery packet
    meshtastic_RouteDiscovery routeDiscovery = meshtastic_RouteDiscovery_init_zero;
    
    // Allocate a packet directly from router
    meshtastic_MeshPacket *p = router->allocForSending();
    if (p) {
        // Set destination and port
        p->to = nodeNum;
        p->decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        p->decoded.want_response = true;
        
        // Manually encode the RouteDiscovery payload
        p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), 
                                                    &meshtastic_RouteDiscovery_msg, &routeDiscovery);
        
        // Send to mesh
        service->sendToMesh(p, RX_SRC_USER);
        
        LOG_INFO("Trace route packet sent to node: 0x%08X", nodeNum);
        
#if HAS_SCREEN
        // Show confirmation message
        if (screen) {
            graphics::BannerOverlayOptions bannerOptions;
            bannerOptions.message = "Trace Route Started";
            bannerOptions.durationMs = 3000; // 3 seconds
            bannerOptions.notificationType = graphics::notificationTypeEnum::text_banner;
            screen->showOverlayBanner(bannerOptions);
        }
#endif
        return true;
    }
    
    LOG_ERROR("Failed to allocate packet for trace route");
    return false;
}