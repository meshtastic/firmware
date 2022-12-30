#include "TraceRouteModule.h"
#include "MeshService.h"
#include "FloodingRouter.h"

TraceRouteModule *traceRouteModule; 


bool TraceRouteModule::handleReceivedProtobuf(const MeshPacket &mp, RouteDiscovery *r)
{ 
    // Only handle a response
    if (mp.decoded.request_id) {
        printRoute(r, mp.to, mp.from);
    }
    
    return false; // let it be handled by RoutingModule
}


void TraceRouteModule::updateRoute(MeshPacket* p)
{ 
    auto &incoming = p->decoded;
    // Only append an ID for the request (one way)
    if (!incoming.request_id) { 
        RouteDiscovery scratch;
        RouteDiscovery *updated = NULL;
        memset(&scratch, 0, sizeof(scratch));
        pb_decode_from_bytes(incoming.payload.bytes, incoming.payload.size, &RouteDiscovery_msg, &scratch);
        updated = &scratch;

        appendMyID(updated);
        printRoute(updated, p->from, NODENUM_BROADCAST);
      
        // Set updated route to the payload of the to be flooded packet
        p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &RouteDiscovery_msg, updated);
    }
}


void TraceRouteModule::appendMyID(RouteDiscovery* updated) 
{
    // Length of route array can normally not be exceeded due to the max. hop_limit of 7
    if (updated->route_count < sizeof(updated->route)/sizeof(updated->route[0])) { 
        updated->route[updated->route_count] = myNodeInfo.my_node_num;
        updated->route_count += 1;        
    } else {
        LOG_WARN("Route exceeded maximum hop limit, are you bridging networks?\n");
    }
}


void TraceRouteModule::printRoute(RouteDiscovery* r, uint32_t origin, uint32_t dest) 
{
    LOG_INFO("Route traced:\n");
    LOG_INFO("0x%x --> ", origin);
    for (uint8_t i=0; i<r->route_count; i++) {
        LOG_INFO("0x%x --> ", r->route[i]); 
    }
    if (dest != NODENUM_BROADCAST) LOG_INFO("0x%x\n", dest); else LOG_INFO("...\n");
}


MeshPacket* TraceRouteModule::allocReply() 
{
    assert(currentRequest);

    // Copy the payload of the current request
    auto req = *currentRequest;
    auto &p = req.decoded;
    RouteDiscovery scratch;
    RouteDiscovery *updated = NULL;
    memset(&scratch, 0, sizeof(scratch));
    pb_decode_from_bytes(p.payload.bytes, p.payload.size, &RouteDiscovery_msg, &scratch);
    updated = &scratch;

    printRoute(updated, req.from, req.to);

    // Create a MeshPacket with this payload and set it as the reply
    MeshPacket* reply = allocDataProtobuf(*updated); 

    return reply;
}


TraceRouteModule::TraceRouteModule() : ProtobufModule("traceroute", PortNum_TRACEROUTE_APP, &RouteDiscovery_msg) {
    ourPortNum = PortNum_TRACEROUTE_APP; 
}
