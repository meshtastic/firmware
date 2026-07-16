#if defined(MESHTASTIC_INCLUDE_INKHUD)

#include "./WaypointListApplet.h"

#include "GeoCoord.h"
#include "NodeDB.h"
#include "RTC.h"
#include "WaypointStore.h"
#include "modules/WaypointModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

InkHUD::WaypointListApplet::WaypointListApplet()
    : SinglePortModule("WaypointListApplet", meshtastic_PortNum_WAYPOINT_APP), concurrency::OSThread("WaypointListApplet")
{
    OSThread::disable();
}

void InkHUD::WaypointListApplet::onActivate()
{
    loopbackOk = true;
    setInputsSubscribed(NAV_UP | NAV_DOWN, true);
    seedFromStore();
    updateRefreshTimer();
}

void InkHUD::WaypointListApplet::onDeactivate()
{
    loopbackOk = false;
    setInputsSubscribed(NAV_UP | NAV_DOWN, false);
    OSThread::disable();
}

int32_t InkHUD::WaypointListApplet::runOnce()
{
    bool needsUpdate = false;
    if (isActive() && pruneExpiredWaypoints())
        needsUpdate = true;

    if (isActive() && !waypoints.empty()) {
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

void InkHUD::WaypointListApplet::seedFromStore()
{
    waypoints.clear();
    scrollOffset = 0;
    hasRenderHash = false;

    const auto &storedWaypoints = waypointStore.getWaypoints();
    for (auto it = storedWaypoints.rbegin(); it != storedWaypoints.rend(); ++it) {
        WaypointCard entry;
        if (!fillWaypointCard(it->waypoint, entry))
            continue;

        waypoints.push_front(entry);
        if (waypoints.size() > MAX_WAYPOINTS)
            waypoints.resize(MAX_WAYPOINTS);
    }

    syncListState();
}

void InkHUD::WaypointListApplet::updateRefreshTimer()
{
    if (waypoints.empty()) {
        OSThread::disable();
        return;
    }

    OSThread::enabled = true;
    OSThread::setIntervalFromNow(nextRefreshIntervalMs());
}

bool InkHUD::WaypointListApplet::removeWaypointById(uint32_t id)
{
    for (auto it = waypoints.begin(); it != waypoints.end(); ++it) {
        if (it->id == id) {
            waypoints.erase(it);
            syncListState();
            return true;
        }
    }

    return false;
}

bool InkHUD::WaypointListApplet::pruneExpiredWaypoints()
{
    const uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);
    if (now == 0)
        return false;

    bool removed = false;
    for (auto it = waypoints.begin(); it != waypoints.end();) {
        if (it->expire != 0 && it->expire <= now) {
            it = waypoints.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        syncListState();
    }

    return removed;
}

bool InkHUD::WaypointListApplet::hasDescription(const WaypointCard &entry)
{
    return entry.description[0] != '\0';
}

uint8_t InkHUD::WaypointListApplet::rowHeight(const WaypointCard &entry, bool landscape)
{
    (void)landscape;
    const uint8_t lines = hasDescription(entry) ? 3 : 2;
    return (fontSmall.lineHeight() * lines) + lines;
}

uint8_t InkHUD::WaypointListApplet::visibleRows(uint8_t start, bool landscape)
{
    const int16_t contentTop = getHeaderHeight() + 2;
    const uint16_t availableH = (height() > contentTop) ? (height() - contentTop) : 1;
    if (waypoints.empty() || start >= waypoints.size())
        return 0;

    uint16_t usedH = 0;
    uint8_t count = 0;
    for (uint8_t i = start; i < waypoints.size(); ++i) {
        const uint8_t nextH = rowHeight(waypoints.at(i), landscape);
        if (count > 0 && usedH + nextH > availableH)
            break;

        usedH += nextH;
        ++count;

        if (usedH >= availableH)
            break;
    }

    return count;
}

uint8_t InkHUD::WaypointListApplet::maxScrollOffset(bool landscape)
{
    if (waypoints.empty())
        return 0;

    const int16_t contentTop = getHeaderHeight() + 2;
    const uint16_t availableH = (height() > contentTop) ? (height() - contentTop) : 1;
    uint16_t usedH = 0;
    uint8_t start = (uint8_t)waypoints.size();

    while (start > 0) {
        const uint8_t nextH = rowHeight(waypoints.at(start - 1), landscape);
        if (usedH > 0 && usedH + nextH > availableH)
            break;

        usedH += nextH;
        --start;

        if (usedH >= availableH)
            break;
    }

    return start;
}

bool InkHUD::WaypointListApplet::rowIndexAt(int16_t y, bool landscape, uint8_t &indexOut)
{
    if (waypoints.empty())
        return false;

    const uint8_t start = std::min<uint8_t>(scrollOffset, (uint8_t)waypoints.size() - 1);
    const uint8_t rows = visibleRows(start, landscape);
    const uint8_t end = std::min<uint8_t>((uint8_t)waypoints.size(), start + rows);

    // Walk the same row layout used by onRender, stopping at whichever row contains y
    int16_t rowTop = getHeaderHeight() + 2;
    for (uint8_t i = start; i < end; ++i) {
        const uint8_t rowH = rowHeight(waypoints.at(i), landscape);
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
    const bool landscape = width() > height();
    const int next = std::clamp<int>((int)scrollOffset + delta, 0, maxScrollOffset(landscape));
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
    if (waypoints.empty() || y < getHeaderHeight())
        return false;

    const bool landscape = width() > height();

    // Long press a row to delete that waypoint (broadcasts the deletion to the mesh too)
    if (longPress) {
        uint8_t index = 0;
        if (!rowIndexAt(y, landscape, index))
            return false;

        if (waypointModule)
            waypointModule->broadcastDelete(waypoints.at(index).id);
        return true;
    }

    const uint16_t midpoint = getHeaderHeight() + ((height() - getHeaderHeight()) / 2);
    scrollBy(y < midpoint ? -1 : 1);
    return true;
}

void InkHUD::WaypointListApplet::ingestWaypoint(const meshtastic_Waypoint &wp)
{
    WaypointCard entry;
    if (!fillWaypointCard(wp, entry)) {
        removeWaypointById(wp.id);
        return;
    }

    removeWaypointById(entry.id);

    waypoints.push_front(entry);
    if (waypoints.size() > MAX_WAYPOINTS)
        waypoints.resize(MAX_WAYPOINTS);

    syncListState();
}

ProcessMessage InkHUD::WaypointListApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (!isActive())
        return ProcessMessage::CONTINUE;

    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Waypoint_msg, &wp))
        return ProcessMessage::CONTINUE;

    ingestWaypoint(wp);

    if (getFrom(&mp) != nodeDB->getNodeNum())
        requestAutoshow();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    return ProcessMessage::CONTINUE;
}

std::string InkHUD::WaypointListApplet::headerText(bool landscape)
{
    if (waypoints.empty())
        return "Waypoints";

    const uint8_t rows = visibleRows(scrollOffset, landscape);
    const uint8_t first = scrollOffset + 1;
    const uint8_t last = std::min<uint8_t>((uint8_t)waypoints.size(), scrollOffset + rows);

    char buf[32];
    snprintf(buf, sizeof(buf), "Waypoints %u-%u/%u", first, last, (unsigned)waypoints.size());
    return buf;
}

std::string InkHUD::WaypointListApplet::waypointName(const WaypointCard &entry)
{
    if (entry.name[0])
        return parse(entry.name);

    char buf[20];
    snprintf(buf, sizeof(buf), "Waypoint 0x%x", (unsigned)entry.id);
    return buf;
}

std::string InkHUD::WaypointListApplet::waypointDescription(const WaypointCard &entry)
{
    if (!hasDescription(entry))
        return "";

    return parse(entry.description);
}

std::string InkHUD::WaypointListApplet::coordinateText(const WaypointCard &entry, bool landscape)
{
    if (!entry.has_latitude_i || !entry.has_longitude_i)
        return "--";

    const uint8_t decimals = landscape ? (width() >= 220 ? 4 : 3) : (width() >= 140 ? 3 : 2);
    const double lat = entry.latitude_i * 1e-7;
    const double lon = entry.longitude_i * 1e-7;

    char buf[40];
    snprintf(buf, sizeof(buf), "%.*f,%.*f", decimals, lat, decimals, lon);
    return buf;
}

bool InkHUD::WaypointListApplet::tryGetOwnPosition(meshtastic_PositionLite &out)
{
    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    return ourNode && nodeDB->copyNodePosition(ourNode->num, out) && (out.latitude_i != 0 || out.longitude_i != 0);
}

std::string InkHUD::WaypointListApplet::distanceText(const WaypointCard &entry)
{
    if (!entry.has_latitude_i || !entry.has_longitude_i)
        return "";

    meshtastic_PositionLite ownPos = meshtastic_PositionLite_init_zero;
    if (!tryGetOwnPosition(ownPos))
        return "";

    const float meters = GeoCoord::latLongToMeter(entry.latitude_i * 1e-7, entry.longitude_i * 1e-7, ownPos.latitude_i * 1e-7,
                                                  ownPos.longitude_i * 1e-7);
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

    for (const WaypointCard &entry : waypoints) {
        if (entry.expire != 0) {
            if (now == 0) {
                intervalMs = std::min(intervalMs, WAITING_STATE_REFRESH_MS);
            } else if (entry.expire > now) {
                intervalMs = std::min(intervalMs, nextExpiryUpdateMs(entry.expire - now));
            }
        }

        if (entry.has_latitude_i && entry.has_longitude_i) {
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

    for (const WaypointCard &entry : waypoints) {
        char idBuf[11];
        snprintf(idBuf, sizeof(idBuf), "%lu", (unsigned long)entry.id);
        hash = fnv1aAppend(hash, idBuf);
        hash = fnv1aAppend(hash, "|");

        const std::string distance = distanceText(entry);
        hash = fnv1aAppend(hash, distance.c_str());
        hash = fnv1aAppend(hash, "|");

        const std::string expire = expireText(entry.expire);
        hash = fnv1aAppend(hash, expire.c_str());
        hash = fnv1aAppend(hash, ";");
    }

    return hash;
}

std::string InkHUD::WaypointListApplet::utf8FromCodepoint(uint32_t codepoint)
{
    char buf[5] = {};
    if (codepoint <= 0x7F) {
        buf[0] = static_cast<char>(codepoint);
        return std::string(buf, 1);
    }
    if (codepoint <= 0x7FF) {
        buf[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        buf[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 2);
    }
    if (codepoint <= 0xFFFF) {
        buf[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 3);
    }
    if (codepoint <= 0x10FFFF) {
        buf[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 4);
    }
    return "";
}

bool InkHUD::WaypointListApplet::fillWaypointCard(const meshtastic_Waypoint &wp, WaypointCard &entry)
{
    if (WaypointStore::isExpired(wp))
        return false;

    entry = {};
    entry.id = wp.id;
    entry.has_latitude_i = wp.has_latitude_i;
    entry.has_longitude_i = wp.has_longitude_i;
    entry.latitude_i = wp.latitude_i;
    entry.longitude_i = wp.longitude_i;
    entry.expire = wp.expire;
    entry.icon = wp.icon;
    strncpy(entry.name, wp.name, sizeof(entry.name) - 1);
    strncpy(entry.description, wp.description, sizeof(entry.description) - 1);
    return true;
}

void InkHUD::WaypointListApplet::syncListState()
{
    const bool landscape = width() > height();
    scrollOffset = std::min<uint8_t>(scrollOffset, maxScrollOffset(landscape));
    hasRenderHash = false;
    // Re-arm the timer whenever visible waypoint state changes.
    updateRefreshTimer();
}

bool InkHUD::WaypointListApplet::canRenderWaypointIcon(const WaypointCard &entry, std::string *mapped)
{
    if (!entry.icon)
        return false;

    const std::string utf8 = utf8FromCodepoint(entry.icon);
    if (utf8.empty())
        return false;

    const std::string glyph = getFont().decodeUTF8(utf8);
    if (glyph.size() != 1 || glyph[0] == '\x1A' || glyph[0] == '\x7F')
        return false;

    if (mapped)
        *mapped = glyph;
    return true;
}

uint8_t InkHUD::WaypointListApplet::fallbackBadgeNumber(const WaypointCard &entry)
{
    uint8_t badge = 0;

    for (auto it = waypoints.rbegin(); it != waypoints.rend(); ++it) {
        const WaypointCard &candidate = *it;
        if (canRenderWaypointIcon(candidate))
            continue;

        if (candidate.id == entry.id)
            return badge;

        if (badge < 9)
            ++badge;
    }

    return 0;
}

bool InkHUD::WaypointListApplet::drawWaypointIcon(const WaypointCard &entry, int16_t left, int16_t centerY, uint16_t boxSize)
{
    std::string mappedGlyph;
    if (!canRenderWaypointIcon(entry, &mappedGlyph))
        return false;

    printAt(left + (boxSize / 2), centerY, mappedGlyph, CENTER, MIDDLE);
    return true;
}

void InkHUD::WaypointListApplet::drawFallbackIcon(const WaypointCard &entry, int16_t left, int16_t rowTop, uint16_t boxWidth,
                                                  uint16_t rowHeight)
{
    char badgeText[3];
    snprintf(badgeText, sizeof(badgeText), "%u", (unsigned)fallbackBadgeNumber(entry));

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
    drawHeader(headerText(landscape));

    if (waypoints.empty()) {
        setFont(fontMedium);
        printAt(X(0.5f), Y(0.5f), "No Waypoints", CENTER, MIDDLE);
        return;
    }

    setFont(fontSmall);

    const int16_t contentTop = getHeaderHeight() + 2;
    const uint8_t start = std::min<uint8_t>(scrollOffset, (uint8_t)waypoints.size() - 1);
    const uint8_t rows = visibleRows(start, landscape);
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
        const WaypointCard &entry = waypoints.at(i);
        const uint8_t rowH = rowHeight(entry, landscape);
        const int16_t line1Y = rowTop + (fontSmall.lineHeight() / 2) + 1;
        const int16_t line2Y = rowTop + fontSmall.lineHeight() + 1;
        const int16_t metaY =
            rowTop + (hasDescription(entry) ? ((fontSmall.lineHeight() * 2) + 2) : (fontSmall.lineHeight() + 1));

        if (!drawWaypointIcon(entry, 1, line1Y, iconW - 1))
            drawFallbackIcon(entry, 0, rowTop, iconW, rowH);

        const std::string name = waypointName(entry);
        const std::string description = waypointDescription(entry);
        const std::string distance = distanceText(entry);
        const std::string coord = coordinateText(entry, landscape);
        const std::string expire = expireText(entry.expire);

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
