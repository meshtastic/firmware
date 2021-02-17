#pragma once
#include "MeshPlugin.h"
#include "Router.h"

/**
 * Most plugins are only interested in sending/receving one particular portnum.  This baseclass simplifies that common
 * case.
 */
class SinglePortPlugin : public MeshPlugin
{
  protected:
    PortNum ourPortNum;

  public:
    /** Constructor
     * name is for debugging output
     */
    SinglePortPlugin(const char *_name, PortNum _ourPortNum) : MeshPlugin(_name), ourPortNum(_ourPortNum) {}

  protected:
    /**
     * @return true if you want to receive the specified portnum
     */
    virtual bool wantPacket(const MeshPacket *p) { return p->decoded.portnum == ourPortNum; }

    /**
     * Return a mesh packet which has been preinited as a data packet with a particular port number.
     * You can then send this packet (after customizing any of the payload fields you might need) with
     * service.sendToMesh()
     */
    MeshPacket *allocDataPacket()
    {
        // Update our local node info with our position (even if we don't decide to update anyone else)
        MeshPacket *p = router->allocForSending();
        p->decoded.portnum = ourPortNum;

        return p;
    }
};
