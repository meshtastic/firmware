#include "configuration.h"

#if defined(HAS_HUB75_NATIVE)

#include "HUB75Native.h"
#include "PortduinoGlue.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include <led-matrix.h>

HUB75Native::HUB75Native(uint8_t, int, int, OLEDDISPLAY_GEOMETRY, HW_I2C)
{
    // The BaseUI treats the panel as a generic raw framebuffer (not an SSD1306 page layout). The
    // panel size comes from config.yaml at runtime (cols*chain x rows*parallel, computed in
    // loadConfig()). Config is fully loaded by portduinoSetup() before Screen constructs.
    setGeometry(GEOMETRY_RAWMODE, portduino_config.displayWidth, portduino_config.displayHeight);
    LOG_DEBUG("HUB75Native %dx%d", portduino_config.displayWidth, portduino_config.displayHeight);
}

HUB75Native::~HUB75Native()
{
    if (matrix) {
        delete matrix;
        matrix = nullptr;
    }
}

// Bring up the rpi-rgb-led-matrix driver from config.yaml. The library owns the GPIO pins
// (selected by HardwareMapping) and runs its own refresh thread, so there is no DMA/I2S setup.
bool HUB75Native::connect()
{
    LOG_INFO("Do HUB75 init");

    rgb_matrix::RGBMatrix::Options options;
    options.hardware_mapping = portduino_config.hub75_hardware_mapping.c_str();
    options.rows = portduino_config.hub75_rows;
    options.cols = portduino_config.hub75_cols;
    options.chain_length = portduino_config.hub75_chain_length;
    options.parallel = portduino_config.hub75_parallel;
    options.pwm_bits = portduino_config.hub75_pwm_bits;
    options.pwm_lsb_nanoseconds = portduino_config.hub75_pwm_lsb_nanoseconds;
    options.brightness = portduino_config.hub75_brightness;
    options.scan_mode = portduino_config.hub75_scan_mode;
    options.row_address_type = portduino_config.hub75_row_address_type;
    options.multiplexing = portduino_config.hub75_multiplexing;
    options.disable_hardware_pulsing = portduino_config.hub75_disable_hardware_pulsing;
    options.show_refresh_rate = portduino_config.hub75_show_refresh_rate;
    options.inverse_colors = portduino_config.hub75_inverse_colors;
    // RGB order is handled by the library instead of a software swap (see display()).
    options.led_rgb_sequence = portduino_config.hub75_led_rgb_sequence.c_str();
    options.limit_refresh_rate_hz = portduino_config.hub75_limit_refresh_rate_hz;
    if (!portduino_config.hub75_pixel_mapper_config.empty())
        options.pixel_mapper_config = portduino_config.hub75_pixel_mapper_config.c_str();
    if (!portduino_config.hub75_panel_type.empty())
        options.panel_type = portduino_config.hub75_panel_type.c_str();

    rgb_matrix::RuntimeOptions runtime;
    runtime.gpio_slowdown = portduino_config.hub75_gpio_slowdown;
    runtime.daemon = 0;          // run in-process; the library starts its own refresh thread
    runtime.drop_privileges = 0; // meshtasticd manages its own privileges (keeps GPIO/LoRa access)

    matrix = rgb_matrix::CreateMatrixFromOptions(options, runtime);
    if (matrix == nullptr) {
        LOG_ERROR("HUB75 CreateMatrixFromOptions() failed (need root / /dev/gpiomem on a Pi?)");
        return false;
    }
    brightness = portduino_config.hub75_brightness; // library scale is 1..100
    matrix->SetBrightness(brightness);              // applies to the matrix and all its FrameCanvases
    matrix->Clear();
    // Offscreen buffer for double-buffered, tear-free presentation (see display()).
    offscreen = matrix->CreateFrameCanvas();
    return true;
}

void HUB75Native::display()
{
    if (!matrix || !offscreen)
        return;

    const uint16_t onNative = graphics::TFTPalette::White;
    const uint16_t offNative = graphics::getThemeBodyBg();

    const uint16_t onBe = (uint16_t)((onNative >> 8) | (onNative << 8));
    const uint16_t offBe = (uint16_t)((offNative >> 8) | (offNative << 8));

#if GRAPHICS_TFT_COLORING_ENABLED
    const bool hasColorRegions = graphics::getTFTColorRegionCount() > 0;
#endif

    // Full-frame redraw into the offscreen canvas every call (no dirty-diff needed at these
    // resolutions), then SwapOnVSync() below presents it atomically for tear-free output.
    for (uint16_t y = 0; y < displayHeight; y++) {
        const uint32_t yByteIndex = (y / 8) * displayWidth;
        const uint8_t yByteMask = (uint8_t)(1 << (y & 7));

#if GRAPHICS_TFT_COLORING_ENABLED
        if (hasColorRegions)
            graphics::beginTFTColorRow((int16_t)y);
#endif

        for (uint16_t x = 0; x < displayWidth; x++) {
            const bool isset = (buffer[x + yByteIndex] & yByteMask) != 0;

            uint16_t be;
#if GRAPHICS_TFT_COLORING_ENABLED
            if (hasColorRegions)
                be = graphics::resolveTFTColorPixelRow((int16_t)x, isset, onBe, offBe);
            else
                be = isset ? onBe : offBe;
#else
            be = isset ? onBe : offBe;
#endif

            const uint16_t c = (uint16_t)((be >> 8) | (be << 8)); // back to native RGB565
            const uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
            const uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
            const uint8_t b = (uint8_t)((c & 0x1F) << 3);
            offscreen->SetPixel(x, y, r, g, b);
        }
    }

#if GRAPHICS_TFT_COLORING_ENABLED
    // Regions are re-registered every frame by the renderers; clear so they don't accumulate.
    graphics::clearTFTColorRegions();
#endif

    // Present the finished frame at the next vsync; the returned canvas (the previously shown one)
    // becomes our next offscreen buffer.
    offscreen = matrix->SwapOnVSync(offscreen);
}

void HUB75Native::sendCommand(uint8_t com)
{
    if (!matrix || !offscreen)
        return;

    switch (com) {
    case DISPLAYON:
        matrix->SetBrightness(brightness);
        break;
    case DISPLAYOFF:
        // rpi-rgb-led-matrix clamps brightness 0 up to 1 and its refresh thread keeps scanning the
        // presented canvas, so SetBrightness(0) alone leaves the last frame faintly lit. Present a
        // cleared frame so the panel actually goes black while asleep. (matrix->Clear() would only
        // clear the RGBMatrix's own buffer, not the FrameCanvas we swap in.) Wake repaints via the
        // next display().
        offscreen->Clear();
        offscreen = matrix->SwapOnVSync(offscreen);
        break;
    default:
        // Drop all other SSD1306 init/config commands - not meaningful for the matrix.
        break;
    }
}

void HUB75Native::setDisplayBrightness(uint8_t _brightness)
{
    brightness = _brightness;
    if (matrix)
        matrix->SetBrightness(brightness);
}

#endif // HAS_HUB75_NATIVE
