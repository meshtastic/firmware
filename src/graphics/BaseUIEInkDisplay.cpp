#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./BaseUIEInkDisplay.h"
#include "UptimeClock.h"

#include "configuration.h"
#include "main.h"

using namespace NicheGraphics;

BaseUIEInkDisplay::BaseUIEInkDisplay(Drivers::EInk *driver, uint8_t rotation)
    : concurrency::OSThread("BaseUIEInk"), driver(driver), rotation(rotation & 0x3)
{
    OSThread::disable();

    this->geometry = GEOMETRY_RAWMODE;

    // BaseUI draws in UI orientation. Physical panel dimensions are swapped for 90°/270°.
    const bool swap = (this->rotation == 1) || (this->rotation == 3);
    this->displayWidth = swap ? driver->height : driver->width;
    this->displayHeight = swap ? driver->width : driver->height;

    // OLEDDisplay indexes buffer[x + (y/8) * displayWidth]
    this->displayBufferSize = displayWidth * ((displayHeight + 7) / 8);

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
    if (framePending) {
        framePending = false;
        OSThread::disable();
        if (commit(pendingType, true))
            lastDrawMsec = Time::stampMillis();
    }
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
    const uint32_t now = Time::stampMillis();
    if (lastDrawMsec != 0 && (now - lastDrawMsec) < msecLimit)
        return false;

    Drivers::EInk::UpdateTypes type = decide();
    // A held FULL frame was already charged against the refresh debt; whatever replaces it must still be FULL
    if (framePending && pendingType == Drivers::EInk::UpdateTypes::FULL)
        type = Drivers::EInk::UpdateTypes::FULL;
    // Decide on the type the panel will actually run: commit() turns FAST into FULL where FAST is unsupported
    if (type == Drivers::EInk::UpdateTypes::FAST && !driver->supports(Drivers::EInk::UpdateTypes::FAST))
        type = Drivers::EInk::UpdateTypes::FULL;
    // BLOCKING only holds for a FULL refresh (the deep-sleep screen), as it did in EInkDynamicDisplay
    const bool blocking = (frameFlags & BLOCKING) && type == Drivers::EInk::UpdateTypes::FULL;

    // Mid-refresh, keep only the newest frame rather than waiting: runOnce() draws it when the panel is free
    if (driver->busy() && !blocking) {
        pendingType = type;
        framePending = true;
        OSThread::setIntervalFromNow(FLUSH_POLL_MS);
        OSThread::enabled = true;
        frameFlags = BACKGROUND;
        return false;
    }
    if (driver->busy())
        driver->await();

    // This frame supersedes any queued one, and nothing is queued behind it
    framePending = false;
    OSThread::disable();
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

int32_t BaseUIEInkDisplay::runOnce()
{
    if (!framePending)
        return disable();
    if (driver->busy())
        return FLUSH_POLL_MS;

    framePending = false;
    if (commit(pendingType, false))
        lastDrawMsec = Time::stampMillis();
    return disable();
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
    const uint16_t pw = driver->width;
    const uint16_t ph = driver->height;
    // flipScreenVertically() is a no-op here (sendCommand() discards commands), so honour flip_screen as a further 180°
    const uint8_t rot = (rotation + (config.display.flip_screen ? 2 : 0)) & 0x3;

    // Unrotated, byte-aligned: convert 8x8 blocks, one OLED page of 8 columns into 8 panel row bytes
    if (rot == 0 && displayWidth == pw && displayHeight == ph && pw % 8 == 0 && ph % 8 == 0) {
        for (uint16_t page = 0; page < ph / 8; page++) {
            const uint8_t *col = &buffer[page * displayWidth];
            uint8_t *row = &panelBuffer[page * 8 * panelRowBytes];
            for (uint16_t xb = 0; xb < pw / 8; xb++, col += 8) {
                for (uint8_t bit = 0; bit < 8; bit++) {
                    uint8_t out = 0;
                    for (uint8_t i = 0; i < 8; i++)
                        out |= ((col[i] >> bit) & 1) << (7 - i);
                    row[bit * panelRowBytes + xb] = ~out; // OLED 1 = black, panel 1 = white
                }
            }
        }
        return hashPanelBuffer();
    }

    memset(panelBuffer, 0xFF, panelBufferSize); // start all-white

    // OLEDDisplay buffer: byte = buffer[x + (y/8) * displayWidth]; bit = 1 << (y & 7); 1 = black
    // Niche buffer:       byte = (y * panelRowBytes) + (x/8);      bit = 1 << (7 - x%8); 1 = white
    for (uint16_t oy = 0; oy < displayHeight; oy++) {
        for (uint16_t ox = 0; ox < displayWidth; ox++) {
            const uint8_t b = buffer[ox + (oy / 8) * displayWidth];
            const bool isBlack = b & (1 << (oy & 7));

            uint16_t px, py;
            switch (rot) {
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

    return hashPanelBuffer();
}

uint32_t BaseUIEInkDisplay::hashPanelBuffer() const
{
    // FNV-1a
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < panelBufferSize; i++) {
        h ^= panelBuffer[i];
        h *= 16777619u;
    }
    return h;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
