#pragma once
#include "Observer.h"
#include "SinglePortModule.h"

/**
 * Waypoint message handling for meshtastic
 */
class WaypointModule : public SinglePortModule, public Observable<const UIFrameEvent *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    WaypointModule() : SinglePortModule("waypoint", meshtastic_PortNum_WAYPOINT_APP) {}
#if HAS_SCREEN
    bool shouldDraw();
#endif
  protected:
    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */

    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
#if HAS_SCREEN
    virtual bool wantUIFrame() override { return this->shouldDraw(); }
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern WaypointModule *waypointModule;