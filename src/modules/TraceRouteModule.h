#pragma once
#include "ProtobufModule.h"

/**
 * A module that traces the route to a certain destination node
 */
class TraceRouteModule : public ProtobufModule<meshtastic_RouteDiscovery>
{
  public:
    TraceRouteModule();

  protected:
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r) override;

    virtual meshtastic_MeshPacket *allocReply() override;

    /* Called before rebroadcasting a RouteDiscovery payload in order to update
       the route array containing the IDs of nodes this packet went through */
    void alterReceivedProtobuf(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r) override;

  private:
    // Call to add unknown hops (e.g. when a node couldn't decrypt it) to the route based on hopStart and current hopLimit
    void insertUnknownHops(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r);

    // Call to add your ID to the route array of a RouteDiscovery message
    void appendMyID(meshtastic_RouteDiscovery *r);

    /* Call to print the route array of a RouteDiscovery message.
       Set origin to where the request came from.
       Set dest to the ID of its destination, or NODENUM_BROADCAST if it has not yet arrived there. */
    void printRoute(meshtastic_RouteDiscovery *r, uint32_t origin, uint32_t dest);
};

extern TraceRouteModule *traceRouteModule;