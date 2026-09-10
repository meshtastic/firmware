#if defined(MESHTASTIC_INCLUDE_INKHUD)

#include "./WaypointListApplet.h"

#include "GeoCoord.h"
#include "NodeDB.h"
#include "RTC.h"
#include "WaypointStore.h"
#include "WaypointUtils.h"
#include "modules/WaypointModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace NicheGraphics;

namespace
{

uint32_t fnv1aAppend(uint32_t hash, const char *text)
{
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= 16777619u;
    }

    return hash;
}

} // namespace

InkHUD::WaypointListApplet::WaypointListApplet() : concurrency::OSThread("WaypointListApplet")
{
    OSThread::disable();
}

void InkHUD::WaypointListApplet::onActivate()
{
    setInputsSubscribed(NAV_UP | NAV_DOWN, true);
    scrollOffset = 0;
    hasRenderHash = false;
    waypointStoreObserver.observe(&waypointStore);
    waypointStore.purgeExpired();
    updateRefreshTimer();
}

void InkHUD::WaypointListApplet::onDeactivate()
{
    waypointStoreObserver.unobserve(&waypointStore);
    setInputsSubscribed(NAV_UP | NAV_DOWN, false);
    OSThread::disable();
}

int32_t InkHUD::WaypointListApplet::runOnce()
{
    bool needsUpdate = false;
    if (isActive())
        waypointStore.purgeExpired();

    if (isActive() && !waypointStore.getWaypoints().empty()) {
        const uint32_t renderHash = buildRenderHash();
        if (!hasRenderHash || renderHash != lastRenderHash) {
            lastRenderHash = renderHash;
            hasRenderHash = true;
            needsUpdate = true;
        }

        updateRefreshTimer();
    }

    if (isActive() && needsUpdate)
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);

    return OSThread::interval;
}

void InkHUD::WaypointListApplet::updateRefreshTimer()
{
    if (waypointStore.getWaypoints().empty()) {
        OSThread::disable();
        return;
    }

    OSThread::enabled = true;
    OSThread::setIntervalFromNow(nextRefreshIntervalMs());
}

bool InkHUD::WaypointListApplet::hasDescription(const meshtastic_Waypoint &waypoint)
{
    return waypoint.description[0] != '\0';
}

uint8_t InkHUD::WaypointListApplet::rowHeight(const meshtastic_Waypoint &waypoint)
{
    const uint8_t lines = hasDescription(waypoint) ? 3 : 2;
    return (fontSmall.lineHeight() * lines) + lines;
}

uint8_t InkHUD::WaypointListApplet::visibleRows(uint8_t start)
{
    const auto &waypoints = waypointStore.getWaypoints();
    const int16_t contentTop = getHeaderHeight() + 2;
    const uint16_t availableH = (height() > contentTop) ? (height() - contentTop) : 1;
    if (waypoints.empty() || start >= waypoints.size())
        return 0;

    uint16_t usedH = 0;
    uint8_t count = 0;
    for (uint8_t i = start; i < waypoints.size(); ++i) {
        const uint8_t nextH = rowHeight(waypoints.at(i).waypoint);
        if (count > 0 && usedH + nextH > availableH)
            break;

        usedH += nextH;
        ++count;

        if (usedH >= availableH)
            break;
    }

    return count;
}

uint8_t InkHUD::WaypointListApplet::maxScrollOffset()
{
    const auto &waypoints = waypointStore.getWaypoints();
    if (waypoints.empty())
        return 0;

    const int16_t contentTop = getHeaderHeight() + 2;
    const uint16_t availableH = (height() > contentTop) ? (height() - contentTop) : 1;
    uint16_t usedH = 0;
    uint8_t start = (uint8_t)waypoints.size();

    while (start > 0) {
        const uint8_t nextH = rowHeight(waypoints.at(start - 1).waypoint);
        if (usedH > 0 && usedH + nextH > availableH)
            break;

        usedH += nextH;
        --start;

        if (usedH >= availableH)
            break;
    }

    return start;
}

bool InkHUD::WaypointListApplet::rowIndexAt(int16_t y, uint8_t &indexOut)
{
    const auto &waypoints = waypointStore.getWaypoints();
    if (waypoints.empty())
        return false;

    const uint8_t start = std::min<uint8_t>(scrollOffset, (uint8_t)waypoints.size() - 1);
    const uint8_t rows = visibleRows(start);
    const uint8_t end = std::min<uint8_t>((uint8_t)waypoints.size(), start + rows);

    // Walk the same row layout used by onRender, stopping at whichever row contains y
    int16_t rowTop = getHeaderHeight() + 2;
    for (uint8_t i = start; i < end; ++i) {
        const uint8_t rowH = rowHeight(waypoints.at(i).waypoint);
        if (y < rowTop + rowH) {
            indexOut = i;
            return true;
        }
        rowTop += rowH;
    }

    return false;
}

void InkHUD::WaypointListApplet::scrollBy(int delta)
{
    const int next = std::clamp<int>((int)scrollOffset + delta, 0, maxScrollOffset());
    if (next == scrollOffset)
        return;

    scrollOffset = (uint8_t)next;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::WaypointListApplet::onNavUp()
{
    scrollBy(-1);
}

void InkHUD::WaypointListApplet::onNavDown()
{
    scrollBy(1);
}

bool InkHUD::WaypointListApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    (void)x;
    const auto &waypoints = waypointStore.getWaypoints();
    if (waypoints.empty() || y < getHeaderHeight())
        return false;

    // Long press a row to delete that waypoint (broadcasts the deletion to the mesh too)
    if (longPress) {
        uint8_t index = 0;
        if (!rowIndexAt(y, index))
            return false;

        if (waypointModule)
            waypointModule->broadcastDelete(waypoints.at(index).waypoint.id);
        return true;
    }

    const uint16_t midpoint = getHeaderHeight() + ((height() - getHeaderHeight()) / 2);
    scrollBy(y < midpoint ? -1 : 1);
    return true;
}

int InkHUD::WaypointListApplet::onWaypointStoreChanged(const WaypointStore *store)
{
    (void)store;
    if (!isActive())
        return 0;

    syncListState();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    return 0;
}

std::string InkHUD::WaypointListApplet::headerText()
{
    const auto &waypoints = waypointStore.getWaypoints();
    if (waypoints.empty())
        return "Waypoints";

    const uint8_t rows = visibleRows(scrollOffset);
    const uint8_t first = scrollOffset + 1;
    const uint8_t last = std::min<uint8_t>((uint8_t)waypoints.size(), scrollOffset + rows);

    char buf[32];
    snprintf(buf, sizeof(buf), "Waypoints %u-%u/%u", first, last, (unsigned)waypoints.size());
    return buf;
}

std::string InkHUD::WaypointListApplet::waypointName(const meshtastic_Waypoint &waypoint)
{
    if (waypoint.name[0])
        return parse(waypoint.name);

    char buf[20];
    snprintf(buf, sizeof(buf), "Waypoint 0x%x", (unsigned)waypoint.id);
    return buf;
}

std::string InkHUD::WaypointListApplet::waypointDescription(const meshtastic_Waypoint &waypoint)
{
    if (!hasDescription(waypoint))
        return "";

    return parse(waypoint.description);
}

std::string InkHUD::WaypointListApplet::coordinateText(const meshtastic_Waypoint &waypoint, bool landscape)
{
    if (!waypoint.has_latitude_i || !waypoint.has_longitude_i)
        return "--";

    const uint8_t decimals = landscape ? (width() >= 220 ? 4 : 3) : (width() >= 140 ? 3 : 2);
    const double lat = waypoint.latitude_i * 1e-7;
    const double lon = waypoint.longitude_i * 1e-7;

    char buf[40];
    snprintf(buf, sizeof(buf), "%.*f,%.*f", decimals, lat, decimals, lon);
    return buf;
}

bool InkHUD::WaypointListApplet::tryGetOwnPosition(meshtastic_PositionLite &out)
{
    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    return ourNode && nodeDB->copyNodePosition(ourNode->num, out) && (out.latitude_i != 0 || out.longitude_i != 0);
}

std::string InkHUD::WaypointListApplet::distanceText(const meshtastic_Waypoint &waypoint)
{
    if (!waypoint.has_latitude_i || !waypoint.has_longitude_i)
        return "";

    meshtastic_PositionLite ownPos = meshtastic_PositionLite_init_zero;
    if (!tryGetOwnPosition(ownPos))
        return "";

    const float meters = GeoCoord::latLongToMeter(waypoint.latitude_i * 1e-7, waypoint.longitude_i * 1e-7,
                                                  ownPos.latitude_i * 1e-7, ownPos.longitude_i * 1e-7);
    if (meters < 0)
        return "";

    return localizeDistance((uint32_t)std::lround(meters));
}

std::string InkHUD::WaypointListApplet::expireText(uint32_t expireEpoch)
{
    if (expireEpoch == 0)
        return "--";

    const uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);
    if (now == 0)
        return "";
    if (expireEpoch <= now)
        return "exp";

    const uint32_t left = expireEpoch - now;
    char buf[12];
    if (left < 3600)
        snprintf(buf, sizeof(buf), "%lum", (unsigned long)((left + 59) / 60));
    else if (left < 86400)
        snprintf(buf, sizeof(buf), "%luh", (unsigned long)((left + 3599) / 3600));
    else
        snprintf(buf, sizeof(buf), "%lud", (unsigned long)((left + 86399) / 86400));
    return buf;
}

uint32_t InkHUD::WaypointListApplet::nextExpiryUpdateMs(uint32_t secondsLeft)
{
    if (secondsLeft < 60)
        return secondsLeft * 1000UL;

    const uint32_t step = (secondsLeft < 3600) ? 60UL : (secondsLeft < 86400 ? 3600UL : 86400UL);
    return ((((secondsLeft - 1) % step) + 1) * 1000UL);
}

uint32_t InkHUD::WaypointListApplet::nextRefreshIntervalMs()
{
    static constexpr uint32_t WAITING_STATE_REFRESH_MS = 1000UL;
    static constexpr uint32_t GPS_DISTANCE_REFRESH_MS = 5000UL;
    static constexpr uint32_t IDLE_REFRESH_MS = 60000UL;

    uint32_t intervalMs = UINT32_MAX;
    const uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);

    meshtastic_PositionLite ownPos = meshtastic_PositionLite_init_zero;
    const bool haveOwnPos = tryGetOwnPosition(ownPos);

    for (const StoredWaypoint &entry : waypointStore.getWaypoints()) {
        const meshtastic_Waypoint &waypoint = entry.waypoint;
        if (waypoint.expire != 0) {
            if (now == 0) {
                intervalMs = std::min(intervalMs, WAITING_STATE_REFRESH_MS);
            } else if (waypoint.expire > now) {
                intervalMs = std::min(intervalMs, nextExpiryUpdateMs(waypoint.expire - now));
            }
        }

        if (waypoint.has_latitude_i && waypoint.has_longitude_i) {
            if (!haveOwnPos) {
                intervalMs = std::min(intervalMs, WAITING_STATE_REFRESH_MS);
            } else if (!config.position.fixed_position) {
                intervalMs = std::min(intervalMs, GPS_DISTANCE_REFRESH_MS);
            }
        }
    }

    if (intervalMs == UINT32_MAX)
        return IDLE_REFRESH_MS;

    return intervalMs;
}

uint32_t InkHUD::WaypointListApplet::buildRenderHash()
{
    uint32_t hash = 2166136261u;

    for (const StoredWaypoint &entry : waypointStore.getWaypoints()) {
        const meshtastic_Waypoint &waypoint = entry.waypoint;
        char idBuf[11];
        snprintf(idBuf, sizeof(idBuf), "%lu", (unsigned long)waypoint.id);
        hash = fnv1aAppend(hash, idBuf);
        hash = fnv1aAppend(hash, "|");

        const std::string distance = distanceText(waypoint);
        hash = fnv1aAppend(hash, distance.c_str());
        hash = fnv1aAppend(hash, "|");

        const std::string expire = expireText(waypoint.expire);
        hash = fnv1aAppend(hash, expire.c_str());
        hash = fnv1aAppend(hash, ";");
    }

    return hash;
}

void InkHUD::WaypointListApplet::syncListState()
{
    scrollOffset = std::min<uint8_t>(scrollOffset, maxScrollOffset());
    hasRenderHash = false;
    // Re-arm the timer whenever visible waypoint state changes.
    updateRefreshTimer();
}

bool InkHUD::WaypointListApplet::canRenderWaypointIcon(const meshtastic_Waypoint &waypoint, std::string *mapped)
{
    if (!waypoint.icon)
        return false;

    const std::string utf8 = WaypointUtils::utf8FromCodepoint(waypoint.icon);
    if (utf8.empty())
        return false;

    const std::string glyph = getFont().decodeUTF8(utf8);
    if (glyph.size() != 1 || glyph[0] == '\x1A' || glyph[0] == '\x7F')
        return false;

    if (mapped)
        *mapped = glyph;
    return true;
}

uint8_t InkHUD::WaypointListApplet::fallbackBadgeNumber(const meshtastic_Waypoint &waypoint)
{
    uint8_t badge = 0;

    for (auto it = waypointStore.getWaypoints().rbegin(); it != waypointStore.getWaypoints().rend(); ++it) {
        const meshtastic_Waypoint &candidate = it->waypoint;
        if (canRenderWaypointIcon(candidate))
            continue;

        if (candidate.id == waypoint.id)
            return badge;

        if (badge < 9)
            ++badge;
    }

    return 0;
}

bool InkHUD::WaypointListApplet::drawWaypointIcon(const meshtastic_Waypoint &waypoint, int16_t left, int16_t centerY,
                                                  uint16_t boxSize)
{
    std::string mappedGlyph;
    if (!canRenderWaypointIcon(waypoint, &mappedGlyph))
        return false;

    printAt(left + (boxSize / 2), centerY, mappedGlyph, CENTER, MIDDLE);
    return true;
}

void InkHUD::WaypointListApplet::drawFallbackIcon(const meshtastic_Waypoint &waypoint, int16_t left, int16_t rowTop,
                                                  uint16_t boxWidth, uint16_t rowHeight)
{
    char badgeText[3];
    snprintf(badgeText, sizeof(badgeText), "%u", (unsigned)fallbackBadgeNumber(waypoint));

    setFont(fontSmall);

    const int16_t cx = left + (boxWidth / 2);
    const uint16_t badgeTextW = std::max<uint16_t>(getTextWidth(badgeText), getTextWidth("0"));
    const int16_t centerY = rowTop + (fontSmall.lineHeight() / 2) + 1;
    const int16_t boxSize = std::max<int16_t>((int16_t)boxWidth, (int16_t)(badgeTextW + 5));
    const int16_t radius = std::max<int16_t>(2, boxSize / 6);
    const int16_t boxLeft = cx - (boxSize / 2);
    const int16_t boxTop = centerY - (boxSize / 2);

    // Match the boxed fallback marker style used on the map.
    fillRoundRect(boxLeft, boxTop, boxSize, boxSize, radius, WHITE);
    drawRoundRect(boxLeft, boxTop, boxSize, boxSize, radius, BLACK);

    setCrop(left - 1, rowTop, boxWidth + 3, rowHeight);
    printAt(cx, centerY + 1, badgeText, CENTER, MIDDLE);
    resetCrop();
}

void InkHUD::WaypointListApplet::onRender(bool full)
{
    (void)full;

    const bool landscape = width() > height();
    const auto &waypoints = waypointStore.getWaypoints();
    drawHeader(headerText());

    if (waypoints.empty()) {
        setFont(fontMedium);
        printAt(X(0.5f), Y(0.5f), "No Waypoints", CENTER, MIDDLE);
        return;
    }

    setFont(fontSmall);

    const int16_t contentTop = getHeaderHeight() + 2;
    const uint8_t start = std::min<uint8_t>(scrollOffset, (uint8_t)waypoints.size() - 1);
    const uint8_t rows = visibleRows(start);
    const uint8_t end = std::min<uint8_t>((uint8_t)waypoints.size(), start + rows);
    const uint16_t iconW = fontSmall.lineHeight();
    const uint16_t gap = 2;

    auto ellipsizeToWidth = [this](std::string text, uint16_t maxWidth) {
        constexpr const char *ellipsis = "...";
        const uint16_t ellipsisW = getTextWidth(ellipsis);
        uint16_t textW = getTextWidth(text);
        if (maxWidth == 0)
            return std::string();
        if (textW <= maxWidth)
            return text;
        if (ellipsisW > maxWidth)
            return std::string();
        while (!text.empty() && (textW + ellipsisW > maxWidth)) {
            text.pop_back();
            textW = getTextWidth(text);
        }
        return text + ellipsis;
    };

    int16_t rowTop = contentTop;
    for (uint8_t i = start; i < end; ++i) {
        const meshtastic_Waypoint &waypoint = waypoints.at(i).waypoint;
        const uint8_t rowH = rowHeight(waypoint);
        const int16_t line1Y = rowTop + (fontSmall.lineHeight() / 2) + 1;
        const int16_t line2Y = rowTop + fontSmall.lineHeight() + 1;
        const int16_t metaY =
            rowTop + (hasDescription(waypoint) ? ((fontSmall.lineHeight() * 2) + 2) : (fontSmall.lineHeight() + 1));

        if (!drawWaypointIcon(waypoint, 1, line1Y, iconW - 1))
            drawFallbackIcon(waypoint, 0, rowTop, iconW, rowH);

        const std::string name = waypointName(waypoint);
        const std::string description = waypointDescription(waypoint);
        const std::string distance = distanceText(waypoint);
        const std::string coord = coordinateText(waypoint, landscape);
        const std::string expire = expireText(waypoint.expire);

        const int16_t nameLeft = iconW + gap;
        int16_t nameRight = width() - 1;
        if (!distance.empty()) {
            printAt(nameRight, line1Y, distance, RIGHT, MIDDLE);
            nameRight -= getTextWidth(distance) + gap;
        }

        const uint16_t nameWidth = (nameRight >= nameLeft) ? ((nameRight - nameLeft) + 1) : 0;
        const std::string shown = ellipsizeToWidth(name, nameWidth);
        const uint16_t shownWidth = getTextWidth(shown);
        setCrop(nameLeft, rowTop, nameWidth, fontSmall.lineHeight() + 2);
        printThick(nameLeft + (shownWidth / 2), line1Y, shown, 2, 1);
        resetCrop();

        if (!description.empty()) {
            const std::string descShown = ellipsizeToWidth(description, nameWidth);
            setCrop(nameLeft, line2Y - 1, nameWidth, fontSmall.lineHeight() + 2);
            printAt(nameLeft, line2Y, descShown, LEFT, TOP);
            resetCrop();
        }

        int16_t metaRight = width() - 1;
        if (!expire.empty()) {
            printAt(metaRight, metaY, expire, RIGHT, TOP);
            metaRight -= getTextWidth(expire) + gap;
        }

        const uint16_t coordWidth = (metaRight >= nameLeft) ? ((metaRight - nameLeft) + 1) : 0;
        const std::string coordShown = ellipsizeToWidth(coord, coordWidth);
        setCrop(nameLeft, metaY - 1, coordWidth, fontSmall.lineHeight() + 2);
        printAt(nameLeft, metaY, coordShown, LEFT, TOP);
        resetCrop();

        const int16_t separatorY = rowTop + rowH - 1;
        if (separatorY < height() - 1 && i + 1 < end) {
            for (int16_t x = 0; x < width(); x += 2)
                drawPixel(x, separatorY, BLACK);
        }

        rowTop += rowH;
    }
}

#endif
