#if defined(MESHTASTIC_INCLUDE_INKHUD)

#pragma once

#include "configuration.h"

#include "Observer.h"
#include "WaypointStore.h"
#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#include <string>

namespace NicheGraphics::InkHUD
{

class WaypointListApplet : public Applet, public concurrency::OSThread
{
  public:
    WaypointListApplet();

    void onActivate() override;
    void onDeactivate() override;
    void onRender(bool full) override;
    void onNavUp() override;
    void onNavDown() override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;

    WaypointListApplet *asWaypointListApplet() override { return this; } // Identify as WaypointListApplet without RTTI

    // Read-only access for MenuApplet's "Remove Waypoint" page
    size_t waypointCount() const { return waypointStore.getWaypoints().size(); }
    uint32_t waypointIdAt(size_t index) const { return waypointStore.getWaypoints().at(index).waypoint.id; }
    std::string waypointLabelAt(size_t index) { return waypointName(waypointStore.getWaypoints().at(index).waypoint); }

  protected:
    int32_t runOnce() override;

  private:
    void updateRefreshTimer();
    uint8_t visibleRows(uint8_t start);
    uint8_t rowHeight(const meshtastic_Waypoint &waypoint);
    uint8_t maxScrollOffset();
    void scrollBy(int delta);
    bool rowIndexAt(int16_t y, uint8_t &indexOut); // Which waypoint row is at this y, if any
    bool tryGetOwnPosition(meshtastic_PositionLite &out);
    uint32_t nextExpiryUpdateMs(uint32_t secondsLeft);
    uint32_t nextRefreshIntervalMs();
    uint32_t buildRenderHash();
    void syncListState();

    std::string headerText();
    std::string waypointName(const meshtastic_Waypoint &waypoint);
    std::string waypointDescription(const meshtastic_Waypoint &waypoint);
    std::string coordinateText(const meshtastic_Waypoint &waypoint, bool landscape);
    std::string distanceText(const meshtastic_Waypoint &waypoint);
    std::string expireText(uint32_t expireEpoch);
    bool canRenderWaypointIcon(const meshtastic_Waypoint &waypoint, std::string *mapped = nullptr);
    uint8_t fallbackBadgeNumber(const meshtastic_Waypoint &waypoint);
    bool drawWaypointIcon(const meshtastic_Waypoint &waypoint, int16_t left, int16_t centerY, uint16_t boxSize);
    void drawFallbackIcon(const meshtastic_Waypoint &waypoint, int16_t left, int16_t rowTop, uint16_t boxWidth,
                          uint16_t rowHeight);
    bool hasDescription(const meshtastic_Waypoint &waypoint);

    int onWaypointStoreChanged(const WaypointStore *store);
    CallbackObserver<WaypointListApplet, const WaypointStore *> waypointStoreObserver =
        CallbackObserver<WaypointListApplet, const WaypointStore *>(this, &WaypointListApplet::onWaypointStoreChanged);

    uint8_t scrollOffset = 0;
    uint32_t lastRenderHash = 0;
    bool hasRenderHash = false;
};

} // namespace NicheGraphics::InkHUD

#endif
