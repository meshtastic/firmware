#include "TraceRouteModule.h"
#include "FloodingRouter.h"
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

void TraceRouteModule::updateRoute(meshtastic_MeshPacket *p)
{
    auto &incoming = p->decoded;
    // Only append an ID for the request (one way)
    if (!incoming.request_id) {
        meshtastic_RouteDiscovery scratch;
        meshtastic_RouteDiscovery *updated = NULL;
        memset(&scratch, 0, sizeof(scratch));
        pb_decode_from_bytes(incoming.payload.bytes, incoming.payload.size, &meshtastic_RouteDiscovery_msg, &scratch);
        updated = &scratch;

        appendMyID(updated);
        printRoute(updated, p->from, NODENUM_BROADCAST);

        // Set updated route to the payload of the to be flooded packet
        p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                     &meshtastic_RouteDiscovery_msg, updated);
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
}