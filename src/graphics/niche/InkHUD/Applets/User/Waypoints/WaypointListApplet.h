#if defined(MESHTASTIC_INCLUDE_INKHUD)

#pragma once

#include "configuration.h"

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#include <deque>
#include <string>

namespace NicheGraphics::InkHUD
{

class WaypointListApplet : public Applet, public SinglePortModule, public concurrency::OSThread
{
  public:
    WaypointListApplet();

    void onActivate() override;
    void onDeactivate() override;
    void onRender(bool full) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    void onNavUp() override;
    void onNavDown() override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;

  protected:
    int32_t runOnce() override;

  private:
    struct WaypointCard {
        uint32_t id = 0;
        bool has_latitude_i = false;
        bool has_longitude_i = false;
        int32_t latitude_i = 0;
        int32_t longitude_i = 0;
        uint32_t expire = 0;
        uint32_t icon = 0;
        bool has_geofence = false;
        char name[31] = {};
        char description[101] = {};
    };

    static constexpr size_t MAX_WAYPOINTS = 10;

    void ingestWaypoint(const meshtastic_Waypoint &wp);
    bool removeWaypointById(uint32_t id);
    bool pruneExpiredWaypoints();
    void seedFromStore();
    void updateRefreshTimer();
    uint8_t visibleRows(uint8_t start, bool landscape);
    uint8_t rowHeight(const WaypointCard &entry, bool landscape);
    uint8_t maxScrollOffset(bool landscape);
    void scrollBy(int delta);
    bool tryGetOwnPosition(meshtastic_PositionLite &out);
    uint32_t nextExpiryUpdateMs(uint32_t secondsLeft);
    uint32_t nextRefreshIntervalMs();
    uint32_t buildRenderHash();
    bool fillWaypointCard(const meshtastic_Waypoint &wp, WaypointCard &entry);
    void syncListState();

    std::string headerText(bool landscape);
    std::string waypointName(const WaypointCard &entry);
    std::string waypointDescription(const WaypointCard &entry);
    std::string coordinateText(const WaypointCard &entry, bool landscape);
    std::string distanceText(const WaypointCard &entry);
    std::string expireText(uint32_t expireEpoch);
    std::string geofenceText(const WaypointCard &entry);
    std::string utf8FromCodepoint(uint32_t codepoint);
    bool canRenderWaypointIcon(const WaypointCard &entry, std::string *mapped = nullptr);
    uint8_t fallbackBadgeNumber(const WaypointCard &entry);
    bool drawWaypointIcon(const WaypointCard &entry, int16_t left, int16_t centerY, uint16_t boxSize);
    void drawFallbackIcon(const WaypointCard &entry, int16_t left, int16_t rowTop, uint16_t boxWidth, uint16_t rowHeight);
    bool hasDescription(const WaypointCard &entry);

    std::deque<WaypointCard> waypoints;
    uint8_t scrollOffset = 0;
    uint32_t lastRenderHash = 0;
    bool hasRenderHash = false;
};

} // namespace NicheGraphics::InkHUD

#endif
