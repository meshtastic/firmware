#pragma once

#include "util/ILog.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "LGFX_T_DISPLAY_P4 requires CONFIG_IDF_TARGET_ESP32P4"
#endif
#if !CONFIG_SPIRAM
#error "LGFX_T_DISPLAY_P4 requires CONFIG_SPIRAM: the DPI frame buffers live in PSRAM"
#endif
#if CONFIG_SPIRAM_SPEED < 200
#warning "LGFX_T_DISPLAY_P4: PSRAM slower than 200 MHz, the frame buffers live in PSRAM"
#endif

#include "graphics/LGFX/experimental/esp32p4/Panel_DSI.hpp"
#include <esp_cache.h>

#include "Panel_RM69A10.h"
#include "TDisplayP4Board.h"
#include "Touch_GT9895.h"

// UI orientation: 0 = portrait 568x1232, 1 = landscape 1232x568, 3 = landscape turned by 180 degrees
#ifndef T_DISPLAY_P4_ROTATION
#if defined(USE_FULL_DOUBLE_BUFFER)
#define T_DISPLAY_P4_ROTATION 0
#else
#define T_DISPLAY_P4_ROTATION 1
#endif
#endif
// Integer zoom of the UI: 2 draws every LVGL pixel as a 2x2 block (the panel has about 330 ppi)
#ifndef T_DISPLAY_P4_UI_SCALE
#define T_DISPLAY_P4_UI_SCALE 1
#endif

#if T_DISPLAY_P4_ROTATION != 0 && T_DISPLAY_P4_ROTATION != 1 && T_DISPLAY_P4_ROTATION != 3
#error "T_DISPLAY_P4_ROTATION must be 0, 1 or 3"
#endif
#if T_DISPLAY_P4_UI_SCALE != 1 && T_DISPLAY_P4_UI_SCALE != 2
#error "T_DISPLAY_P4_UI_SCALE must be 1 or 2"
#endif
#if defined(USE_FULL_DOUBLE_BUFFER) && (T_DISPLAY_P4_ROTATION != 0 || T_DISPLAY_P4_UI_SCALE != 1)
#error "USE_FULL_DOUBLE_BUFFER renders straight into the panel frame buffers: no rotation, no zoom"
#endif

/**
 * LilyGO T-Display-P4 V1.0, AMOLED assembly: RM69A10 568x1232 on 2-lane MIPI-DSI, GT9895 touch.
 * Parsed only inside device-ui's DisplayDriverFactory.cpp (through the LGFX_GENERIC.h shim), so
 * everything is header-only. Panel and touch resets go through the board helpers of variant.cpp.
 */
class LGFX_T_DISPLAY_P4 : public lgfx::LGFX_Device
{
    lgfx::experimental::Bus_DSI _bus_instance;
    Panel_RM69A10 _panel_instance;
    Light_RM69A10 _light_instance;
    Touch_GT9895 _touch_instance;

    static constexpr int32_t PANEL_W = 568;
    static constexpr int32_t PANEL_H = 1232;
    static constexpr int32_t UI_ROT = T_DISPLAY_P4_ROTATION;
    static constexpr int32_t UI_SCALE = T_DISPLAY_P4_UI_SCALE;

  public:
    // Resolution LVGL renders at; the panel itself always stays 568x1232
    const uint32_t screenWidth = (UI_ROT ? PANEL_H : PANEL_W) / UI_SCALE;
    const uint32_t screenHeight = (UI_ROT ? PANEL_W : PANEL_H) / UI_SCALE;

#if defined(USE_FULL_DOUBLE_BUFFER)
    void *getFrameBuffer(uint8_t index) const { return _panel_instance.getFrameBuffer(index); }
    bool presentFrameBuffer(const void *frame_buffer, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
    {
        return _panel_instance.presentFrameBuffer(frame_buffer, x, y, width, height);
    }
    void waitFrameBuffer(void) { _panel_instance.waitFrameBuffer(); }
#else
    // Flush of one LVGL area (RGB565, UI coordinates) into the single DPI frame buffer, rotated and zoomed.
    // Hides LGFX_Device::pushImage: device-ui calls it on the concrete class.
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data)
    {
        auto fb = static_cast<uint16_t *>(_panel_instance.getFrameBuffer(0));
        if (fb == nullptr || data == nullptr || w <= 0 || h <= 0 || x < 0 || y < 0 ||
            x + w > static_cast<int32_t>(screenWidth) || y + h > static_cast<int32_t>(screenHeight))
            return;

        const int32_t s = UI_SCALE;
        int32_t rowFirst, rowCount; // panel rows touched, for the cache write-back
        if (UI_ROT == 1) {
            // panel x = PANEL_W - 1 - ui y, panel y = ui x
            rowFirst = x * s;
            rowCount = w * s;
            for (int32_t dx = 0; dx < w; ++dx) {
                for (int32_t rep = 0; rep < s; ++rep) {
                    uint16_t *dst = fb + ((x + dx) * s + rep) * PANEL_W + (PANEL_W - (y + h) * s);
                    const uint16_t *src = data + (h - 1) * w + dx;
                    for (int32_t dy = h; dy > 0; --dy, src -= w) {
                        *dst++ = *src;
                        if (s == 2)
                            *dst++ = *src;
                    }
                }
            }
        } else if (UI_ROT == 3) {
            // panel x = ui y, panel y = PANEL_H - 1 - ui x
            rowFirst = PANEL_H - (x + w) * s;
            rowCount = w * s;
            for (int32_t dx = 0; dx < w; ++dx) {
                for (int32_t rep = 0; rep < s; ++rep) {
                    uint16_t *dst = fb + (PANEL_H - (x + dx + 1) * s + rep) * PANEL_W + y * s;
                    const uint16_t *src = data + dx;
                    for (int32_t dy = h; dy > 0; --dy, src += w) {
                        *dst++ = *src;
                        if (s == 2)
                            *dst++ = *src;
                    }
                }
            }
        } else {
            rowFirst = y * s;
            rowCount = h * s;
            for (int32_t dy = 0; dy < h; ++dy) {
                for (int32_t rep = 0; rep < s; ++rep) {
                    uint16_t *dst = fb + ((y + dy) * s + rep) * PANEL_W + x * s;
                    const uint16_t *src = data + dy * w;
                    if (s == 1) {
                        memcpy(dst, src, w * sizeof(uint16_t));
                    } else {
                        for (int32_t dx = w; dx > 0; --dx, ++src) {
                            *dst++ = *src;
                            *dst++ = *src;
                        }
                    }
                }
            }
        }

        // The DSI host reads the frame buffer from PSRAM by DMA: push the CPU cache lines out
        const uintptr_t lineBytes = static_cast<uintptr_t>(PANEL_W) * sizeof(uint16_t);
        const uintptr_t base = reinterpret_cast<uintptr_t>(fb);
        const uintptr_t limit = base + lineBytes * PANEL_H;
        uintptr_t start = (base + lineBytes * rowFirst) & ~static_cast<uintptr_t>(127);
        uintptr_t end = (base + lineBytes * (rowFirst + rowCount) + 127) & ~static_cast<uintptr_t>(127);
        if (start < base)
            start = base;
        if (end > limit)
            end = limit;
        if (start < end)
            esp_cache_msync(reinterpret_cast<void *>(start), end - start,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }

    // Touch points come back in panel pixels: turn them into UI coordinates. Hides the LGFX_Device overloads.
    uint_fast8_t getTouch(lgfx::touch_point_t *tp, uint_fast8_t count = 1)
    {
        const uint_fast8_t n = lgfx::LGFX_Device::getTouch(tp, count);
        for (uint_fast8_t i = 0; i < n; ++i) {
            const int32_t px = constrain(static_cast<int32_t>(tp[i].x), 0, PANEL_W - 1);
            const int32_t py = constrain(static_cast<int32_t>(tp[i].y), 0, PANEL_H - 1);
            if (UI_ROT == 1) {
                tp[i].x = py / UI_SCALE;
                tp[i].y = (PANEL_W - 1 - px) / UI_SCALE;
            } else if (UI_ROT == 3) {
                tp[i].x = (PANEL_H - 1 - py) / UI_SCALE;
                tp[i].y = px / UI_SCALE;
            } else {
                tp[i].x = px / UI_SCALE;
                tp[i].y = py / UI_SCALE;
            }
        }
        return n;
    }

    template <typename T> uint_fast8_t getTouch(T *x, T *y, uint_fast8_t index = 0)
    {
        if (index >= 8)
            return 0;
        lgfx::touch_point_t tp[8];
        const uint_fast8_t count = getTouch(tp, index + 1);
        if (index >= count)
            return 0;
        if (x)
            *x = tp[index].x;
        if (y)
            *y = tp[index].y;
        return count;
    }
#endif
    bool hasButton(void) { return false; }

    // Capacitive panel with a fixed raw range: no interactive calibration. Hides LGFX_Device::calibrateTouch, which
    // blocks under the SPI lock and draws its markers into frame buffer 0 only. The parameters stay zero, so
    // device-ui stores no calibration and the mapping built by setTouch() stays in force.
    template <typename T> void calibrateTouch(uint16_t *, const T &, const T &, uint8_t = 10) {}

    // device-ui ignores the result and aborts on a missing frame buffer: name the failed step first
    bool init_impl(bool use_reset, bool use_clear) override
    {
        if (_panel_instance.ready())
            return true; // a second reset pulse would blank a running panel

        if (!tDisplayP4ExpanderReady())
            ILOG_ERROR("T-Display-P4: IO expander not ready, panel and touch reset lines are not driven");

        // LilyGO order: PHY LDO on (variant), panel reset, then the DSI bus
        tDisplayP4PanelReset();
        if (!_bus_instance.init()) {
            ILOG_CRIT("T-Display-P4: MIPI-DSI bus init failed (LDO3 2500 mV or DSI host)");
            return false;
        }
        if (!lgfx::LGFX_Device::init_impl(use_reset, use_clear)) {
            ILOG_CRIT("T-Display-P4: RM69A10 panel init failed (DPI panel or PSRAM frame buffer)");
            return false;
        }

        // Without touch there is no wake source: unregister it so that device-ui never blanks the screen
        if (!_touch_instance.ready()) {
            _panel_instance.setTouch(nullptr);
            ILOG_ERROR("T-Display-P4: GT9895 touch not available, touch input disabled (TFT assembly?)");
        }

        // The init table leaves the AMOLED at brightness 0
        if (getBrightness() == 0)
            setBrightness(T_DISPLAY_P4_INIT_BRIGHTNESS);
        return true;
    }

    LGFX_T_DISPLAY_P4(void)
    {
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = PANEL_W;
            cfg.memory_height = PANEL_H;
            cfg.panel_width = PANEL_W;
            cfg.panel_height = PANEL_H;
            cfg.readable = true;
            cfg.rgb_order = true; // TODO(hw-verify): colour order (false sends MADCTL 0x08)
            cfg.invert = false;
            cfg.bus_shared = false;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.pin_cs = -1;
            cfg.pin_rst = -1; // on the IO expander
            _panel_instance.config(cfg);
        }

        {
            // LilyGO t_display_p4_config.h, RM69A10 block
            auto cfg = _panel_instance.config_detail();
#if defined(USE_FULL_DOUBLE_BUFFER)
            cfg.num_fbs = 2;
#else
            cfg.num_fbs = 1;
#endif
            cfg.dpi_freq_mhz = 60;
            cfg.hsync_pulse_width = 50;
            cfg.hsync_back_porch = 150;
            cfg.hsync_front_porch = 50;
            cfg.vsync_pulse_width = 40;
            cfg.vsync_back_porch = 120;
            cfg.vsync_front_porch = 80;
            _panel_instance.config_detail(cfg);
        }

        {
            auto cfg = _bus_instance.config();
            cfg.bus_id = 0;
            cfg.lane_num = 2;
            cfg.lane_mbps = 1000;
            cfg.ldo_chan_id = 3; // second reference: the variant already holds LDO3 at the same voltage
            cfg.ldo_voltage_mv = 2500;
            _bus_instance.config(cfg);
        }
        _panel_instance.setBus(&_bus_instance);

        {
            auto cfg = _touch_instance.config();
            cfg.i2c_addr = 0x5D;
            cfg.x_min = 0;
            cfg.x_max = 1059;
            cfg.y_min = 0;
            cfg.y_max = 2399;
            cfg.pin_int = -1; // on the IO expander, polled
            cfg.pin_rst = -1; // on the IO expander
            cfg.pin_cs = -1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0; // TODO(hw-verify): touch orientation against the panel
            _touch_instance.config(cfg);
            // After the panel config: setTouch() builds the raw -> pixel mapping from both
            _panel_instance.setTouch(&_touch_instance);
        }

        _light_instance.setBus(&_bus_instance);
        _panel_instance.setLight(&_light_instance);

        setPanel(&_panel_instance);
    }

  private:
    static constexpr uint8_t T_DISPLAY_P4_INIT_BRIGHTNESS = 128;
};
