#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "input/InputBroker.h"
#if HAS_SCREEN
#include "OLEDDisplayUi.h"
#endif
#include <vector>

#define ROUTE_SIZE sizeof(((meshtastic_RouteDiscovery *)0)->route) / sizeof(((meshtastic_RouteDiscovery *)0)->route[0])

/**
 * A module that traces the route to a certain destination node
 */
enum TraceRouteRunState { TRACEROUTE_STATE_IDLE, TRACEROUTE_STATE_TRACKING, TRACEROUTE_STATE_RESULT, TRACEROUTE_STATE_COOLDOWN };

class TraceRouteModule : public ProtobufModule<meshtastic_RouteDiscovery>,
                         public Observable<const UIFrameEvent *>,
                         private concurrency::OSThread
{
  public:
    TraceRouteModule();

    bool startTraceRoute(NodeNum node);
    void launch(NodeNum node);
    void handleTraceRouteResult(const String &result);
    bool shouldDraw();
#if HAS_SCREEN
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    const char *getNodeName(NodeNum node);

    virtual bool wantUIFrame() override { return shouldDraw(); }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }

    void processUpgradedPacket(const meshtastic_MeshPacket &mp);

  protected:
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r) override;

    virtual meshtastic_MeshPacket *allocReply() override;

    /* Called before rebroadcasting a RouteDiscovery payload in order to update
       the route array containing the IDs of nodes this packet went through */
    void alterReceivedProtobuf(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r) override;

    virtual int32_t runOnce() override;

  private:
    void setResultText(const String &text);
    void clearResultLines();
#if HAS_SCREEN
    void rebuildResultLines(OLEDDisplay *display);
#endif
    // Call to add unknown hops (e.g. when a node couldn't decrypt it) to the route based on hopStart and current hopLimit
    void insertUnknownHops(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r, bool isTowardsDestination);

    // Call to add your ID to the route array of a RouteDiscovery message
    void appendMyIDandSNR(meshtastic_RouteDiscovery *r, float snr, bool isTowardsDestination, bool SNRonly);

    // Update next-hops in the routing table based on the returned route
    void updateNextHops(meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r);

    // Helper to update next-hop for a single node
    void maybeSetNextHop(NodeNum target, uint8_t nextHopByte);

    /* Call to print the route array of a RouteDiscovery message.
       Set origin to where the request came from.
       Set dest to the ID of its destination, or NODENUM_BROADCAST if it has not yet arrived there. */
    void printRoute(meshtastic_RouteDiscovery *r, uint32_t origin, uint32_t dest, bool isTowardsDestination);

    TraceRouteRunState runState = TRACEROUTE_STATE_IDLE;
    unsigned long lastTraceRouteTime = 0;
    unsigned long resultShowTime = 0;
    unsigned long cooldownMs = 30000;
    unsigned long resultDisplayMs = 10000;
    unsigned long trackingTimeoutMs = 10000;
    String bannerText;
    String resultText;
    std::vector<String> resultLines;
    bool resultLinesDirty = false;
    NodeNum tracingNode = 0;
    bool initialized = false;
};

extern TraceRouteModule *traceRouteModule;
