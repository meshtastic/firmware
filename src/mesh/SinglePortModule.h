#pragma once
#include "MeshModule.h"
#include "Router.h"

/**
 * Most modules are only interested in sending/receiving one particular portnum.  This baseclass simplifies that common
 * case.
 */
class SinglePortModule : public MeshModule
{
  protected:
    meshtastic_PortNum ourPortNum;

  public:
    /** Constructor
     * name is for debugging output
     */
    SinglePortModule(const char *_name, meshtastic_PortNum _ourPortNum) : MeshModule(_name), ourPortNum(_ourPortNum) {}

  protected:
    /**
     * @return true if you want to receive the specified portnum
     */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == ourPortNum; }

    /**
     * Return a mesh packet which has been preinited as a data packet with a particular port number.
     * You can then send this packet (after customizing any of the payload fields you might need) with
     * service.sendToMesh()
     */
    meshtastic_MeshPacket *allocDataPacket()
    {
        // Update our local node info with our position (even if we don't decide to update anyone else)
        meshtastic_MeshPacket *p = router->allocForSending();
        p->decoded.portnum = ourPortNum;

        return p;
    }
};
