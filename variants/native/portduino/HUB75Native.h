#pragma once

#include "configuration.h"

#if defined(HAS_HUB75_NATIVE)

#include <OLEDDisplay.h>

#ifndef HUB75_BRIGHTNESS_DEFAULT
#define HUB75_BRIGHTNESS_DEFAULT 100 // rpi-rgb-led-matrix brightness is a 1..100 percentage
#endif

namespace rgb_matrix
{
class RGBMatrix;
class FrameCanvas;
} // namespace rgb_matrix

/**
 * Native (Raspberry Pi / Portduino) HUB75 RGB-matrix display backend.
 *
 * Drives the panel with hzeller/rpi-rgb-led-matrix. Like the ESP32 HUB75Display it plugs into the
 * BaseUI color path by subclassing OLEDDisplay and expanding the mono framebuffer into RGB via the
 * shared TFTColorRegions/TFTPalette helpers. It is a separate class (not shared with the ESP32
 * driver) so no rpi-rgb-led-matrix code lives in src/graphics/. All wiring/tuning comes from
 * config.yaml (portduino_config.hub75_*); the library owns the GPIO pins.
 *
 * Bodies live in src/platform/portduino/HUB75Native.cpp (compiled native-only).
 */
class HUB75Native : public OLEDDisplay
{
  public:
    HUB75Native(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus);
    ~HUB75Native();

    // Redraw the whole panel from the BaseUI buffer (region-aware color expansion).
    void display() override;

    void setDisplayBrightness(uint8_t brightness);

  protected:
    int getBufferOffset(void) override { return 0; }

    void sendCommand(uint8_t com) override;

    bool connect() override;

  private:
    rgb_matrix::RGBMatrix *matrix = nullptr;
    // Offscreen buffer for tear-free updates: display() draws here, then SwapOnVSync() atomically
    // presents it. Without this, writing into the live canvas while the refresh thread scans it
    // tears (worse when display() is contended during packet TX/RX).
    rgb_matrix::FrameCanvas *offscreen = nullptr;
    uint8_t brightness = HUB75_BRIGHTNESS_DEFAULT; // 1..100
};

#endif // HAS_HUB75_NATIVE
