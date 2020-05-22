#include "ReliableRouter.h"

class DSRRouter : public ReliableRouter
{

  protected:
    /**
     * Every (non duplicate) packet this node receives will be passed through this method.  This allows subclasses to
     * update routing tables etc... based on what we overhear (even for messages not destined to our node)
     */
    virtual void sniffReceived(const MeshPacket *p);

  private:
    /**
     * Does our node appear in the specified route
     */
    bool weAreInRoute(const RouteDiscovery &route);

    /**
     * Given a DSR route, use that route to update our DB of possible routes
     **/
    void updateRoutes(const RouteDiscovery &route, bool reverse);

    /**
     * send back a route reply (the sender address will be first in the list)
     */
    void sendRouteReply(const RouteDiscovery &route, NodeNum toAppend = 0);

    /**
     * Given a nodenum return the next node we should forward to if we want to reach that node.
     *
     * @return 0 if no route found
     */
    NodeNum getNextHop(NodeNum dest);

    /** Not in our route cache, rebroadcast on their behalf (after adding ourselves to the request route)
     */
    void resendRouteRequest(const MeshPacket *p);
};