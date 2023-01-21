#pragma once
#include "Observer.h"
#include "SinglePortModule.h"

/**
 * Waypoint message handling for meshtastic
 */
class WaypointModule : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    WaypointModule() : SinglePortModule("waypoint", meshtastic_PortNum_WAYPOINT_APP) {}

  protected:
    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern WaypointModule *waypointModule;
