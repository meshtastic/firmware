#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./AppSwitcherApplet.h"

#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Tile.h"

#include <algorithm>
#include <cctype>

using namespace NicheGraphics;

namespace
{
static constexpr uint16_t BODY_MARGIN_X = 8;
static constexpr uint16_t BODY_MARGIN_Y = 6;
static constexpr uint16_t SLOT_GAP_X = 8;
static constexpr uint16_t SLOT_GAP_Y = 8;
static constexpr uint8_t ICON_RADIUS = 8;
static constexpr uint16_t FOOTER_PAD = 4;
static constexpr uint16_t LABEL_BOTTOM_PAD = 1;
static constexpr uint16_t LABEL_GAP_Y = 1;
static constexpr uint16_t TITLE_H_PAD = 8;

static constexpr uint8_t GRID_COLS = 3;
static constexpr uint8_t GRID_ROWS = 4;
static constexpr uint8_t ICON_NATIVE_SIZE = 48;
static constexpr uint8_t ICON_OUTLINE_STROKE = 1;

enum class IconKind : uint8_t { GENERIC, ALL_MESSAGES, DMS, CHANNEL, POSITIONS, RECENTS, HEARD, FAVORITES };

struct GridLayout {
    uint16_t footerH = 0;
    uint16_t bodyTop = 0;
    uint16_t bodyBottom = 0;
    uint16_t slotW = 0;
    uint16_t slotH = 0;
    uint16_t iconBox = 0;
};

/*
 * Icons sourced from Material Design Icons PNG set (Apache 2.0):
 * https://github.com/material-icons/material-icons-png
 *
 * Families used: outline-2x (48x48)
 * apps, markunread, chat, forum, place, history, hearing, star_border
 */
static constexpr uint64_t icon_generic_apps[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL,
    0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x000000000000ULL, 0x000000000000ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL,
    0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x000000000000ULL, 0x000000000000ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL,
    0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x00FF0FF0FF00ULL, 0x000000000000ULL, 0x000000000000ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

static constexpr uint64_t icon_all_messages[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x07FFFFFFFFE0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL,
    0x0FC0000003F0ULL, 0x0FE0000007F0ULL, 0x0FF800001FF0ULL, 0x0FFE00007FF0ULL, 0x0FFF0000FFF0ULL, 0x0F7FC003FEF0ULL,
    0x0F1FE007F8F0ULL, 0x0F07F81FE0F0ULL, 0x0F03FE7FC0F0ULL, 0x0F00FFFF00F0ULL, 0x0F007FFE00F0ULL, 0x0F001FF800F0ULL,
    0x0F0007E000F0ULL, 0x0F0003C000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL,
    0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL,
    0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL, 0x07FFFFFFFFE0ULL, 0x000000000000ULL, 0x000000000000ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

static constexpr uint64_t icon_dms[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x07FFFFFFFFE0ULL, 0x0FFFFFFFFFF0ULL,
    0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL,
    0x0F0FFFFFF0F0ULL, 0x0F0FFFFFF0F0ULL, 0x0F0FFFFFF0F0ULL, 0x0F0FFFFFF0F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL,
    0x0F0FFFFFF0F0ULL, 0x0F0FFFFFF0F0ULL, 0x0F0FFFFFF0F0ULL, 0x0F0FFFFFF0F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL,
    0x0F0FFFF000F0ULL, 0x0F0FFFF000F0ULL, 0x0F0FFFF000F0ULL, 0x0F0FFFF000F0ULL, 0x0F00000000F0ULL, 0x0F00000000F0ULL,
    0x0F00000000F0ULL, 0x0F00000000F0ULL, 0x0F7FFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFF0ULL, 0x0FFFFFFFFFE0ULL,
    0x0FF000000000ULL, 0x0FE000000000ULL, 0x0FC000000000ULL, 0x0F8000000000ULL, 0x0F0000000000ULL, 0x0E0000000000ULL,
    0x0C0000000000ULL, 0x080000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

static constexpr uint64_t icon_channel[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x0FFFFFFFC000ULL, 0x0FFFFFFFC000ULL,
    0x0FFFFFFFC000ULL, 0x0FFFFFFFC000ULL, 0x0F000003C000ULL, 0x0F000003C000ULL, 0x0F000003C000ULL, 0x0F000003C000ULL,
    0x0F000003C3F0ULL, 0x0F000003C3F0ULL, 0x0F000003C3F0ULL, 0x0F000003C3F0ULL, 0x0F000003C3F0ULL, 0x0F000003C3F0ULL,
    0x0F000003C3F0ULL, 0x0F000003C3F0ULL, 0x0F000003C3F0ULL, 0x0F000003C3F0ULL, 0x0F7FFFFFC3F0ULL, 0x0FFFFFFFC3F0ULL,
    0x0FFFFFFFC3F0ULL, 0x0FFFFFFFC3F0ULL, 0x0FF0000003F0ULL, 0x0FE0000003F0ULL, 0x0FC0000003F0ULL, 0x0F80000003F0ULL,
    0x0F0FFFFFFFF0ULL, 0x0E0FFFFFFFF0ULL, 0x0C0FFFFFFFF0ULL, 0x080FFFFFFFF0ULL, 0x000FFFFFFFF0ULL, 0x000FFFFFFFF0ULL,
    0x000000000FF0ULL, 0x0000000007F0ULL, 0x0000000003F0ULL, 0x0000000001F0ULL, 0x0000000000F0ULL, 0x000000000070ULL,
    0x000000000030ULL, 0x000000000010ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

static constexpr uint64_t icon_positions[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x00001FF80000ULL, 0x00007FFE0000ULL,
    0x0001FFFF8000ULL, 0x0003FFFFC000ULL, 0x0007FFFFE000ULL, 0x000FF00FF000ULL, 0x000FC003F000ULL, 0x001F8001F800ULL,
    0x001F0000F800ULL, 0x003F07E0FC00ULL, 0x003E0FF07C00ULL, 0x003E1FF87C00ULL, 0x003E1FF87C00ULL, 0x003E1FF87C00ULL,
    0x003E1FF87C00ULL, 0x003E1FF87C00ULL, 0x003E1FF87C00ULL, 0x003E0FF07C00ULL, 0x003E07E07C00ULL, 0x001F0000F800ULL,
    0x001F0000F800ULL, 0x001F8001F800ULL, 0x000F8001F000ULL, 0x000FC003F000ULL, 0x0007C003E000ULL, 0x0007E007E000ULL,
    0x0003F00FC000ULL, 0x0003F00FC000ULL, 0x0001F81F8000ULL, 0x0001FC3F8000ULL, 0x0000FC3F0000ULL, 0x00007E7E0000ULL,
    0x00003FFC0000ULL, 0x00003FFC0000ULL, 0x00001FF80000ULL, 0x00000FF00000ULL, 0x00000FF00000ULL, 0x000007E00000ULL,
    0x000003C00000ULL, 0x000001800000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

static constexpr uint64_t icon_recents[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
    0x00000FFF0000ULL, 0x00003FFFC000ULL, 0x0000FFFFF000ULL, 0x0001FFFFF800ULL, 0x0007FFFFFE00ULL, 0x000FF801FF00ULL,
    0x000FE0007F00ULL, 0x001FC0003F80ULL, 0x003F00000FC0ULL, 0x003F00000FC0ULL, 0x007E00E007E0ULL, 0x007C00E003E0ULL,
    0x00FC00E003F0ULL, 0x00F800E001F0ULL, 0x00F800E001F0ULL, 0x00F800E001F0ULL, 0x00F800E001F0ULL, 0x00F800E001F0ULL,
    0x3FFFC0F001F0ULL, 0x1FFF80FC01F0ULL, 0x0FFF00FF01F0ULL, 0x07FE003F81F0ULL, 0x03FC001FC1F0ULL, 0x01F80007C3F0ULL,
    0x00F0000183E0ULL, 0x0060000007E0ULL, 0x000000000FC0ULL, 0x000000000FC0ULL, 0x0001C0003F80ULL, 0x0003E0007F00ULL,
    0x0007F801FF00ULL, 0x0007FFFFFE00ULL, 0x0001FFFFF800ULL, 0x0000FFFFF000ULL, 0x00003FFFC000ULL, 0x00000FFF0000ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

static constexpr uint64_t icon_heard[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000800000000ULL, 0x001C00000000ULL, 0x003E01FF8000ULL, 0x007F07FFE000ULL,
    0x007E1FFFF800ULL, 0x00FC3FFFFC00ULL, 0x00F87FFFFE00ULL, 0x01F8FF00FF00ULL, 0x01F0FC003F00ULL, 0x01F1F8001F80ULL,
    0x03E1F0000F80ULL, 0x03E3F07E0FC0ULL, 0x03E3E0FF07C0ULL, 0x03E3E1FF87C0ULL, 0x03E3E1FF87C0ULL, 0x03E3E1FF87C0ULL,
    0x03E3E1FF8000ULL, 0x03E3E1FF8000ULL, 0x03E3E1FF8000ULL, 0x03E3E0FF0000ULL, 0x03E3F07E0000ULL, 0x03E1F0000000ULL,
    0x01F1F8000000ULL, 0x01F1F8000000ULL, 0x01F8FC000000ULL, 0x00F87E000000ULL, 0x00FC7F800000ULL, 0x007E3FC00000ULL,
    0x007F1FE00000ULL, 0x003E0FF00000ULL, 0x001C03F00000ULL, 0x000801F80000ULL, 0x000000F80000ULL, 0x000000FC0000ULL,
    0x0000007C07C0ULL, 0x0000007E07C0ULL, 0x0000003F0FC0ULL, 0x0000003FFFC0ULL, 0x0000001FFF80ULL, 0x0000000FFF00ULL,
    0x00000007FE00ULL, 0x00000003FC00ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

static constexpr uint64_t icon_favorites[48] = {
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000001800000ULL, 0x000001800000ULL,
    0x000003C00000ULL, 0x000003C00000ULL, 0x000003C00000ULL, 0x000007E00000ULL, 0x000007E00000ULL, 0x00000FF00000ULL,
    0x00000FF00000ULL, 0x00001FF80000ULL, 0x00001FF80000ULL, 0x00001E780000ULL, 0x00003E7C0000ULL, 0x003FFC3FFC00ULL,
    0x0FFFFC3FFFF0ULL, 0x0FFFF81FFFF0ULL, 0x03FFF81FFFC0ULL, 0x01F800001F80ULL, 0x00FC00003F00ULL, 0x007E00007E00ULL,
    0x003F8001FC00ULL, 0x001FC003F800ULL, 0x000FE007F000ULL, 0x0003E007C000ULL, 0x0003E007C000ULL, 0x0003C003C000ULL,
    0x0003C003C000ULL, 0x0003C3C3C000ULL, 0x0007CFF3E000ULL, 0x00079FF9E000ULL, 0x0007FFFFE000ULL, 0x0007FE7FE000ULL,
    0x000FFC3FF000ULL, 0x000FF00FF000ULL, 0x000FC003F000ULL, 0x000F8001F000ULL, 0x001E00007000ULL, 0x001800001800ULL,
    0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL, 0x000000000000ULL,
};

using IconBitmap = const uint64_t *;

IconBitmap iconBitmapForKind(IconKind kind)
{
    switch (kind) {
    case IconKind::ALL_MESSAGES:
        return icon_all_messages;
    case IconKind::DMS:
        return icon_dms;
    case IconKind::CHANNEL:
        return icon_channel;
    case IconKind::POSITIONS:
        return icon_positions;
    case IconKind::RECENTS:
        return icon_recents;
    case IconKind::HEARD:
        return icon_heard;
    case IconKind::FAVORITES:
        return icon_favorites;
    case IconKind::GENERIC:
    default:
        return icon_generic_apps;
    }
}

GridLayout computeLayout(const InkHUD::Applet *applet)
{
    GridLayout layout;

    const uint16_t w = applet->width();
    const uint16_t h = applet->height();

    layout.footerH = InkHUD::Applet::fontSmall.lineHeight() + (FOOTER_PAD * 2);
    layout.bodyTop = BODY_MARGIN_Y;
    layout.bodyBottom = (h > (layout.footerH + BODY_MARGIN_Y)) ? (h - layout.footerH - BODY_MARGIN_Y) : layout.bodyTop;

    const uint16_t bodyW = (w > (BODY_MARGIN_X * 2)) ? (w - (BODY_MARGIN_X * 2)) : 1;
    const uint16_t bodyH = (layout.bodyBottom > layout.bodyTop) ? (layout.bodyBottom - layout.bodyTop) : 1;
    const uint16_t gapsX = SLOT_GAP_X * (GRID_COLS - 1);
    const uint16_t gapsY = SLOT_GAP_Y * (GRID_ROWS - 1);

    layout.slotW = (bodyW > gapsX) ? ((bodyW - gapsX) / GRID_COLS) : 1;
    layout.slotH = (bodyH > gapsY) ? ((bodyH - gapsY) / GRID_ROWS) : 1;

    const uint16_t maxIconW = (layout.slotW > 6) ? (layout.slotW - 6) : layout.slotW;
    const uint16_t maxIconH = (layout.slotH > (InkHUD::Applet::fontSmall.lineHeight() + LABEL_GAP_Y + LABEL_BOTTOM_PAD + 6))
                                  ? (layout.slotH - InkHUD::Applet::fontSmall.lineHeight() - LABEL_GAP_Y - LABEL_BOTTOM_PAD - 6)
                                  : layout.slotH / 2;
    layout.iconBox = std::max<uint16_t>(20, std::min<uint16_t>(maxIconW, maxIconH));
    return layout;
}

std::string lowercase(const char *name)
{
    if (!name)
        return "";
    std::string out(name);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

IconKind iconKindForAppletName(const char *name)
{
    const std::string lower = lowercase(name);
    if (lower.find("all message") != std::string::npos || lower.find("messages") != std::string::npos)
        return IconKind::ALL_MESSAGES;
    if (lower.find("dm") != std::string::npos)
        return IconKind::DMS;
    if (lower.find("channel") != std::string::npos)
        return IconKind::CHANNEL;
    if (lower.find("position") != std::string::npos)
        return IconKind::POSITIONS;
    if (lower.find("recent") != std::string::npos)
        return IconKind::RECENTS;
    if (lower.find("heard") != std::string::npos)
        return IconKind::HEARD;
    if (lower.find("favorite") != std::string::npos)
        return IconKind::FAVORITES;
    return IconKind::GENERIC;
}

void drawIconBitmapScaled(InkHUD::Applet *applet, IconBitmap bmp48, int16_t left, int16_t top, uint16_t boxSize, uint16_t color)
{
    if (!bmp48 || boxSize == 0)
        return;

    auto srcOn = [bmp48](int16_t sx, int16_t sy) -> bool {
        if (sx < 0 || sy < 0 || sx >= ICON_NATIVE_SIZE || sy >= ICON_NATIVE_SIZE)
            return false;
        const uint64_t rowBits = bmp48[sy];
        return (rowBits & (1ULL << (47 - sx))) != 0;
    };

    for (uint16_t y = 0; y < boxSize; y++) {
        const uint8_t srcY = (uint8_t)((y * ICON_NATIVE_SIZE) / boxSize);
        for (uint16_t x = 0; x < boxSize; x++) {
            const uint8_t srcX = (uint8_t)((x * ICON_NATIVE_SIZE) / boxSize);
            if (!srcOn(srcX, srcY))
                continue;

            const uint16_t w = std::min<uint16_t>(ICON_OUTLINE_STROKE, boxSize - x);
            const uint16_t h = std::min<uint16_t>(ICON_OUTLINE_STROKE, boxSize - y);
            applet->fillRect(left + x, top + y, w, h, color);
        }
    }
}
} // namespace

InkHUD::AppSwitcherApplet::AppSwitcherApplet()
{
    alwaysRender = true;
}

void InkHUD::AppSwitcherApplet::rebuildActiveAppletList()
{
    activeAppletIndices.clear();

    const auto &settings = inkhud->persistence->settings;
    const uint8_t tileCount = std::min<uint8_t>(settings.userTiles.count, Persistence::MAX_TILES_GLOBAL);
    const uint8_t focusedTile = (tileCount > 0) ? std::min<uint8_t>(settings.userTiles.focused, tileCount - 1) : 0;

    // Applets displayed on other tiles should not be selectable here.
    std::vector<bool> occupiedOnOtherTiles(inkhud->userApplets.size(), false);
    for (uint8_t tile = 0; tile < tileCount; tile++) {
        if (tile == focusedTile)
            continue;

        const uint8_t appletIndex = settings.userTiles.displayedUserApplet[tile];
        if (appletIndex < occupiedOnOtherTiles.size())
            occupiedOnOtherTiles[appletIndex] = true;
    }

    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        Applet *a = inkhud->userApplets.at(i);
        if (a && a->isActive() && !occupiedOnOtherTiles[i])
            activeAppletIndices.push_back(i);
    }
}

uint8_t InkHUD::AppSwitcherApplet::cardsPerPage() const
{
    return GRID_COLS * GRID_ROWS;
}

uint8_t InkHUD::AppSwitcherApplet::currentPage() const
{
    const uint8_t cpp = cardsPerPage();
    if (cpp == 0)
        return 0;
    return selectedIndex / cpp;
}

void InkHUD::AppSwitcherApplet::stepPage(int8_t delta)
{
    if (activeAppletIndices.empty())
        return;

    const uint8_t cpp = cardsPerPage();
    const uint8_t pageCount = std::max<uint8_t>(1, (activeAppletIndices.size() + cpp - 1) / cpp);
    int16_t nextPage = (int16_t)currentPage() + delta;
    while (nextPage < 0)
        nextPage += pageCount;
    while (nextPage >= pageCount)
        nextPage -= pageCount;

    selectedIndex = std::min<uint8_t>((uint8_t)(nextPage * cpp), activeAppletIndices.size() - 1);
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::AppSwitcherApplet::clampSelection()
{
    if (activeAppletIndices.empty()) {
        selectedIndex = 0;
        return;
    }

    if (selectedIndex >= activeAppletIndices.size())
        selectedIndex = activeAppletIndices.size() - 1;
}

void InkHUD::AppSwitcherApplet::activateSelectedApplet()
{
    if (activeAppletIndices.empty()) {
        sendToBackground();
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return;
    }

    const uint8_t appletIndex = activeAppletIndices.at(selectedIndex);

    sendToBackground();
    inkhud->showApplet(appletIndex);
}

void InkHUD::AppSwitcherApplet::onForeground()
{
    rebuildActiveAppletList();
    clampSelection();
    handleInput = true;
    lockRequests = true;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::AppSwitcherApplet::onBackground()
{
    handleInput = false;
    lockRequests = false;

    if (borrowedTileOwner)
        borrowedTileOwner->bringToForeground();

    Tile *t = getTile();
    if (t)
        t->assignApplet(borrowedTileOwner);
    borrowedTileOwner = nullptr;
}

void InkHUD::AppSwitcherApplet::show(Tile *t)
{
    if (!t)
        return;

    borrowedTileOwner = t->getAssignedApplet();
    if (borrowedTileOwner)
        borrowedTileOwner->sendToBackground();

    t->assignApplet(this);
    bringToForeground();
}

void InkHUD::AppSwitcherApplet::onRender(bool full)
{
    (void)full;

    const GridLayout layout = computeLayout(this);
    const uint8_t cpp = cardsPerPage();
    const uint8_t page = currentPage();
    const uint8_t pageStart = page * cpp;

    setFont(fontMedium);
    setTextColor(BLACK);

    fillRect(0, 0, width(), height(), WHITE);
    drawRect(0, 0, width(), height(), BLACK);

    if (activeAppletIndices.empty()) {
        setFont(fontSmall);
        printAt(width() / 2, height() / 2, "No Available Applets", CENTER, MIDDLE);
        return;
    }

    for (uint8_t i = 0; i < cpp; i++) {
        const uint8_t idx = pageStart + i;
        if (idx >= activeAppletIndices.size())
            break;

        const uint8_t row = i / GRID_COLS;
        const uint8_t col = i % GRID_COLS;
        const int16_t slotL = BODY_MARGIN_X + (col * (layout.slotW + SLOT_GAP_X));
        const int16_t slotT = layout.bodyTop + (row * (layout.slotH + SLOT_GAP_Y));
        const bool selected = (idx == selectedIndex);

        const uint8_t appletIndex = activeAppletIndices.at(idx);
        Applet *a = inkhud->userApplets.at(appletIndex);
        if (!a)
            continue;

        const int16_t iconLeft = slotL + ((layout.slotW - layout.iconBox) / 2);
        const int16_t iconTop = slotT + 1;

        // Requested style: icon in outlined rounded square only (no filled box, no outer app card).
        drawRoundRect(iconLeft, iconTop, layout.iconBox, layout.iconBox, ICON_RADIUS, BLACK);
        if (selected)
            drawRoundRect(iconLeft + 2, iconTop + 2, layout.iconBox - 4, layout.iconBox - 4, ICON_RADIUS, BLACK);

        const IconBitmap bmp = iconBitmapForKind(iconKindForAppletName(a->name));
        drawIconBitmapScaled(this, bmp, iconLeft + 3, iconTop + 3, layout.iconBox - 6, BLACK);

        setFont(fontSmall);
        std::string label = a->name ? a->name : "Applet";
        const uint16_t maxLabelW = layout.slotW > 4 ? (layout.slotW - 4) : layout.slotW;
        if (getTextWidth(label) > maxLabelW) {
            while (!label.empty() && getTextWidth(label + "...") > maxLabelW)
                label.pop_back();
            label = label.empty() ? "..." : label + "...";
        }
        const int16_t labelY = iconTop + layout.iconBox + LABEL_GAP_Y;
        setTextColor(BLACK);
        printAt(slotL + (layout.slotW / 2), labelY, label.c_str(), CENTER, TOP);

        if (a->isForeground())
            fillCircle(iconLeft + layout.iconBox - 4, iconTop + 4, 2, BLACK);
    }

    const uint8_t pageCount = std::max<uint8_t>(1, (activeAppletIndices.size() + cpp - 1) / cpp);
    if (pageCount > 1) {
        setFont(fontSmall);
        setTextColor(BLACK);
        const int16_t footerY = height() - layout.footerH + FOOTER_PAD;
        printAt(TITLE_H_PAD, footerY, "<", LEFT, TOP);
        printAt(width() - TITLE_H_PAD, footerY, ">", RIGHT, TOP);
        const std::string pageText = std::to_string(page + 1) + "/" + std::to_string(pageCount);
        printAt(width() / 2, footerY, pageText.c_str(), CENTER, TOP);
    }
}

bool InkHUD::AppSwitcherApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    (void)longPress;

    Tile *t = getTile();
    if (!t || activeAppletIndices.empty())
        return true;

    const uint16_t tileL = t->getLeft();
    const uint16_t tileT = t->getTop();
    const uint16_t tileR = tileL + t->getWidth();
    const uint16_t tileB = tileT + t->getHeight();
    if (x < tileL || x >= tileR || y < tileT || y >= tileB)
        return false;

    const GridLayout layout = computeLayout(this);
    const uint8_t cpp = cardsPerPage();
    const uint8_t page = currentPage();
    const uint8_t pageStart = page * cpp;
    const int16_t localX = (int16_t)x - (int16_t)tileL;
    const int16_t localY = (int16_t)y - (int16_t)tileT;

    for (uint8_t i = 0; i < cpp; i++) {
        const uint8_t idx = pageStart + i;
        if (idx >= activeAppletIndices.size())
            break;

        const uint8_t row = i / GRID_COLS;
        const uint8_t col = i % GRID_COLS;
        const int16_t slotL = BODY_MARGIN_X + (col * (layout.slotW + SLOT_GAP_X));
        const int16_t slotT = layout.bodyTop + (row * (layout.slotH + SLOT_GAP_Y));

        if (localX < slotL || localX >= (slotL + (int16_t)layout.slotW))
            continue;
        if (localY < slotT || localY >= (slotT + (int16_t)layout.slotH))
            continue;

        selectedIndex = idx;
        clampSelection();
        activateSelectedApplet();
        return true;
    }

    const uint8_t pageCount = std::max<uint8_t>(1, (activeAppletIndices.size() + cpp - 1) / cpp);
    if (pageCount <= 1)
        return true;

    const int16_t footerTop = height() - layout.footerH;
    if (localY >= footerTop) {
        if (localX < (int16_t)(width() / 3))
            stepPage(-1);
        else if (localX >= (int16_t)((width() * 2) / 3))
            stepPage(1);
    }

    return true;
}

void InkHUD::AppSwitcherApplet::onButtonShortPress()
{
    if (activeAppletIndices.empty())
        return;

    selectedIndex = (selectedIndex + 1) % activeAppletIndices.size();
    clampSelection();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::AppSwitcherApplet::onButtonLongPress()
{
    activateSelectedApplet();
}

void InkHUD::AppSwitcherApplet::onExitShort()
{
    sendToBackground();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::AppSwitcherApplet::onNavUp()
{
    if (activeAppletIndices.empty())
        return;

    if (selectedIndex == 0)
        selectedIndex = activeAppletIndices.size() - 1;
    else
        selectedIndex--;

    clampSelection();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::AppSwitcherApplet::onNavDown()
{
    if (activeAppletIndices.empty())
        return;

    selectedIndex = (selectedIndex + 1) % activeAppletIndices.size();
    clampSelection();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

#endif
