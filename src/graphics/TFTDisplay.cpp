#include "configuration.h"
#include "main.h"

#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON HIGH
#endif

#ifdef GPIO_EXTENDER
#include <SparkFunSX1509.h>
#include <Wire.h>
extern SX1509 gpioExtender;
#endif

#ifdef TFT_MESH_OVERRIDE
uint16_t TFT_MESH = TFT_MESH_OVERRIDE;
#else
uint16_t TFT_MESH = COLOR565(0x67, 0xEA, 0x94);
#endif

#if defined(ST7735S)
#include <LovyanGFX.hpp> // Graphics and font library for ST7735 driver chip

#ifndef TFT_INVERT
#define TFT_INVERT true
#endif

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7735S _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

  public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();

            // configure SPI
            cfg.spi_host = ST7735_SPI_HOST; // ESP32-S2,S3,C3 : SPI2_HOST or SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
            cfg.spi_mode = 0;
            cfg.freq_write = SPI_FREQUENCY; // SPI clock for transmission (up to 80MHz, rounded to the value obtained by dividing
                                            // 80MHz by an integer)
            cfg.freq_read = SPI_READ_FREQUENCY; // SPI clock when receiving
            cfg.spi_3wire = false;              // Set to true if reception is done on the MOSI pin
            cfg.use_lock = true;                // Set to true to use transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO;  // SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch /
                                                // SPI_DMA_CH_AUTO=auto setting)
            cfg.pin_sclk = ST7735_SCK;          // Set SPI SCLK pin number
            cfg.pin_mosi = ST7735_SDA;          // Set SPI MOSI pin number
            cfg.pin_miso = ST7735_MISO;         // Set SPI MISO pin number (-1 = disable)
            cfg.pin_dc = ST7735_RS;             // Set SPI DC pin number (-1 = disable)

            _bus_instance.config(cfg);              // applies the set value to the bus.
            _panel_instance.setBus(&_bus_instance); // set the bus on the panel.
        }

        {                                        // Set the display panel control.
            auto cfg = _panel_instance.config(); // Gets a structure for display panel settings.

            cfg.pin_cs = ST7735_CS;     // Pin number where CS is connected (-1 = disable)
            cfg.pin_rst = ST7735_RESET; // Pin number where RST is connected  (-1 = disable)
            cfg.pin_busy = ST7735_BUSY; // Pin number where BUSY is connected (-1 = disable)

            // The following setting values ​​are general initial values ​​for each panel, so please comment out any
            // unknown items and try them.

            cfg.panel_width = TFT_WIDTH;   // actual displayable width
            cfg.panel_height = TFT_HEIGHT; // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;   // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;   // Panel offset amount in Y direction
            cfg.offset_rotation = 0;       // Rotation direction value offset 0~7 (4~7 is upside down)
            cfg.dummy_read_pixel = 8;      // Number of bits for dummy read before pixel readout
            cfg.dummy_read_bits = 1;       // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;           // Set to true if data can be read
            cfg.invert = TFT_INVERT;       // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;         // Set to true if the panel's red and blue are swapped
            cfg.dlen_16bit =
                false;             // Set to true for panels that transmit data length in 16-bit units with 16-bit parallel or SPI
            cfg.bus_shared = true; // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            // Set the following only when the display is shifted with a driver with a variable number of pixels, such as the
            // ST7735 or ILI9163.
            cfg.memory_width = TFT_WIDTH;   // Maximum width supported by the driver IC
            cfg.memory_height = TFT_HEIGHT; // Maximum height supported by the driver IC
            _panel_instance.config(cfg);
        }

#ifdef TFT_BL
        // Set the backlight control
        {
            auto cfg = _light_instance.config(); // Gets a structure for backlight settings.

            cfg.pin_bl = TFT_BL; // Pin number to which the backlight is connected
            cfg.invert = true;   // true to invert the brightness of the backlight
            // cfg.freq = 44100;    // PWM frequency of backlight
            // cfg.pwm_channel = 1; // PWM channel number to use

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance); // Set the backlight on the panel.
        }
#endif

        setPanel(&_panel_instance);
    }
};

static LGFX *tft = nullptr;

#elif defined(RAK14014)
#include <RAK14014_FT6336U.h>
#include <TFT_eSPI.h>
TFT_eSPI *tft = nullptr;
FT6336U ft6336u;

static uint8_t _rak14014_touch_int = false; // TP interrupt generation flag.
static void rak14014_tpIntHandle(void)
{
    _rak14014_touch_int = true;
}

#elif defined(ST72xx_DE)
#include <LovyanGFX.hpp>
#include <TCA9534.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
TCA9534 ioex;

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Bus_RGB _bus_instance;
    lgfx::Panel_RGB _panel_instance;
    lgfx::Touch_GT911 _touch_instance;

  public:
    const uint16_t screenWidth = TFT_WIDTH;
    const uint16_t screenHeight = TFT_HEIGHT;

    bool init_impl(bool use_reset, bool use_clear) override
    {
        ioex.attach(Wire);
        ioex.setDeviceAddress(0x18);
        ioex.config(1, TCA9534::Config::OUT);
        ioex.config(2, TCA9534::Config::OUT);
        ioex.config(3, TCA9534::Config::OUT);
        ioex.config(4, TCA9534::Config::OUT);

        ioex.output(1, TCA9534::Level::H);
        ioex.output(3, TCA9534::Level::L);
        ioex.output(4, TCA9534::Level::H);

        pinMode(1, OUTPUT);
        digitalWrite(1, LOW);
        ioex.output(2, TCA9534::Level::L);
        delay(20);
        ioex.output(2, TCA9534::Level::H);
        delay(100);
        pinMode(1, INPUT);

        return LGFX_Device::init_impl(use_reset, use_clear);
    }

    LGFX(void)
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
            cfg.use_psram = 0;
            _panel_instance.config_detail(cfg);
        }

        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;
            cfg.pin_d0 = ST72xx_B0;  // B0
            cfg.pin_d1 = ST72xx_B1;  // B1
            cfg.pin_d2 = ST72xx_B2;  // B2
            cfg.pin_d3 = ST72xx_B3;  // B3
            cfg.pin_d4 = ST72xx_B4;  // B4
            cfg.pin_d5 = ST72xx_G0;  // G0
            cfg.pin_d6 = ST72xx_G1;  // G1
            cfg.pin_d7 = ST72xx_G2;  // G2
            cfg.pin_d8 = ST72xx_G3;  // G3
            cfg.pin_d9 = ST72xx_G4;  // G4
            cfg.pin_d10 = ST72xx_G5; // G5
            cfg.pin_d11 = ST72xx_R0; // R0
            cfg.pin_d12 = ST72xx_R1; // R1
            cfg.pin_d13 = ST72xx_R2; // R2
            cfg.pin_d14 = ST72xx_R3; // R3
            cfg.pin_d15 = ST72xx_R4; // R4

            cfg.pin_henable = ST72xx_DE;
            cfg.pin_vsync = ST72xx_VSYNC;
            cfg.pin_hsync = ST72xx_HSYNC;
            cfg.pin_pclk = ST72xx_PCLK;
            cfg.freq_write = 13000000;

#ifdef ST7265_HSYNC_POLARITY
            cfg.hsync_polarity = ST7265_HSYNC_POLARITY;
            cfg.hsync_front_porch = ST7265_HSYNC_FRONT_PORCH; // 8;
            cfg.hsync_pulse_width = ST7265_HSYNC_PULSE_WIDTH; // 4;
            cfg.hsync_back_porch = ST7265_HSYNC_BACK_PORCH;   // 8;

            cfg.vsync_polarity = ST7265_VSYNC_POLARITY;       // 0;
            cfg.vsync_front_porch = ST7265_VSYNC_FRONT_PORCH; // 8;
            cfg.vsync_pulse_width = ST7265_VSYNC_PULSE_WIDTH; // 4;
            cfg.vsync_back_porch = ST7265_VSYNC_BACK_PORCH;   // 8;

            cfg.pclk_idle_high = 1;
            cfg.pclk_active_neg = ST7265_PCLK_ACTIVE_NEG; // 0;
            // cfg.pclk_idle_high = 0;
            // cfg.de_idle_high = 1;
#endif

#ifdef ST7262_HSYNC_POLARITY
            cfg.hsync_polarity = ST7262_HSYNC_POLARITY;
            cfg.hsync_front_porch = ST7262_HSYNC_FRONT_PORCH; // 8;
            cfg.hsync_pulse_width = ST7262_HSYNC_PULSE_WIDTH; // 4;
            cfg.hsync_back_porch = ST7262_HSYNC_BACK_PORCH;   // 8;

            cfg.vsync_polarity = ST7262_VSYNC_POLARITY;       // 0;
            cfg.vsync_front_porch = ST7262_VSYNC_FRONT_PORCH; // 8;
            cfg.vsync_pulse_width = ST7262_VSYNC_PULSE_WIDTH; // 4;
            cfg.vsync_back_porch = ST7262_VSYNC_BACK_PORCH;   // 8;

            cfg.pclk_idle_high = 1;
            cfg.pclk_active_neg = ST7262_PCLK_ACTIVE_NEG; // 0;
            // cfg.pclk_idle_high = 0;
            // cfg.de_idle_high = 1;
#endif

#ifdef SC7277_HSYNC_POLARITY
            cfg.hsync_polarity = SC7277_HSYNC_POLARITY;
            cfg.hsync_front_porch = SC7277_HSYNC_FRONT_PORCH; // 8;
            cfg.hsync_pulse_width = SC7277_HSYNC_PULSE_WIDTH; // 4;
            cfg.hsync_back_porch = SC7277_HSYNC_BACK_PORCH;   // 8;

            cfg.vsync_polarity = SC7277_VSYNC_POLARITY;       // 0;
            cfg.vsync_front_porch = SC7277_VSYNC_FRONT_PORCH; // 8;
            cfg.vsync_pulse_width = SC7277_VSYNC_PULSE_WIDTH; // 4;
            cfg.vsync_back_porch = SC7277_VSYNC_BACK_PORCH;   // 8;

            cfg.pclk_idle_high = 1;
            cfg.pclk_active_neg = SC7277_PCLK_ACTIVE_NEG; // 0;
            // cfg.pclk_idle_high = 0;
            // cfg.de_idle_high = 1;
#endif

            _bus_instance.config(cfg);
        }
        _panel_instance.setBus(&_bus_instance);

        {
            auto cfg = _touch_instance.config();
            cfg.x_min = 0;
            cfg.x_max = TFT_WIDTH;
            cfg.y_min = 0;
            cfg.y_max = TFT_HEIGHT;
            cfg.pin_int = -1;
            cfg.pin_rst = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;

            cfg.i2c_port = 0;
            cfg.i2c_addr = 0x5D;
            cfg.pin_sda = I2C_SDA;
            cfg.pin_scl = I2C_SCL;
            cfg.freq = 400000;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX *tft = nullptr;

#elif defined(ILI9488_CS)
#include <LovyanGFX.hpp> // Graphics and font library for ILI9488 driver chip

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_GT911 _touch_instance;

  public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();

            // configure SPI
            cfg.spi_host = ILI9488_SPI_HOST; // ESP32-S2,S3,C3 : SPI2_HOST or SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
            cfg.spi_mode = 0;
            cfg.freq_write = SPI_FREQUENCY; // SPI clock for transmission (up to 80MHz, rounded to the value obtained by dividing
                                            // 80MHz by an integer)
            cfg.freq_read = SPI_READ_FREQUENCY; // SPI clock when receiving
            cfg.spi_3wire = false;              // Set to true if reception is done on the MOSI pin
            cfg.use_lock = true;                // Set to true to use transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO;  // SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch /
                                                // SPI_DMA_CH_AUTO=auto setting)
            cfg.pin_sclk = ILI9488_SCK;         // Set SPI SCLK pin number
            cfg.pin_mosi = ILI9488_SDA;         // Set SPI MOSI pin number
            cfg.pin_miso = ILI9488_MISO;        // Set SPI MISO pin number (-1 = disable)
            cfg.pin_dc = ILI9488_RS;            // Set SPI DC pin number (-1 = disable)

            _bus_instance.config(cfg);              // applies the set value to the bus.
            _panel_instance.setBus(&_bus_instance); // set the bus on the panel.
        }

        {                                        // Set the display panel control.
            auto cfg = _panel_instance.config(); // Gets a structure for display panel settings.

            cfg.pin_cs = ILI9488_CS; // Pin number where CS is connected (-1 = disable)
            cfg.pin_rst = -1;        // Pin number where RST is connected  (-1 = disable)
            cfg.pin_busy = -1;       // Pin number where BUSY is connected (-1 = disable)

            // The following setting values ​​are general initial values ​​for each panel, so please comment out any
            // unknown items and try them.

            cfg.memory_width = TFT_WIDTH;                 // Maximum width supported by the driver IC
            cfg.memory_height = TFT_HEIGHT;               // Maximum height supported by the driver IC
            cfg.panel_width = TFT_WIDTH;                  // actual displayable width
            cfg.panel_height = TFT_HEIGHT;                // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;                  // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;                  // Panel offset amount in Y direction
            cfg.offset_rotation = TFT_OFFSET_ROTATION;    // Rotation direction value offset 0~7 (4~7 is mirrored)
#ifdef TFT_DUMMY_READ_PIXELS
            cfg.dummy_read_pixel = TFT_DUMMY_READ_PIXELS; // Number of bits for dummy read before pixel readout
#else
            cfg.dummy_read_pixel = 9; // Number of bits for dummy read before pixel readout
#endif
            cfg.dummy_read_bits = 1;                      // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;                          // Set to true if data can be read
            cfg.invert = true;                            // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;                        // Set to true if the panel's red and blue are swapped
            cfg.dlen_16bit =
                false;             // Set to true for panels that transmit data length in 16-bit units with 16-bit parallel or SPI
            cfg.bus_shared = true; // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            // Set the following only when the display is shifted with a driver with a variable number of pixels, such as the
            // ST7735 or ILI9163.
            // cfg.memory_width = TFT_WIDTH;   // Maximum width supported by the driver IC
            // cfg.memory_height = TFT_HEIGHT; // Maximum height supported by the driver IC
            _panel_instance.config(cfg);
        }

#ifdef ILI9488_BL
        // Set the backlight control
        {
            auto cfg = _light_instance.config(); // Gets a structure for backlight settings.

            cfg.pin_bl = ILI9488_BL; // Pin number to which the backlight is connected
            cfg.invert = false;      // true to invert the brightness of the backlight
            // cfg.freq = 44100;    // PWM frequency of backlight
            // cfg.pwm_channel = 1; // PWM channel number to use

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance); // Set the backlight on the panel.
        }
#endif

#if HAS_TOUCHSCREEN
        // Configure settings for touch screen control.
        {
            auto cfg = _touch_instance.config();

            cfg.pin_cs = -1;
            cfg.x_min = 0;
            cfg.x_max = TFT_HEIGHT - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_WIDTH - 1;
            cfg.pin_int = SCREEN_TOUCH_INT;
#ifdef SCREEN_TOUCH_RST
            cfg.pin_rst = SCREEN_TOUCH_RST;
#endif
            cfg.bus_shared = true;
            cfg.offset_rotation = TFT_OFFSET_ROTATION;
            // cfg.freq = 2500000;

            // I2C
            cfg.i2c_port = TOUCH_I2C_PORT;
            cfg.i2c_addr = TOUCH_SLAVE_ADDRESS;
#ifdef SCREEN_TOUCH_USE_I2C1
            cfg.pin_sda = I2C_SDA1;
            cfg.pin_scl = I2C_SCL1;
#else
            cfg.pin_sda = I2C_SDA;
            cfg.pin_scl = I2C_SCL;
#endif
            // cfg.freq = 400000;

            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
#endif

        setPanel(&_panel_instance);
    }
};

static LGFX *tft = nullptr;

#elif defined(ST7789_CS)
#include <LovyanGFX.hpp> // Graphics and font library for ST7735 driver chip
#ifdef HELTEC_V4_TFT
#include "chsc6x.h"
#include "lgfx/v1/Touch.hpp"
namespace lgfx
{
inline namespace v1
{
class TOUCH_CHSC6X : public ITouch
{
  public:
    TOUCH_CHSC6X(void)
    {
        _cfg.i2c_addr = TOUCH_SLAVE_ADDRESS;
        _cfg.x_min = 0;
        _cfg.x_max = 240;
        _cfg.y_min = 0;
        _cfg.y_max = 320;
    };

    bool init(void) override
    {
        if (chsc6xTouch == nullptr) {
            chsc6xTouch = new chsc6x(&Wire1, TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_INT_PIN, TOUCH_RST_PIN);
        }
        chsc6xTouch->chsc6x_init();
        return true;
    };

    uint_fast8_t getTouchRaw(touch_point_t *tp, uint_fast8_t count) override
    {
        uint16_t raw_x, raw_y;
        if (chsc6xTouch->chsc6x_read_touch_info(&raw_x, &raw_y) == 0) {
            tp[0].x = 320 - 1 - raw_y;
            tp[0].y = 240 - 1 - raw_x;
            tp[0].size = 1;
            tp[0].id = 1;
            return 1;
        }
        tp[0].size = 0;
        return 0;
    };

    void wakeup(void) override{};
    void sleep(void) override{};

  private:
    chsc6x *chsc6xTouch = nullptr;
};
} // namespace v1
} // namespace lgfx
#endif
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;
#if HAS_TOUCHSCREEN
#if defined(T_WATCH_S3) || defined(ELECROW)
    lgfx::Touch_FT5x06 _touch_instance;
#elif defined(HELTEC_V4_TFT)
    lgfx::TOUCH_CHSC6X _touch_instance;
#else
    lgfx::Touch_GT911 _touch_instance;
#endif
#endif

  public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();

            // SPI
            cfg.spi_host = ST7789_SPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = SPI_FREQUENCY; // SPI clock for transmission (up to 80MHz, rounded to the value obtained by dividing
                                            // 80MHz by an integer)
            cfg.freq_read = SPI_READ_FREQUENCY; // SPI clock when receiving
            cfg.spi_3wire = false;
            cfg.use_lock = true;               // Set to true to use transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO; // SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch /
                                               // SPI_DMA_CH_AUTO=auto setting)
            cfg.pin_sclk = ST7789_SCK;         // Set SPI SCLK pin number
            cfg.pin_mosi = ST7789_SDA;         // Set SPI MOSI pin number
            cfg.pin_miso = ST7789_MISO;        // Set SPI MISO pin number (-1 = disable)
            cfg.pin_dc = ST7789_RS;            // Set SPI DC pin number (-1 = disable)

            _bus_instance.config(cfg);              // applies the set value to the bus.
            _panel_instance.setBus(&_bus_instance); // set the bus on the panel.
        }

        {                                        // Set the display panel control.
            auto cfg = _panel_instance.config(); // Gets a structure for display panel settings.

            cfg.pin_cs = ST7789_CS;     // Pin number where CS is connected (-1 = disable)
            cfg.pin_rst = ST7789_RESET; // Pin number where RST is connected  (-1 = disable)
            cfg.pin_busy = ST7789_BUSY; // Pin number where BUSY is connected (-1 = disable)

            // The following setting values ​​are general initial values ​​for each panel, so please comment out any
            // unknown items and try them.
#if defined(T_WATCH_S3)
            cfg.panel_width = 240;
            cfg.panel_height = 240;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;                             // No vertical shift needed — panel is top-aligned
            cfg.offset_rotation = 2;                      // Rotate 180° to correct upside-down layout
#else
            cfg.memory_width = TFT_WIDTH;              // Maximum width supported by the driver IC
            cfg.memory_height = TFT_HEIGHT;            // Maximum height supported by the driver IC
            cfg.panel_width = TFT_WIDTH;               // actual displayable width
            cfg.panel_height = TFT_HEIGHT;             // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;               // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;               // Panel offset amount in Y direction
            cfg.offset_rotation = TFT_OFFSET_ROTATION; // Rotation direction value offset 0~7 (4~7 is mirrored)
#endif
#ifdef TFT_DUMMY_READ_PIXELS
            cfg.dummy_read_pixel = TFT_DUMMY_READ_PIXELS; // Number of bits for dummy read before pixel readout
#else
            cfg.dummy_read_pixel = 9;                  // Number of bits for dummy read before pixel readout
#endif
            cfg.dummy_read_bits = 1;                      // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;                          // Set to true if data can be read
            cfg.invert = true;                            // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;                        // Set to true if the panel's red and blue are swapped
            cfg.dlen_16bit =
                false;             // Set to true for panels that transmit data length in 16-bit units with 16-bit parallel or SPI
            cfg.bus_shared = true; // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            // Set the following only when the display is shifted with a driver with a variable number of pixels, such as the
            // ST7735 or ILI9163.
            // cfg.memory_width = TFT_WIDTH;   // Maximum width supported by the driver IC
            // cfg.memory_height = TFT_HEIGHT; // Maximum height supported by the driver IC
            _panel_instance.config(cfg);
        }

#ifdef ST7789_BL
        // Set the backlight control. (delete if not necessary)
        {
            auto cfg = _light_instance.config(); // Gets a structure for backlight settings.

            cfg.pin_bl = ST7789_BL; // Pin number to which the backlight is connected
            cfg.invert = false;     // true to invert the brightness of the backlight
            // cfg.pwm_channel = 0;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance); // Set the backlight on the panel.
        }
#endif

#if HAS_TOUCHSCREEN
        // Configure settings for touch screen control.
        {
            auto cfg = _touch_instance.config();

            cfg.pin_cs = -1;
            cfg.x_min = 0;
            cfg.x_max = TFT_HEIGHT - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_WIDTH - 1;
            cfg.pin_int = SCREEN_TOUCH_INT;
#ifdef SCREEN_TOUCH_RST
            cfg.pin_rst = SCREEN_TOUCH_RST;
#endif
            cfg.bus_shared = true;
            cfg.offset_rotation = TFT_OFFSET_ROTATION;
            // cfg.freq = 2500000;

            // I2C
            cfg.i2c_port = TOUCH_I2C_PORT;
            cfg.i2c_addr = TOUCH_SLAVE_ADDRESS;
#ifdef SCREEN_TOUCH_USE_I2C1
            cfg.pin_sda = I2C_SDA1;
            cfg.pin_scl = I2C_SCL1;
#else
            cfg.pin_sda = I2C_SDA;
            cfg.pin_scl = I2C_SCL;
#endif
            // cfg.freq = 400000;

            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
#endif

        setPanel(&_panel_instance); // Sets the panel to use.
    }
};

static LGFX *tft = nullptr;

#elif defined(ST7796_CS)
#include <LovyanGFX.hpp> // Graphics and font library for ST7796 driver chip

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7796 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

  public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();

            // SPI
            cfg.spi_host = ST7796_SPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = SPI_FREQUENCY; // SPI clock for transmission (up to 80MHz, rounded to the value obtained by dividing
                                            // 80MHz by an integer)
            cfg.freq_read = SPI_READ_FREQUENCY; // SPI clock when receiving
            cfg.spi_3wire = false;
            cfg.use_lock = true;               // Set to true to use transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO; // SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch /
                                               // SPI_DMA_CH_AUTO=auto setting)
            cfg.pin_sclk = ST7796_SCK;         // Set SPI SCLK pin number
            cfg.pin_mosi = ST7796_SDA;         // Set SPI MOSI pin number
            cfg.pin_miso = ST7796_MISO;        // Set SPI MISO pin number (-1 = disable)
            cfg.pin_dc = ST7796_RS;            // Set SPI DC pin number (-1 = disable)

            _bus_instance.config(cfg);              // applies the set value to the bus.
            _panel_instance.setBus(&_bus_instance); // set the bus on the panel.
        }

        {                                        // Set the display panel control.
            auto cfg = _panel_instance.config(); // Gets a structure for display panel settings.

            cfg.pin_cs = ST7796_CS;     // Pin number where CS is connected (-1 = disable)
            cfg.pin_rst = ST7796_RESET; // Pin number where RST is connected  (-1 = disable)
            cfg.pin_busy = ST7796_BUSY; // Pin number where BUSY is connected (-1 = disable)

            // cfg.memory_width = TFT_WIDTH;              // Maximum width supported by the driver IC
            // cfg.memory_height = TFT_HEIGHT;            // Maximum height supported by the driver IC
            cfg.panel_width = TFT_WIDTH;                  // actual displayable width
            cfg.panel_height = TFT_HEIGHT;                // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;                  // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;                  // Panel offset amount in Y direction
            cfg.offset_rotation = TFT_OFFSET_ROTATION;    // Rotation direction value offset 0~7 (4~7 is mirrored)
#ifdef TFT_DUMMY_READ_PIXELS
            cfg.dummy_read_pixel = TFT_DUMMY_READ_PIXELS; // Number of bits for dummy read before pixel readout
#else
            cfg.dummy_read_pixel = 8; // Number of bits for dummy read before pixel readout
#endif
            cfg.dummy_read_bits = 1;                      // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;                          // Set to true if data can be read
            cfg.invert = true;                            // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;                        // Set to true if the panel's red and blue are swapped
            cfg.dlen_16bit =
                false;             // Set to true for panels that transmit data length in 16-bit units with 16-bit parallel or SPI
            cfg.bus_shared = true; // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            _panel_instance.config(cfg);
        }

#ifdef ST7796_BL
        // Set the backlight control. (delete if not necessary)
        {
            auto cfg = _light_instance.config(); // Gets a structure for backlight settings.

            cfg.pin_bl = ST7796_BL; // Pin number to which the backlight is connected
            cfg.invert = false;     // true to invert the brightness of the backlight
            cfg.freq = 44100;
            cfg.pwm_channel = 7;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance); // Set the backlight on the panel.
        }
#endif

        setPanel(&_panel_instance); // Sets the panel to use.
    }
};

static LGFX *tft = nullptr;

#elif defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER)

#include <LovyanGFX.hpp> // Graphics and font library for ILI9341/ILI9342 driver chip

#if defined(ILI9341_BACKLIGHT_EN) && !defined(TFT_BL)
#define TFT_BL ILI9341_BACKLIGHT_EN
#endif

class LGFX : public lgfx::LGFX_Device
{
#if defined(ILI9341_DRIVER)
    lgfx::Panel_ILI9341 _panel_instance;
#elif defined(ILI9342_DRIVER)
    lgfx::Panel_ILI9342 _panel_instance;
#endif
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

  public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();

            // configure SPI
#if defined(ILI9341_DRIVER)
            cfg.spi_host = ILI9341_SPI_HOST; // ESP32-S2,S3,C3 : SPI2_HOST or SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
#elif defined(ILI9342_DRIVER)
            cfg.spi_host = ILI9342_SPI_HOST; // ESP32-S2,S3,C3 : SPI2_HOST or SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
#endif
            cfg.spi_mode = 0;
            cfg.freq_write = SPI_FREQUENCY; // SPI clock for transmission (up to 80MHz, rounded to the value obtained by dividing
                                            // 80MHz by an integer)
            cfg.freq_read = SPI_READ_FREQUENCY; // SPI clock when receiving
            cfg.spi_3wire = false;              // Set to true if reception is done on the MOSI pin
            cfg.use_lock = true;                // Set to true to use transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO;  // SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch /
                                                // SPI_DMA_CH_AUTO=auto setting)
            cfg.pin_sclk = TFT_SCLK;            // Set SPI SCLK pin number
            cfg.pin_mosi = TFT_MOSI;            // Set SPI MOSI pin number
            cfg.pin_miso = TFT_MISO;            // Set SPI MISO pin number (-1 = disable)
            cfg.pin_dc = TFT_DC;                // Set SPI DC pin number (-1 = disable)

            _bus_instance.config(cfg);              // applies the set value to the bus.
            _panel_instance.setBus(&_bus_instance); // set the bus on the panel.
        }

        {                                        // Set the display panel control.
            auto cfg = _panel_instance.config(); // Gets a structure for display panel settings.

            cfg.pin_cs = TFT_CS;     // Pin number where CS is connected (-1 = disable)
            cfg.pin_rst = TFT_RST;   // Pin number where RST is connected  (-1 = disable)
            cfg.pin_busy = TFT_BUSY; // Pin number where BUSY is connected (-1 = disable)

            // The following setting values ​​are general initial values ​​for each panel, so please comment out any
            // unknown items and try them.

            cfg.panel_width = TFT_WIDTH;   // actual displayable width
            cfg.panel_height = TFT_HEIGHT; // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;   // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;   // Panel offset amount in Y direction
            cfg.offset_rotation = 0;       // Rotation direction value offset 0~7 (4~7 is upside down)
            cfg.dummy_read_pixel = 8;      // Number of bits for dummy read before pixel readout
            cfg.dummy_read_bits = 1;       // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;           // Set to true if data can be read
            cfg.invert = false;            // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;         // Set to true if the panel's red and blue are swapped
            cfg.dlen_16bit =
                false;             // Set to true for panels that transmit data length in 16-bit units with 16-bit parallel or SPI
            cfg.bus_shared = true; // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            // Set the following only when the display is shifted with a driver with a variable number of pixels, such as the
            // ST7735 or ILI9163.
            cfg.memory_width = TFT_WIDTH;   // Maximum width supported by the driver IC
            cfg.memory_height = TFT_HEIGHT; // Maximum height supported by the driver IC
            _panel_instance.config(cfg);
        }

#ifdef TFT_BL
        // Set the backlight control
        {
            auto cfg = _light_instance.config(); // Gets a structure for backlight settings.

            cfg.pin_bl = TFT_BL; // Pin number to which the backlight is connected
            cfg.invert = false;  // true to invert the brightness of the backlight
            // cfg.freq = 44100;    // PWM frequency of backlight
            // cfg.pwm_channel = 1; // PWM channel number to use

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance); // Set the backlight on the panel.
        }
#endif

        setPanel(&_panel_instance);
    }
};

static LGFX *tft = nullptr;

#elif defined(ST7735_CS)
#include <TFT_eSPI.h> // Graphics and font library for ILI9342 driver chip

static TFT_eSPI *tft = nullptr; // Invoke library, pins defined in User_Setup.h
#elif ARCH_PORTDUINO
#include "Panel_sdl.hpp"
#include <LovyanGFX.hpp> // Graphics and font library for ST7735 driver chip

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Bus_SPI _bus_instance;

    lgfx::ITouch *_touch_instance;

  public:
    lgfx::Panel_Device *_panel_instance;

    LGFX(void)
    {
        if (portduino_config.displayPanel == st7789)
            _panel_instance = new lgfx::Panel_ST7789;
        else if (portduino_config.displayPanel == st7735)
            _panel_instance = new lgfx::Panel_ST7735;
        else if (portduino_config.displayPanel == st7735s)
            _panel_instance = new lgfx::Panel_ST7735S;
        else if (portduino_config.displayPanel == st7796)
            _panel_instance = new lgfx::Panel_ST7796;
        else if (portduino_config.displayPanel == ili9341)
            _panel_instance = new lgfx::Panel_ILI9341;
        else if (portduino_config.displayPanel == ili9342)
            _panel_instance = new lgfx::Panel_ILI9342;
        else if (portduino_config.displayPanel == ili9488)
            _panel_instance = new lgfx::Panel_ILI9488;
        else if (portduino_config.displayPanel == hx8357d)
            _panel_instance = new lgfx::Panel_HX8357D;
#if defined(SDL_h_)

        else if (portduino_config.displayPanel == x11)
            _panel_instance = new lgfx::Panel_sdl;
#endif
        else {
            _panel_instance = new lgfx::Panel_NULL;
            LOG_ERROR("Unknown display panel configured!");
        }

        auto buscfg = _bus_instance.config();
        buscfg.spi_mode = 0;
        buscfg.spi_host = portduino_config.display_spi_dev_int;

        buscfg.pin_dc = portduino_config.displayDC.pin; // Set SPI DC pin number (-1 = disable)

        _bus_instance.config(buscfg); // applies the set value to the bus.
        if (portduino_config.displayPanel != x11)
            _panel_instance->setBus(&_bus_instance); // set the bus on the panel.

        auto cfg = _panel_instance->config(); // Gets a structure for display panel settings.
        LOG_DEBUG("Width: %d, Height: %d", portduino_config.displayWidth, portduino_config.displayHeight);
        cfg.pin_cs = portduino_config.displayCS.pin; // Pin number where CS is connected (-1 = disable)
        cfg.pin_rst = portduino_config.displayReset.pin;
        if (portduino_config.displayRotate) {
            cfg.panel_width = portduino_config.displayHeight; // actual displayable width
            cfg.panel_height = portduino_config.displayWidth; // actual displayable height
        } else {
            cfg.panel_width = portduino_config.displayWidth;   // actual displayable width
            cfg.panel_height = portduino_config.displayHeight; // actual displayable height
        }
        cfg.offset_x = portduino_config.displayOffsetX;             // Panel offset amount in X direction
        cfg.offset_y = portduino_config.displayOffsetY;             // Panel offset amount in Y direction
        cfg.offset_rotation = portduino_config.displayOffsetRotate; // Rotation direction value offset 0~7 (4~7 is mirrored)
        cfg.invert = portduino_config.displayInvert;                // Set to true if the light/darkness of the panel is reversed

        _panel_instance->config(cfg);

        // Configure settings for touch  control.
        if (portduino_config.touchscreenModule) {
            if (portduino_config.touchscreenModule == xpt2046) {
                _touch_instance = new lgfx::Touch_XPT2046;
            } else if (portduino_config.touchscreenModule == stmpe610) {
                _touch_instance = new lgfx::Touch_STMPE610;
            } else if (portduino_config.touchscreenModule == ft5x06) {
                _touch_instance = new lgfx::Touch_FT5x06;
            }
            auto touch_cfg = _touch_instance->config();

            touch_cfg.pin_cs = portduino_config.touchscreenCS.pin;
            touch_cfg.x_min = 0;
            touch_cfg.x_max = portduino_config.displayHeight - 1;
            touch_cfg.y_min = 0;
            touch_cfg.y_max = portduino_config.displayWidth - 1;
            touch_cfg.pin_int = portduino_config.touchscreenIRQ.pin;
            touch_cfg.bus_shared = true;
            touch_cfg.offset_rotation = portduino_config.touchscreenRotate;
            if (portduino_config.touchscreenI2CAddr != -1) {
                touch_cfg.i2c_addr = portduino_config.touchscreenI2CAddr;
            } else {
                touch_cfg.spi_host = portduino_config.touchscreen_spi_dev_int;
            }

            _touch_instance->config(touch_cfg);
            _panel_instance->setTouch(_touch_instance);
        }
#if defined(SDL_h_)
        if (portduino_config.displayPanel == x11) {
            lgfx::Panel_sdl *sdl_panel_ = (lgfx::Panel_sdl *)_panel_instance;
            sdl_panel_->setup();
            sdl_panel_->addKeyCodeMapping(SDLK_RETURN, SDL_SCANCODE_KP_ENTER);
        }
#endif
        setPanel(_panel_instance); // Sets the panel to use.
    }
};

static LGFX *tft = nullptr;

#elif defined(HX8357_CS)
#include <LovyanGFX.hpp> // Graphics and font library for HX8357 driver chip

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_HX8357D _panel_instance;
    lgfx::Bus_SPI _bus_instance;
#if defined(USE_XPT2046)
    lgfx::Touch_XPT2046 _touch_instance;
#endif

  public:
    LGFX(void)
    {
        // Panel_HX8357D
        {
            // configure SPI
            auto cfg = _bus_instance.config();

            cfg.spi_host = HX8357_SPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = SPI_FREQUENCY; // SPI clock for transmission (up to 80MHz, rounded to the value obtained by dividing
                                            // 80MHz by an integer)
            cfg.freq_read = SPI_READ_FREQUENCY; // SPI clock when receiving
            cfg.spi_3wire = false;              // Set to true if reception is done on the MOSI pin
            cfg.use_lock = true;                // Set to true to use transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO;  // SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch /
                                                // SPI_DMA_CH_AUTO=auto setting)
            cfg.pin_sclk = HX8357_SCK;          // Set SPI SCLK pin number
            cfg.pin_mosi = HX8357_MOSI;         // Set SPI MOSI pin number
            cfg.pin_miso = HX8357_MISO;         // Set SPI MISO pin number (-1 = disable)
            cfg.pin_dc = HX8357_RS;             // Set SPI DC pin number (-1 = disable)

            _bus_instance.config(cfg);              // applies the set value to the bus.
            _panel_instance.setBus(&_bus_instance); // set the bus on the panel.
        }
        {
            // Set the display panel control.
            auto cfg = _panel_instance.config(); // Gets a structure for display panel settings.

            cfg.pin_cs = HX8357_CS;     // Pin number where CS is connected (-1 = disable)
            cfg.pin_rst = HX8357_RESET; // Pin number where RST is connected  (-1 = disable)
            cfg.pin_busy = HX8357_BUSY; // Pin number where BUSY is connected (-1 = disable)

            cfg.panel_width = TFT_WIDTH;               // actual displayable width
            cfg.panel_height = TFT_HEIGHT;             // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;               // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;               // Panel offset amount in Y direction
            cfg.offset_rotation = TFT_OFFSET_ROTATION; // Rotation direction value offset 0~7 (4~7 is upside down)
            cfg.dummy_read_pixel = 8;                  // Number of bits for dummy read before pixel readout
            cfg.dummy_read_bits = 1;                   // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;                       // Set to true if data can be read
            cfg.invert = TFT_INVERT;                   // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;                     // Set to true if the panel's red and blue are swapped
            cfg.dlen_16bit = false;
            cfg.bus_shared = true; // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            _panel_instance.config(cfg);
        }
#if defined(USE_XPT2046)
        {
            // Configure settings for touch control.
            auto touch_cfg = _touch_instance.config();

            touch_cfg.pin_cs = TOUCH_CS;
            touch_cfg.x_min = 0;
            touch_cfg.x_max = TFT_HEIGHT - 1;
            touch_cfg.y_min = 0;
            touch_cfg.y_max = TFT_WIDTH - 1;
            touch_cfg.pin_int = -1;
            touch_cfg.bus_shared = true;
            touch_cfg.offset_rotation = 1;

            _touch_instance.config(touch_cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
#endif
        setPanel(&_panel_instance);
    }
};

static LGFX *tft = nullptr;

#elif defined(ST7701_CS)
#include <LovyanGFX.hpp> // Graphics and font library for ST7701 driver chip
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

class PanelInit_ST7701 : public lgfx::Panel_ST7701
{
  public:
    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        // 180 degree hw rotation: vertical flip, horizontal flip
        static constexpr const uint8_t list1[] = {0x36, 1,   0x10,                         // MADCTL for vertical flip
                                                  0xFF, 5,   0x77, 0x01, 0x00, 0x00, 0x10, // Command2 BK0 SEL
                                                  0xC7, 1,   0x04, // SDIR: X-direction Control (Horizontal Flip)
                                                  0xFF, 5,   0x77, 0x01, 0x00, 0x00, 0x00, // Command2 BK0 DIS
                                                  0xFF, 0xFF};
        switch (listno) {
        case 1:
            return list1;
        default:
            return lgfx::Panel_ST7701::getInitCommands(listno);
        }
    }
};

class LGFX : public lgfx::LGFX_Device
{
    PanelInit_ST7701 _panel_instance;
    lgfx::Bus_RGB _bus_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_FT5x06 _touch_instance;

  public:
    LGFX(void)
    {
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = 800;
            cfg.memory_height = 480;
            cfg.panel_width = TFT_WIDTH;
            cfg.panel_height = TFT_HEIGHT;
            cfg.offset_x = TFT_OFFSET_X;
            cfg.offset_y = TFT_OFFSET_Y;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _panel_instance.config_detail();
            cfg.pin_cs = ST7701_CS;
            cfg.pin_sclk = ST7701_SCK;
            cfg.pin_mosi = ST7701_SDA;
            // cfg.use_psram = 1;
            _panel_instance.config_detail(cfg);
        }

        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;
#ifdef SENSECAP_INDICATOR
            cfg.pin_d0 = GPIO_NUM_15; // B0
            cfg.pin_d1 = GPIO_NUM_14; // B1
            cfg.pin_d2 = GPIO_NUM_13; // B2
            cfg.pin_d3 = GPIO_NUM_12; // B3
            cfg.pin_d4 = GPIO_NUM_11; // B4

            cfg.pin_d5 = GPIO_NUM_10; // G0
            cfg.pin_d6 = GPIO_NUM_9;  // G1
            cfg.pin_d7 = GPIO_NUM_8;  // G2
            cfg.pin_d8 = GPIO_NUM_7;  // G3
            cfg.pin_d9 = GPIO_NUM_6;  // G4
            cfg.pin_d10 = GPIO_NUM_5; // G5

            cfg.pin_d11 = GPIO_NUM_4; // R0
            cfg.pin_d12 = GPIO_NUM_3; // R1
            cfg.pin_d13 = GPIO_NUM_2; // R2
            cfg.pin_d14 = GPIO_NUM_1; // R3
            cfg.pin_d15 = GPIO_NUM_0; // R4

            cfg.pin_henable = GPIO_NUM_18;
            cfg.pin_vsync = GPIO_NUM_17;
            cfg.pin_hsync = GPIO_NUM_16;
            cfg.pin_pclk = GPIO_NUM_21;
            cfg.freq_write = 12000000;

            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 10;
            cfg.hsync_pulse_width = 8;
            cfg.hsync_back_porch = 50;

            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 10;
            cfg.vsync_pulse_width = 8;
            cfg.vsync_back_porch = 20;

            cfg.pclk_active_neg = 0;
            cfg.de_idle_high = 1;
            cfg.pclk_idle_high = 0;
#endif
            _bus_instance.config(cfg);
        }
        _panel_instance.setBus(&_bus_instance);

        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = ST7701_BL;
            _light_instance.config(cfg);
        }
        _panel_instance.light(&_light_instance);

        {
            auto cfg = _touch_instance.config();
            cfg.pin_cs = -1;
            cfg.x_min = 0;
            cfg.x_max = 479;
            cfg.y_min = 0;
            cfg.y_max = 479;
            cfg.pin_int = -1; // don't use SCREEN_TOUCH_INT;
            cfg.pin_rst = SCREEN_TOUCH_RST;
            cfg.bus_shared = true;
            cfg.offset_rotation = TFT_OFFSET_ROTATION;

            cfg.i2c_port = TOUCH_I2C_PORT;
            cfg.i2c_addr = TOUCH_SLAVE_ADDRESS;
            cfg.pin_sda = I2C_SDA;
            cfg.pin_scl = I2C_SCL;
            cfg.freq = 400000;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX *tft = nullptr;

#endif

#if defined(ST7701_CS) || defined(ST7735_CS) || defined(ST7789_CS) || defined(ST7796_CS) || defined(ILI9341_DRIVER) ||           \
    defined(ILI9342_DRIVER) || defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS) || defined(ST72xx_DE) ||           \
    (ARCH_PORTDUINO && HAS_SCREEN != 0)
#include "SPILock.h"
#include "TFTDisplay.h"
#include <SPI.h>

#ifdef UNPHONE
#include "unPhone.h"
extern unPhone unphone;
#endif

GpioPin *TFTDisplay::backlightEnable = NULL;

TFTDisplay::TFTDisplay(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus)
{
    LOG_DEBUG("TFTDisplay!");

#ifdef TFT_BL
    GpioPin *p = new GpioHwPin(TFT_BL);

    if (!TFT_BACKLIGHT_ON) { // Need to invert the pin before hardware
        auto virtPin = new GpioVirtPin();
        new GpioNotTransformer(
            virtPin, p); // We just leave this created object on the heap so it can stay watching virtPin and driving en_gpio
        p = virtPin;
    }
#else
    GpioPin *p = new GpioVirtPin(); // Just simulate a pin
#endif
    backlightEnable = p;

#if ARCH_PORTDUINO
    if (portduino_config.displayRotate) {
        setGeometry(GEOMETRY_RAWMODE, portduino_config.displayWidth, portduino_config.displayWidth);
    } else {
        setGeometry(GEOMETRY_RAWMODE, portduino_config.displayHeight, portduino_config.displayHeight);
    }

#elif defined(SCREEN_ROTATE)
    setGeometry(GEOMETRY_RAWMODE, TFT_HEIGHT, TFT_WIDTH);
#else
    setGeometry(GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
}

TFTDisplay::~TFTDisplay()
{
    // Clean up allocated line pixel buffer to prevent memory leak
    if (linePixelBuffer != nullptr) {
        free(linePixelBuffer);
        linePixelBuffer = nullptr;
    }
}

// Write the buffer to the display memory
void TFTDisplay::display(bool fromBlank)
{
    if (fromBlank)
        tft->fillScreen(TFT_BLACK);

    concurrency::LockGuard g(spiLock);

    uint32_t x, y;
    uint32_t y_byteIndex;
    uint8_t y_byteMask;
    uint32_t x_FirstPixelUpdate;
    uint32_t x_LastPixelUpdate;
    bool isset, dblbuf_isset;
    uint16_t colorTftMesh, colorTftBlack;
    bool somethingChanged = false;

    // Store colors byte-reversed so that TFT_eSPI doesn't have to swap bytes in a separate step
    colorTftMesh = (TFT_MESH >> 8) | ((TFT_MESH & 0xFF) << 8);
    colorTftBlack = (TFT_BLACK >> 8) | ((TFT_BLACK & 0xFF) << 8);

    y = 0;
    while (y < displayHeight) {
        y_byteIndex = (y / 8) * displayWidth;
        y_byteMask = (1 << (y & 7));

        // Step 1: Do a quick scan of 8 rows together. This allows fast-forwarding over unchanged screen areas.
        if (y_byteMask == 1) {
            if (!fromBlank) {
                for (x = 0; x < displayWidth; x++) {
                    if (buffer[x + y_byteIndex] != buffer_back[x + y_byteIndex])
                        break;
                }
            } else {
                for (x = 0; x < displayWidth; x++) {
                    if (buffer[x + y_byteIndex] != 0)
                        break;
                }
            }
            if (x >= displayWidth) {
                // No changed pixels found in these 8 rows, fast-forward to the next 8
                y = y + 8;
                continue;
            }
        }

        // Step 2: Scan each of the 8 rows individually. Find the first pixel in each row that needs updating
        for (x_FirstPixelUpdate = 0; x_FirstPixelUpdate < displayWidth; x_FirstPixelUpdate++) {
            isset = buffer[x_FirstPixelUpdate + y_byteIndex] & y_byteMask;

            if (!fromBlank) {
                // get src pixel in the page based ordering the OLED lib uses
                dblbuf_isset = buffer_back[x_FirstPixelUpdate + y_byteIndex] & y_byteMask;
                if (isset != dblbuf_isset) {
                    break;
                }
            } else if (isset) {
                break;
            }
        }

        // Did we find a pixel that needs updating on this row?
        if (x_FirstPixelUpdate < displayWidth) {

            // Quickly write out the first changed pixel (saves another array lookup)
            linePixelBuffer[x_FirstPixelUpdate] = isset ? colorTftMesh : colorTftBlack;
            x_LastPixelUpdate = x_FirstPixelUpdate;

            // Step 3: copy all remaining pixels in this row into the pixel line buffer,
            // while also recording the last pixel in the row that needs updating
            for (x = x_FirstPixelUpdate + 1; x < displayWidth; x++) {
                isset = buffer[x + y_byteIndex] & y_byteMask;
                linePixelBuffer[x] = isset ? colorTftMesh : colorTftBlack;

                if (!fromBlank) {
                    dblbuf_isset = buffer_back[x + y_byteIndex] & y_byteMask;
                    if (isset != dblbuf_isset) {
                        x_LastPixelUpdate = x;
                    }
                } else if (isset) {
                    x_LastPixelUpdate = x;
                }
            }

            // Step 4: Send the changed pixels on this line to the screen as a single block transfer.
            // This function accepts pixel data MSB first so it can dump the memory straight out the SPI port.
            tft->pushRect(x_FirstPixelUpdate, y, (x_LastPixelUpdate - x_FirstPixelUpdate + 1), 1,
                          &linePixelBuffer[x_FirstPixelUpdate]);

            somethingChanged = true;
        }
        y++;
    }
    // Copy the Buffer to the Back Buffer
    if (somethingChanged)
        memcpy(buffer_back, buffer, displayBufferSize);
}

void TFTDisplay::sdlLoop()
{
#if defined(SDL_h_)
    static int lastPressed = 0;
    static int shuttingDown = false;
    if (portduino_config.displayPanel == x11) {
        lgfx::Panel_sdl *sdl_panel_ = (lgfx::Panel_sdl *)tft->_panel_instance;
        if (sdl_panel_->loop() && !shuttingDown) {
            LOG_WARN("Window Closed!");
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_SHUTDOWN, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        }
        // debounce
        if (lastPressed != 0 && !sdl_panel_->gpio_in(lastPressed))
            return;
        if (!sdl_panel_->gpio_in(37)) {
            lastPressed = 37;
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_RIGHT, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else if (!sdl_panel_->gpio_in(36)) {
            lastPressed = 36;
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_UP, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else if (!sdl_panel_->gpio_in(38)) {
            lastPressed = 38;
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_DOWN, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else if (!sdl_panel_->gpio_in(39)) {
            lastPressed = 39;
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_LEFT, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else if (!sdl_panel_->gpio_in(SDL_SCANCODE_KP_ENTER)) {
            lastPressed = SDL_SCANCODE_KP_ENTER;
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_SELECT, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        } else {
            lastPressed = 0;
        }
    }
#endif
}

// Send a command to the display (low level function)
void TFTDisplay::sendCommand(uint8_t com)
{
    // handle display on/off directly
    switch (com) {
    case DISPLAYON: {
        // LOG_DEBUG("Display on");
        backlightEnable->set(true);
#if ARCH_PORTDUINO
        display(true);
        if (portduino_config.displayBacklight.pin > 0)
            digitalWrite(portduino_config.displayBacklight.pin, TFT_BACKLIGHT_ON);
#elif !defined(RAK14014) && !defined(M5STACK) && !defined(UNPHONE)
        tft->wakeup();
        tft->powerSaveOff();
#endif

#ifdef VTFT_CTRL
        digitalWrite(VTFT_CTRL, LOW);
#endif
#ifdef UNPHONE
        unphone.backlight(true); // using unPhone library
#endif
#ifdef RAK14014
#elif !defined(M5STACK) && !defined(ST7789_CS) // T-Deck gets brightness set in Screen.cpp in the handleSetOn function
        tft->setBrightness(172);
#endif
        break;
    }
    case DISPLAYOFF: {
        // LOG_DEBUG("Display off");
        backlightEnable->set(false);
#if ARCH_PORTDUINO
        tft->clear();
        if (portduino_config.displayBacklight.pin > 0)
            digitalWrite(portduino_config.displayBacklight.pin, !TFT_BACKLIGHT_ON);
#elif !defined(RAK14014) && !defined(M5STACK) && !defined(UNPHONE)
        tft->sleep();
        tft->powerSaveOn();
#endif

#ifdef VTFT_CTRL
        digitalWrite(VTFT_CTRL, HIGH);
#endif
#ifdef UNPHONE
        unphone.backlight(false); // using unPhone library
#endif
#ifdef RAK14014
#elif !defined(M5STACK)
        tft->setBrightness(0);
#endif
        break;
    }
    default:
        break;
    }

    // Drop all other commands to device (we just update the buffer)
}

void TFTDisplay::setDisplayBrightness(uint8_t _brightness)
{
#ifdef RAK14014
    // todo
#else
    tft->setBrightness(_brightness);
    LOG_DEBUG("Brightness is set to value: %i ", _brightness);
#endif
}

void TFTDisplay::flipScreenVertically()
{
#if defined(T_WATCH_S3)
    LOG_DEBUG("Flip TFT vertically"); // T-Watch S3 right-handed orientation
    tft->setRotation(0);
#endif
}

bool TFTDisplay::hasTouch(void)
{
#ifdef RAK14014
    return true;
#elif !defined(M5STACK)
    return tft->touch() != nullptr;
#else
    return false;
#endif
}

bool TFTDisplay::getTouch(int16_t *x, int16_t *y)
{
#ifdef RAK14014
    if (_rak14014_touch_int) {
        _rak14014_touch_int = false;
        /* The X and Y axes have to be switched */
        *y = ft6336u.read_touch1_x();
        *x = TFT_HEIGHT - ft6336u.read_touch1_y();
        return true;
    } else {
        return false;
    }
#elif !defined(M5STACK)
    return tft->getTouch(x, y);
#else
    return false;
#endif
}

void TFTDisplay::setDetected(uint8_t detected)
{
    (void)detected;
}

// Connect to the display
bool TFTDisplay::connect()
{
    concurrency::LockGuard g(spiLock);
    LOG_INFO("Do TFT init");
#ifdef RAK14014
    tft = new TFT_eSPI;
#else
    tft = new LGFX;
#endif

    backlightEnable->set(true);
    LOG_INFO("Power to TFT Backlight");

#ifdef UNPHONE
    unphone.backlight(true); // using unPhone library
#endif

    tft->init();

#if defined(M5STACK)
    tft->setRotation(0);
#elif defined(RAK14014)
    tft->setRotation(1);
    tft->setSwapBytes(true);
    //    tft->fillScreen(TFT_BLACK);
    ft6336u.begin();
    pinMode(SCREEN_TOUCH_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SCREEN_TOUCH_INT), rak14014_tpIntHandle, FALLING);
#elif defined(T_DECK) || defined(PICOMPUTER_S3) || defined(CHATTER_2)
    tft->setRotation(1); // T-Deck has the TFT in landscape
#elif defined(T_WATCH_S3)
    tft->setRotation(2); // T-Watch S3 left-handed orientation
#elif ARCH_PORTDUINO || defined(SENSECAP_INDICATOR) || defined(T_LORA_PAGER)
    tft->setRotation(0); // use config.yaml to set rotation
#else
    tft->setRotation(3); // Orient horizontal and wide underneath the silkscreen name label
#endif
    tft->fillScreen(TFT_BLACK);

    if (this->linePixelBuffer == NULL) {
        this->linePixelBuffer = (uint16_t *)malloc(sizeof(uint16_t) * displayWidth);

        if (!this->linePixelBuffer) {
            LOG_ERROR("Not enough memory to create TFT line buffer\n");
            return false;
        }
    }
    return true;
}

#endif
