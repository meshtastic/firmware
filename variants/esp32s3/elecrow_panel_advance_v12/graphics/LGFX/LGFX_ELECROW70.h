#pragma once

#define LGFX_USE_V1
#include "platform/esp32/ElecrowStc.h"
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

#ifndef FREQ_WRITE
#define FREQ_WRITE 14000000
#endif

namespace elecrow_panel
{
class StcBacklight : public lgfx::ILight
{
  public:
    bool init(uint8_t brightness) override
    {
        setBrightness(brightness);
        return true;
    }

    void setBrightness(uint8_t brightness) override
    {
        const uint8_t command = brightness == 0 ? BACKLIGHT_OFF : 244 - (static_cast<uint16_t>(brightness) * 244 / 255);
        writeI2c(STC_ADDRESS, &command, 1);
    }
};

class Gt911WireTouch : public lgfx::ITouch
{
  public:
    bool init() override
    {
        uint8_t status = 0;
        _inited = readI2cRegister16(GT911_ADDRESS, 0x814E, &status, 1);
        if (_inited)
            clearStatus();
        return _inited;
    }

    void wakeup() override {}
    void sleep() override {}

    uint_fast8_t getTouchRaw(lgfx::touch_point_t *point, uint_fast8_t count) override
    {
        if (!_inited || count == 0)
            return 0;

        uint8_t status = 0;
        if (!readI2cRegister16(GT911_ADDRESS, 0x814E, &status, 1) || !(status & 0x80))
            return 0;

        const uint8_t points = status & 0x0F;
        if (points == 0 || points > 5) {
            clearStatus();
            return 0;
        }

        uint8_t data[8];
        if (!readI2cRegister16(GT911_ADDRESS, 0x814F, data, sizeof(data))) {
            clearStatus();
            return 0;
        }

        point->id = data[0];
        point->x = data[1] | (data[2] << 8);
        point->y = data[3] | (data[4] << 8);
        point->size = data[5] | (data[6] << 8);
        clearStatus();
        return 1;
    }

  private:
    void clearStatus()
    {
        const uint8_t clear[] = {0x81, 0x4E, 0x00};
        writeI2c(GT911_ADDRESS, clear, sizeof(clear));
    }
};
} // namespace elecrow_panel

class LGFX_ELECROW70 : public lgfx::LGFX_Device
{
    lgfx::Bus_RGB _bus_instance;
    lgfx::Panel_RGB _panel_instance;
    elecrow_panel::Gt911WireTouch _touch_instance;
    elecrow_panel::StcBacklight _backlight_instance;

  public:
    const uint16_t screenWidth = 800;
    const uint16_t screenHeight = 480;

    bool hasButton() { return false; }

    LGFX_ELECROW70()
    {
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = screenWidth;
            cfg.memory_height = screenHeight;
            cfg.panel_width = screenWidth;
            cfg.panel_height = screenHeight;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _panel_instance.config_detail();
            cfg.use_psram = 1;
            _panel_instance.config_detail(cfg);
        }

        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;
            cfg.pin_d0 = GPIO_NUM_21;
            cfg.pin_d1 = GPIO_NUM_47;
            cfg.pin_d2 = GPIO_NUM_48;
            cfg.pin_d3 = GPIO_NUM_45;
            cfg.pin_d4 = GPIO_NUM_38;
            cfg.pin_d5 = GPIO_NUM_9;
            cfg.pin_d6 = GPIO_NUM_10;
            cfg.pin_d7 = GPIO_NUM_11;
            cfg.pin_d8 = GPIO_NUM_12;
            cfg.pin_d9 = GPIO_NUM_13;
            cfg.pin_d10 = GPIO_NUM_14;
            cfg.pin_d11 = GPIO_NUM_7;
            cfg.pin_d12 = GPIO_NUM_17;
            cfg.pin_d13 = GPIO_NUM_18;
            cfg.pin_d14 = GPIO_NUM_3;
            cfg.pin_d15 = GPIO_NUM_46;
            cfg.pin_henable = GPIO_NUM_42;
            cfg.pin_vsync = GPIO_NUM_41;
            cfg.pin_hsync = GPIO_NUM_40;
            cfg.pin_pclk = GPIO_NUM_39;
            cfg.freq_write = FREQ_WRITE;
            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 8;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 8;
            cfg.pclk_idle_high = 1;
            _bus_instance.config(cfg);
        }
        _panel_instance.setBus(&_bus_instance);

        {
            auto cfg = _touch_instance.config();
            cfg.x_min = 0;
            cfg.x_max = screenWidth;
            cfg.y_min = 0;
            cfg.y_max = screenHeight;
            cfg.pin_int = -1;
            cfg.pin_rst = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.i2c_port = 0;
            cfg.i2c_addr = elecrow_panel::GT911_ADDRESS;
            cfg.pin_sda = GPIO_NUM_15;
            cfg.pin_scl = GPIO_NUM_16;
            cfg.freq = 400000;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        _panel_instance.setLight(&_backlight_instance);
        setPanel(&_panel_instance);
    }
};
