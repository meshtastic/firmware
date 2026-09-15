#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Optional design-QA tooling, compiled in only with -D T5_INKHUD_SCREENSHOT.

Hold BOOT and press the side key: saves the T5 Home / Nodes / Node Detail screen on display to
/t5shots/NNN-<carry|console>-<screen>.pbm (binary PBM, logical orientation, 1 = black), without system overlays.

- Driver keeps the pointer to Renderer's private framebuffer, which Renderer hands to every update()
- Between renders that buffer already holds the overlays (notification, TouchStatus, calibration crosshair), so the
  trigger forces one blocking FAST re-render, and Capture, the first system applet, writes the file mid-pass:
  after the user applets draw, before any overlay does. FAST leaves unchanged rows alone, so nothing visibly refreshes

*/

#pragma once

#include "configuration.h"

#include "SPILock.h"
#include "UptimeClock.h"
#include "graphics/niche/Drivers/EInk/ED047TC1.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/InkHUD/Tile.h"
#include "mesh/Throttle.h"

#include <SD.h>
#include <algorithm>

namespace T5Screenshot
{
using namespace NicheGraphics;

class Driver : public Drivers::ED047TC1
{
  public:
    static inline uint8_t *frame = nullptr; // Renderer::imageBuffer: 928x508 rotated, 1 bpp, MSB first, 1 = white

    void update(uint8_t *imageData, UpdateTypes type) override
    {
        frame = imageData;
        ED047TC1::update(imageData, type);
    }
};

class Capture : public InkHUD::SystemApplet
{
  public:
    static inline Capture *instance = nullptr;
    const char *screen = nullptr; // Filename part; set only for the render pass that takes the screenshot

    void onRender(bool full) override
    {
        (void)full;
        if (screen)
            save();
        screen = nullptr;
    }

  private:
    // Logical pixel, through the same transform as Renderer::rotatePixelCoords
    bool isBlack(uint16_t x, uint16_t y)
    {
        const uint16_t physW = settings->rotation % 2 ? inkhud->height() : inkhud->width();
        const uint16_t physH = settings->rotation % 2 ? inkhud->width() : inkhud->height();
        uint16_t px = x, py = y;
        switch (settings->rotation) {
        case 1:
            px = physW - 1 - y;
            py = x;
            break;
        case 2:
            px = physW - 1 - x;
            py = physH - 1 - y;
            break;
        case 3:
            px = y;
            py = physH - 1 - x;
            break;
        }
        return !((Driver::frame[py * ((physW + 7) / 8) + px / 8] >> (7 - px % 8)) & 1);
    }

    void save()
    {
        const uint16_t w = inkhud->width();
        const uint16_t h = inkhud->height();
        const size_t rowBytes = (w + 7) / 8;
        char header[16];
        const size_t headerLen = snprintf(header, sizeof(header), "P4\n%u %u\n", w, h);

        concurrency::LockGuard guard(spiLock); // SD shares the LoRa SPI bus
        if (SD.cardType() == CARD_NONE) {
            LOG_WARN("T5 screenshot: SD unavailable");
            return;
        }

        // Next number after the highest on the card, so earlier captures are never overwritten
        SD.mkdir("/t5shots");
        unsigned seq = 0;
        File dir = SD.open("/t5shots");
        for (File f = dir.openNextFile(); f; f = dir.openNextFile())
            seq = std::max(seq, (unsigned)atoi(f.name()));
        dir.close();

        char path[48];
        snprintf(path, sizeof(path), "/t5shots/%03u-%s-%s.pbm", seq + 1, w > h ? "console" : "carry", screen);
        LOG_INFO("T5 screenshot: writing %s", path);

        uint8_t row[(928 + 7) / 8]; // Safe-area width bounds either orientation
        File file = SD.open(path, FILE_WRITE);
        bool ok = file && file.write((const uint8_t *)header, headerLen) == headerLen;
        for (uint16_t y = 0; ok && y < h; y++) {
            memset(row, 0, rowBytes); // Row padding bits stay 0
            for (uint16_t x = 0; x < w; x++) {
                if (isBlack(x, y))
                    row[x / 8] |= 0x80 >> (x % 8);
            }
            ok = file.write(row, rowBytes) == rowBytes;
        }
        file.close();
        // close() can't report a failed flush: confirm what actually reached the card
        ok = ok && SD.open(path).size() == headerLen + rowBytes * h;

        if (!ok) {
            SD.remove(path);
            LOG_ERROR("T5 screenshot: write failed, removed %s", path);
            return;
        }
        LOG_INFO("T5 screenshot: saved %ux%u", w, h);
    }
};

inline uint32_t chordAtMs = 0; // 0: no chord waiting to swallow a BOOT action

// Side-key thread: side key pressed while BOOT is held
inline void chord()
{
    LOG_INFO("T5 screenshot: trigger");
    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    InkHUD::Applet *a = inkhud->getActiveApplet(); // Menu / App Switcher borrow the tile, so they are "active" here
    const char *name =
        a && a->isForeground() && a->getTile()->getWidth() == inkhud->width() && a->getTile()->getHeight() == inkhud->height()
            ? a->name
            : "";
    Capture *c = Capture::instance;
    c->screen = !strcmp(name, "Home")          ? "home"
                : !strcmp(name, "Nodes")       ? "nodes"
                : !strcmp(name, "Node Detail") ? "node-detail"
                                               : nullptr;
    const bool supported = c->screen;
    if (supported)
        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true, false); // Blocking: Capture::onRender saves
    if (!supported || c->screen) { // Still set after the pass: a system applet locked rendering (logo, pairing...)
        c->screen = nullptr;
        LOG_WARN("T5 screenshot: unsupported screen");
    }
    // Stamped after the capture: the render and SD write can block longer than swallowBoot()'s window
    chordAtMs = Time::skipZero(Time::getMillis());
}

// BOOT short / long press handlers: true for the one BOOT action belonging to the chord's press.
// The 1 s window drops the stamp when that press fired neither (<50 ms tap); a BOOT press
// released and re-pressed within it after such a tap is swallowed once
inline bool swallowBoot()
{
    const bool swallow = chordAtMs && Throttle::isWithinTimespanMs(chordAtMs, 1000);
    chordAtMs = 0;
    return swallow;
}

inline void begin()
{
    InkHUD::InkHUD *hud = InkHUD::InkHUD::getInstance();
    Capture *c = Capture::instance = new Capture;
    c->name = "T5Screenshot";
    (new InkHUD::Tile)->assignApplet(c); // Zero-size tile: Renderer never clears any pixels for it
    c->activate();
    c->bringToForeground();
    hud->systemApplets.insert(hud->systemApplets.begin(), c); // First: renders before every overlay
}

} // namespace T5Screenshot

#endif
