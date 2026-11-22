#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <Arduino.h>

#include "graphics/LGFX/experimental/esp32p4/Panel_DSI.hpp"

/**
 * RM69A10 AMOLED controller (568x1232) in MIPI-DSI video mode, LilyGO T-Display-P4 V1.0.
 * Init table and DCS commands follow LilyGO cpp_bus_driver chip/mipi/rm69a10.{h,cpp}.
 * RESX is on the IO expander: the board class pulses it before the DSI bus is created.
 * The chip id (DCS 0xA1) is not read: a DSI host read waits for the answer without a timeout.
 */
struct Panel_RM69A10 : public lgfx::experimental::Panel_DSI {
    bool init(bool use_reset) override
    {
        _ready = false;
        _invertSent = false; // state after the hardware reset
        _asleep = false;
        _ready = lgfx::experimental::Panel_DSI::init(use_reset);
        return _ready;
    }

    bool ready(void) const { return _ready; }

    // Sends INVON/INVOFF only on a change, so the vendor default is left alone
    void setInvert(bool invert) override
    {
        _invert = invert;
        const bool want = invert != _cfg.invert;
        if (_ready && want != _invertSent && write_params(want ? CMD_INVON : CMD_INVOFF))
            _invertSent = want;
    }

    // LilyGO order: DISPOFF, SLPIN / SLPOUT, 120 ms, DISPON. The DPI stream keeps running.
    // The sleep path must not block: device-ui calls it while holding the radio SPI lock.
    void setSleep(bool sleep) override
    {
        if (!_ready)
            return;
        if (sleep) {
            write_params(CMD_DISPOFF);
            write_params(CMD_SLPIN);
            _sleepInMs = millis();
            _asleep = true;
        } else {
            if (_asleep) {
                const uint32_t elapsed = millis() - _sleepInMs;
                if (elapsed < RM69A10_SLEEP_DELAY_MS)
                    delay(RM69A10_SLEEP_DELAY_MS - elapsed);
            }
            write_params(CMD_SLPOUT);
            delay(RM69A10_SLEEP_DELAY_MS);
            write_params(CMD_DISPON);
            _asleep = false;
        }
    }

    // LilyGO never uses idle mode (0x38/0x39)
    void setPowerSave(bool) override {}

  protected:
    static constexpr uint32_t RM69A10_SLEEP_DELAY_MS = 120;

    // Record = [length][command][length - 1 parameters], 0 ends a list.
    // 1:1 from LilyGO rm69a10.h kInitSequence; the base class then sends MADCTL and COLMOD 0x55.
    // TODO(hw-verify): MADCTL/COLMOD are not in the vendor table; partial window shows columns 3..564 only
    const uint8_t *getInitParams(size_t listno) const override
    {
        static constexpr uint8_t list0[] = {
            2, 0xFE, 0xFD,                   // command page 0xFD
            2, 0x80, 0xFC,                   // vendor register
            2, 0xFE, 0x00,                   // command page 0
            5, 0x2A, 0x00, 0x00, 0x02, 0x37, // CASET 0..567
            5, 0x2B, 0x00, 0x00, 0x04, 0xCF, // RASET 0..1231
            5, 0x31, 0x00, 0x03, 0x02, 0x34, // partial columns 3..564
            5, 0x30, 0x00, 0x00, 0x04, 0xCF, // partial rows 0..1231
            2, 0x12, 0x00,                   // partial mode on
            2, 0x35, 0x00,                   // TE on
            2, 0x51, 0x00,                   // brightness 0, raised by LGFX_Device::init_impl()
            1, 0x11,                         // SLPOUT, followed by getInitDelay(0)
            0,
        };
        static constexpr uint8_t list1[] = {
            1, 0x29, // DISPON
            0,
        };
        switch (listno) {
        case 0:
            return list0;
        case 1:
            return list1;
        default:
            return nullptr;
        }
    }

    size_t getInitDelay(size_t listno) const override { return listno == 0 ? RM69A10_SLEEP_DELAY_MS : 0; }

  private:
    bool _ready = false;
    bool _invertSent = false;
    bool _asleep = false;
    uint32_t _sleepInMs = 0;
};

/**
 * AMOLED brightness through DCS 0x51 (no backlight pin), so that device-ui sees a light and can dim.
 */
struct Light_RM69A10 : public lgfx::ILight {
    void setBus(lgfx::experimental::Bus_DSI *bus) { _bus = bus; }

    // Called by Panel_Device::init() before the init table: nothing may be sent here
    bool init(uint8_t) override { return true; }

    // An awake panel is never left at 0 (black): it is switched off with DISPOFF/SLPIN, not with the brightness.
    // TODO(hw-verify): one parameter byte as LilyGO does (8-bit brightness mode assumed); lowest visible level
    void setBrightness(uint8_t brightness) override
    {
        if (brightness < RM69A10_MIN_BRIGHTNESS)
            brightness = RM69A10_MIN_BRIGHTNESS;
        if (_bus)
            _bus->writeParams(RM69A10_CMD_WRDISBV, &brightness, 1);
    }

  private:
    static constexpr uint8_t RM69A10_CMD_WRDISBV = 0x51;
    static constexpr uint8_t RM69A10_MIN_BRIGHTNESS = 2; // 1 %, the lowest step of the MUI brightness slider

    lgfx::experimental::Bus_DSI *_bus = nullptr;
};
