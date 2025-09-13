/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)

Porting for SDL:
 [imliubo](https://github.com/imliubo)
/----------------------------------------------------------------------------*/
#pragma once

#define SDL_MAIN_HANDLED
// cppcheck-suppress preprocessorErrorDirective
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#include <SDL_main.h>
#endif

#if defined(SDL_h_)
#include "lgfx/v1/Touch.hpp"
#include "lgfx/v1/misc/range.hpp"
#include "lgfx/v1/panel/Panel_FrameBufferBase.hpp"
#include <cstdint>

namespace lgfx
{
inline namespace v1
{

struct Panel_sdl;
struct monitor_t {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    SDL_Texture *texture_frameimage = nullptr;
    Panel_sdl *panel = nullptr;

    // 外枠
    const void *frame_image = 0;
    uint_fast16_t frame_width = 0;
    uint_fast16_t frame_height = 0;
    uint_fast16_t frame_inner_x = 0;
    uint_fast16_t frame_inner_y = 0;
    int_fast16_t frame_rotation = 0;
    int_fast16_t frame_angle = 0;

    float scaling_x = 1;
    float scaling_y = 1;
    int_fast16_t touch_x, touch_y;
    bool touched = false;
    bool closing = false;
};
//----------------------------------------------------------------------------

struct Touch_sdl : public ITouch {
    bool init(void) override { return true; }
    void wakeup(void) override {}
    void sleep(void) override {}
    bool isEnable(void) override { return true; };
    uint_fast8_t getTouchRaw(touch_point_t *tp, uint_fast8_t count) override { return 0; }
};

//----------------------------------------------------------------------------

struct Panel_sdl : public Panel_FrameBufferBase {
    static constexpr size_t EMULATED_GPIO_MAX = 128;
    static volatile uint8_t _gpio_dummy_values[EMULATED_GPIO_MAX];

  public:
    Panel_sdl(void);
    virtual ~Panel_sdl(void);

    bool init(bool use_reset) override;

    color_depth_t setColorDepth(color_depth_t depth) override;

    void display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h) override;

    // void setInvert(bool invert) override {}
    void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override;
    void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, uint32_t rawcolor) override;
    void writeBlock(uint32_t rawcolor, uint32_t length) override;
    void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t *param,
                    bool use_dma) override;
    void writeImageARGB(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t *param) override;
    void writePixels(pixelcopy_t *param, uint32_t len, bool use_dma) override;

    uint_fast8_t getTouchRaw(touch_point_t *tp, uint_fast8_t count) override;

    void setWindowTitle(const char *title);
    void setScaling(uint_fast8_t scaling_x, uint_fast8_t scaling_y);
    void setFrameImage(const void *frame_image, int frame_width, int frame_height, int inner_x, int inner_y);
    void setFrameRotation(uint_fast16_t frame_rotaion);
    void setBrightness(uint8_t brightness) override{};

    static volatile void gpio_hi(uint32_t pin) { _gpio_dummy_values[pin & (EMULATED_GPIO_MAX - 1)] = 1; }
    static volatile void gpio_lo(uint32_t pin) { _gpio_dummy_values[pin & (EMULATED_GPIO_MAX - 1)] = 0; }
    static volatile bool gpio_in(uint32_t pin) { return _gpio_dummy_values[pin & (EMULATED_GPIO_MAX - 1)]; }

    static int setup(void);
    static int loop(void);
    static int close(void);

    static int main(int (*fn)(bool *), uint32_t msec_step_exec = 512);

    static void setShortcutKeymod(SDL_Keymod keymod) { _keymod = keymod; }

    struct KeyCodeMapping_t {
        SDL_KeyCode keycode = SDLK_UNKNOWN;
        uint8_t gpio = 0;
    };
    static void addKeyCodeMapping(SDL_KeyCode keyCode, uint8_t gpio);
    static int getKeyCodeMapping(SDL_KeyCode keyCode);

  protected:
    const char *_window_title = "LGFX Simulator";
    SDL_mutex *_sdl_mutex = nullptr;

    void sdl_create(monitor_t *m);
    void sdl_update(void);

    touch_point_t _touch_point;
    monitor_t monitor;

    rgb888_t *_texturebuf = nullptr;
    uint_fast16_t _modified_counter;
    uint_fast16_t _texupdate_counter;
    uint_fast16_t _display_counter;
    bool _invalidated;

    static void _event_proc(void);
    static void _update_proc(void);
    static void _update_scaling(monitor_t *m, float sx, float sy);
    void sdl_invalidate(void) { _invalidated = true; }
    void render_texture(SDL_Texture *texture, int tx, int ty, int tw, int th, float angle);
    bool initFrameBuffer(size_t width, size_t height);
    void deinitFrameBuffer(void);

    static SDL_Keymod _keymod;

    struct lock_t {
        lock_t(Panel_sdl *parent);
        ~lock_t();

      protected:
        Panel_sdl *_parent;
    };
};
//----------------------------------------------------------------------------
} // namespace v1
} // namespace lgfx
#endif