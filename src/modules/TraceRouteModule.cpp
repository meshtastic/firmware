#include "TraceRouteModule.h"
#include "MeshService.h"

TraceRouteModule *traceRouteModule;

bool TraceRouteModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r)
{
    // Only handle a response
    if (mp.decoded.request_id) {
        printRoute(r, mp.to, mp.from);
    }

    return false; // let it be handled by RoutingModule
}

void TraceRouteModule::alterReceivedProtobuf(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r)
{
    auto &incoming = p.decoded;
    // Only append IDs for the request (one way)
    if (!incoming.request_id) {
        // Insert unknown hops if necessary
        insertUnknownHops(p, r);

        // Don't add ourselves if we are the destination (the reply will have our NodeNum already)
        if (p.to != nodeDB->getNodeNum()) {
            appendMyID(r);
            printRoute(r, p.from, NODENUM_BROADCAST);
        }
        // Set updated route to the payload of the to be flooded packet
        p.decoded.payload.size =
            pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_RouteDiscovery_msg, r);
    }
}

void TraceRouteModule::insertUnknownHops(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r)
{
    // Only insert unknown hops if hop_start is valid
    if (p.hop_start != 0 && p.hop_limit <= p.hop_start) {
        uint8_t hopsTaken = p.hop_start - p.hop_limit;
        int8_t diff = hopsTaken - r->route_count;
        for (uint8_t i = 0; i < diff; i++) {
            if (r->route_count < sizeof(r->route) / sizeof(r->route[0])) {
                r->route[r->route_count] = NODENUM_BROADCAST; // This will represent an unknown hop
                r->route_count += 1;
            }
        }
    }
}

void TraceRouteModule::appendMyID(meshtastic_RouteDiscovery *updated)
{
    // Length of route array can normally not be exceeded due to the max. hop_limit of 7
    if (updated->route_count < sizeof(updated->route) / sizeof(updated->route[0])) {
        updated->route[updated->route_count] = myNodeInfo.my_node_num;
        updated->route_count += 1;
    } else {
        LOG_WARN("Route exceeded maximum hop limit, are you bridging networks?\n");
    }
}

void TraceRouteModule::printRoute(meshtastic_RouteDiscovery *r, uint32_t origin, uint32_t dest)
{
#ifdef DEBUG_PORT
    LOG_INFO("Route traced:\n");
    LOG_INFO("0x%x --> ", origin);
    for (uint8_t i = 0; i < r->route_count; i++) {
        LOG_INFO("0x%x --> ", r->route[i]);
    }
    if (dest != NODENUM_BROADCAST)
        LOG_INFO("0x%x\n", dest);
    else
        LOG_INFO("...\n");
#endif
}

meshtastic_MeshPacket *TraceRouteModule::allocReply()
{
    assert(currentRequest);

    // Copy the payload of the current request
    auto req = *currentRequest;
    const auto &p = req.decoded;
    meshtastic_RouteDiscovery scratch;
    meshtastic_RouteDiscovery *updated = NULL;
    memset(&scratch, 0, sizeof(scratch));
    pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_RouteDiscovery_msg, &scratch);
    updated = &scratch;

    printRoute(updated, req.from, req.to);

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