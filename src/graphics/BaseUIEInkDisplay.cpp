#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./BaseUIEInkDisplay.h"

#include "configuration.h"
#include "main.h"

using namespace NicheGraphics;

BaseUIEInkDisplay::BaseUIEInkDisplay(Drivers::EInk *driver, uint8_t rotation) : driver(driver), rotation(rotation & 0x3)
{
    this->geometry = GEOMETRY_RAWMODE;

    // BaseUI draws in UI orientation. Physical panel dimensions are swapped for 90°/270°.
    const bool swap = (this->rotation == 1) || (this->rotation == 3);
    this->displayWidth = swap ? driver->height : driver->width;
    this->displayHeight = swap ? driver->width : driver->height;

    uint16_t shortSide = min(displayWidth, displayHeight);
    uint16_t longSide = max(displayWidth, displayHeight);
    if (shortSide % 8 != 0)
        shortSide = (shortSide | 7) + 1;
    this->displayBufferSize = longSide * (shortSide / 8);

    // Panel-native row-major buffer
    panelRowBytes = ((driver->width - 1) / 8) + 1;
    panelBufferSize = panelRowBytes * driver->height;
    panelBuffer = new uint8_t[panelBufferSize];
    memset(panelBuffer, 0xFF, panelBufferSize); // All white
}

BaseUIEInkDisplay::~BaseUIEInkDisplay()
{
    delete[] panelBuffer;
}

bool BaseUIEInkDisplay::connect()
{
    LOG_INFO("Init BaseUI E-Ink (%u x %u, rot %u)", driver->width, driver->height, rotation);
    return true;
}

void BaseUIEInkDisplay::addFrameFlag(frameFlagTypes flag)
{
    frameFlags = (frameFlagTypes)(frameFlags | flag);
}

void BaseUIEInkDisplay::setDisplayResilience(uint8_t fastPerFull, float stressMultiplier)
{
    this->fastPerFull = (fastPerFull == 0) ? 1 : fastPerFull;
    this->stressMultiplier = stressMultiplier;
}

void BaseUIEInkDisplay::joinAsyncRefresh()
{
    if (driver->busy())
        driver->await();
}

// OLEDDisplayUi tick path. Honours rate-limit unless flags demand otherwise.
void BaseUIEInkDisplay::display()
{
    const bool demandFast = frameFlags & DEMAND_FAST;
    const bool cosmetic = frameFlags & COSMETIC;
    const bool unlimitedFast = frameFlags & UNLIMITED_FAST;

    if (!demandFast && !cosmetic && !unlimitedFast) {
        if (!forceDisplay(lastDrawMsec == 0 ? 0 : 1000))
            return;
        return;
    }

    forceDisplay(0);
}

// Keyframe path. Returns true if a frame was pushed (sets lastDrawMsec).
bool BaseUIEInkDisplay::forceDisplay(uint32_t msecLimit)
{
    const uint32_t now = millis();
    if (lastDrawMsec != 0 && (now - lastDrawMsec) < msecLimit)
        return false;

    const bool blocking = frameFlags & BLOCKING;
    Drivers::EInk::UpdateTypes type = decide();

    // Don't pile frames on top of a running update - wait it out.
    if (driver->busy())
        driver->await();

    const bool pushed = commit(type, blocking);
    if (pushed)
        lastDrawMsec = now;

    // Reset flags for next frame
    frameFlags = BACKGROUND;
    return pushed;
}

bool BaseUIEInkDisplay::commit(Drivers::EInk::UpdateTypes type, bool blocking)
{
    uint32_t hash = repack();

    // Skip if frame unchanged. Exception: caller explicitly wants a refresh (COSMETIC or FULL).
    if (hash == lastHash && type != Drivers::EInk::UpdateTypes::FULL && lastDrawMsec != 0)
        return false;
    lastHash = hash;

    // Fall back to FULL on panels that don't advertise FAST support.
    if (type == Drivers::EInk::UpdateTypes::FAST && !driver->supports(Drivers::EInk::UpdateTypes::FAST))
        type = Drivers::EInk::UpdateTypes::FULL;

    driver->update(panelBuffer, type);

    if (blocking)
        driver->await();
    return true;
}

Drivers::EInk::UpdateTypes BaseUIEInkDisplay::decide()
{
    typedef Drivers::EInk::UpdateTypes UT;

    const bool unlimitedFast = frameFlags & UNLIMITED_FAST;

    // Explicit flag wins outright
    if (frameFlags & COSMETIC) {
        fullRefreshDebt = max(fullRefreshDebt - 1.0f, 0.0f);
        return UT::FULL;
    }
    if (frameFlags & DEMAND_FAST) {
        if (!unlimitedFast) {
            fullRefreshDebt += (fullRefreshDebt < 1.0f) ? (1.0f / fastPerFull) : (stressMultiplier * (1.0f / fastPerFull));
        }
        return UT::FAST;
    }

    const bool explicitFast = frameFlags & RESPONSIVE;

    if (explicitFast || unlimitedFast) {
        if (!unlimitedFast) {
            fullRefreshDebt += (fullRefreshDebt < 1.0f) ? (1.0f / fastPerFull) : (stressMultiplier * (1.0f / fastPerFull));
        }
        return UT::FAST;
    }

    // BACKGROUND / unspecified: let debt decide
    if (fullRefreshDebt >= 1.0f) {
        fullRefreshDebt = max(fullRefreshDebt - 1.0f, 0.0f);
        return UT::FULL;
    }
    fullRefreshDebt += 1.0f / fastPerFull;
    return UT::FAST;
}

uint32_t BaseUIEInkDisplay::repack()
{
    memset(panelBuffer, 0xFF, panelBufferSize); // start all-white

    const uint16_t pw = driver->width;
    const uint16_t ph = driver->height;

    // OLEDDisplay buffer: byte = buffer[x + (y/8) * displayWidth]; bit = 1 << (y & 7); 1 = black
    // Niche buffer:       byte = (y * panelRowBytes) + (x/8);      bit = 1 << (7 - x%8); 1 = white
    for (uint16_t oy = 0; oy < displayHeight; oy++) {
        for (uint16_t ox = 0; ox < displayWidth; ox++) {
            const uint8_t b = buffer[ox + (oy / 8) * displayWidth];
            const bool isBlack = b & (1 << (oy & 7));

            uint16_t px, py;
            switch (rotation) {
            case 1: // 90° CW: OLED (ox,oy) → panel (pw-1-oy, ox)
                px = pw - 1 - oy;
                py = ox;
                break;
            case 2: // 180°
                px = pw - 1 - ox;
                py = ph - 1 - oy;
                break;
            case 3: // 270° CW
                px = oy;
                py = ph - 1 - ox;
                break;
            case 0:
            default:
                px = ox;
                py = oy;
                break;
            }

            if (px >= pw || py >= ph)
                continue;

            const uint32_t byteNum = (py * panelRowBytes) + (px / 8);
            const uint8_t bitNum = 7 - (px % 8);
            if (isBlack)
                panelBuffer[byteNum] &= ~(1 << bitNum);
            else
                panelBuffer[byteNum] |= (1 << bitNum);
        }
    }

    // FNV-1a
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < panelBufferSize; i++) {
        h ^= panelBuffer[i];
        h *= 16777619u;
    }
    return h;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
