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

#ifndef TFT_MESH
#define TFT_MESH COLOR565(0x67, 0xEA, 0x94)
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

#elif defined(ST7789_CS)
#include <LovyanGFX.hpp> // Graphics and font library for ST7735 driver chip

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;
#if HAS_TOUCHSCREEN
#ifdef T_WATCH_S3
    lgfx::Touch_FT5x06 _touch_instance;
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

            cfg.pin_cs = ST7789_CS; // Pin number where CS is connected (-1 = disable)
            cfg.pin_rst = -1;       // Pin number where RST is connected  (-1 = disable)
            cfg.pin_busy = -1;      // Pin number where BUSY is connected (-1 = disable)

            // The following setting values ​​are general initial values ​​for each panel, so please comment out any
            // unknown items and try them.

            cfg.panel_width = TFT_WIDTH;               // actual displayable width
            cfg.panel_height = TFT_HEIGHT;             // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;               // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;               // Panel offset amount in Y direction
            cfg.offset_rotation = TFT_OFFSET_ROTATION; // Rotation direction value offset 0~7 (4~7 is mirrored)
            cfg.dummy_read_pixel = 9;                  // Number of bits for dummy read before pixel readout
            cfg.dummy_read_bits = 1;                   // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;                       // Set to true if data can be read
            cfg.invert = true;                         // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;                     // Set to true if the panel's red and blue are swapped
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

#elif defined(ILI9341_DRIVER)

#include <LovyanGFX.hpp> // Graphics and font library for ILI9341 driver chip

#if defined(ILI9341_BACKLIGHT_EN) && !defined(TFT_BL)
#define TFT_BL ILI9341_BACKLIGHT_EN
#endif

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

  public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();

            // configure SPI
            cfg.spi_host = ILI9341_SPI_HOST; // ESP32-S2,S3,C3 : SPI2_HOST or SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
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
#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip

static TFT_eSPI *tft = nullptr; // Invoke library, pins defined in User_Setup.h
#elif ARCH_PORTDUINO && HAS_SCREEN != 0
#include <LovyanGFX.hpp> // Graphics and font library for ST7735 driver chip

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_LCD *_panel_instance;
    lgfx::Bus_SPI _bus_instance;

    lgfx::ITouch *_touch_instance;

  public:
    LGFX(void)
    {
        if (settingsMap[displayPanel] == st7789)
            _panel_instance = new lgfx::Panel_ST7789;
        else if (settingsMap[displayPanel] == st7735)
            _panel_instance = new lgfx::Panel_ST7735;
        else if (settingsMap[displayPanel] == st7735s)
            _panel_instance = new lgfx::Panel_ST7735S;
        else if (settingsMap[displayPanel] == ili9341)
            _panel_instance = new lgfx::Panel_ILI9341;
        auto buscfg = _bus_instance.config();
        buscfg.spi_mode = 0;
        buscfg.spi_host = settingsMap[displayspidev];

        buscfg.pin_dc = settingsMap[displayDC]; // Set SPI DC pin number (-1 = disable)

        _bus_instance.config(buscfg);            // applies the set value to the bus.
        _panel_instance->setBus(&_bus_instance); // set the bus on the panel.

        auto cfg = _panel_instance->config(); // Gets a structure for display panel settings.
        LOG_DEBUG("Height: %d, Width: %d \n", settingsMap[displayHeight], settingsMap[displayWidth]);
        cfg.pin_cs = settingsMap[displayCS]; // Pin number where CS is connected (-1 = disable)
        cfg.pin_rst = settingsMap[displayReset];
        cfg.panel_width = settingsMap[displayWidth];   // actual displayable width
        cfg.panel_height = settingsMap[displayHeight]; // actual displayable height
        cfg.offset_x = settingsMap[displayOffsetX];    // Panel offset amount in X direction
        cfg.offset_y = settingsMap[displayOffsetY];    // Panel offset amount in Y direction
        cfg.offset_rotation = 0;                       // Rotation direction value offset 0~7 (4~7 is mirrored)
        cfg.invert = settingsMap[displayInvert];       // Set to true if the light/darkness of the panel is reversed

        _panel_instance->config(cfg);

        // Configure settings for touch  control.
        if (settingsMap[touchscreenModule]) {
            if (settingsMap[touchscreenModule] == xpt2046) {
                _touch_instance = new lgfx::Touch_XPT2046;
            } else if (settingsMap[touchscreenModule] == stmpe610) {
                _touch_instance = new lgfx::Touch_STMPE610;
            } else if (settingsMap[touchscreenModule] == ft5x06) {
                _touch_instance = new lgfx::Touch_FT5x06;
            }
            auto touch_cfg = _touch_instance->config();

            touch_cfg.pin_cs = settingsMap[touchscreenCS];
            touch_cfg.x_min = 0;
            touch_cfg.x_max = settingsMap[displayHeight] - 1;
            touch_cfg.y_min = 0;
            touch_cfg.y_max = settingsMap[displayWidth] - 1;
            touch_cfg.pin_int = settingsMap[touchscreenIRQ];
            touch_cfg.bus_shared = true;
            touch_cfg.offset_rotation = 1;
            if (settingsMap[touchscreenI2CAddr] != -1) {
                touch_cfg.i2c_addr = settingsMap[touchscreenI2CAddr];
            } else {
                touch_cfg.spi_host = settingsMap[touchscreenspidev];
            }

            _touch_instance->config(touch_cfg);
            _panel_instance->setTouch(_touch_instance);
        }

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

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7701 _panel_instance;
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

#if defined(ST7701_CS) || defined(ST7735_CS) || defined(ST7789_CS) || defined(ILI9341_DRIVER) || defined(RAK14014) ||            \
    defined(HX8357_CS) || (ARCH_PORTDUINO && HAS_SCREEN != 0)
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
    LOG_DEBUG("TFTDisplay!\n");

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
    if (settingsMap[displayRotate]) {
        setGeometry(GEOMETRY_RAWMODE, settingsMap[configNames::displayHeight], settingsMap[configNames::displayWidth]);
    } else {
        setGeometry(GEOMETRY_RAWMODE, settingsMap[configNames::displayWidth], settingsMap[configNames::displayHeight]);
    }

#elif defined(SCREEN_ROTATE)
    setGeometry(GEOMETRY_RAWMODE, TFT_HEIGHT, TFT_WIDTH);
#else
    setGeometry(GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
}

// Write the buffer to the display memory
void TFTDisplay::display(bool fromBlank)
{
    if (fromBlank)
        tft->fillScreen(TFT_BLACK);
    // tft->clear();
    concurrency::LockGuard g(spiLock);

    uint16_t x, y;

    for (y = 0; y < displayHeight; y++) {
        for (x = 0; x < displayWidth; x++) {
            auto isset = buffer[x + (y / 8) * displayWidth] & (1 << (y & 7));
            if (!fromBlank) {
                // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
                auto dblbuf_isset = buffer_back[x + (y / 8) * displayWidth] & (1 << (y & 7));
                if (isset != dblbuf_isset) {
                    tft->drawPixel(x, y, isset ? TFT_MESH : TFT_BLACK);
                }
            } else if (isset) {
                tft->drawPixel(x, y, TFT_MESH);
            }
        }
    }
    // Copy the Buffer to the Back Buffer
    for (y = 0; y < (displayHeight / 8); y++) {
        for (x = 0; x < displayWidth; x++) {
            uint16_t pos = x + y * displayWidth;
            buffer_back[pos] = buffer[pos];
        }
    }
}

// Send a command to the display (low level function)
void TFTDisplay::sendCommand(uint8_t com)
{
    // handle display on/off directly
    switch (com) {
    case DISPLAYON: {
        // LOG_DEBUG("Display on\n");
        backlightEnable->set(true);
#if ARCH_PORTDUINO
        display(true);
        if (settingsMap[displayBacklight] > 0)
            digitalWrite(settingsMap[displayBacklight], TFT_BACKLIGHT_ON);
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
        // LOG_DEBUG("Display off\n");
        backlightEnable->set(false);
#if ARCH_PORTDUINO
        tft->clear();
        if (settingsMap[displayBacklight] > 0)
            digitalWrite(settingsMap[displayBacklight], !TFT_BACKLIGHT_ON);
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
    LOG_DEBUG("Brightness is set to value: %i \n", _brightness);
#endif
}

void TFTDisplay::flipScreenVertically()
{
#if defined(T_WATCH_S3)
    LOG_DEBUG("Flip TFT vertically\n"); // T-Watch S3 right-handed orientation
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
    LOG_INFO("Doing TFT init\n");
#ifdef RAK14014
    tft = new TFT_eSPI;
#else
    tft = new LGFX;
#endif

    backlightEnable->set(true);
    LOG_INFO("Power to TFT Backlight\n");

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
#elif defined(T_WATCH_S3) || defined(SENSECAP_INDICATOR)
    tft->setRotation(2); // T-Watch S3 left-handed orientation
#else
    tft->setRotation(3); // Orient horizontal and wide underneath the silkscreen name label
#endif
    tft->fillScreen(TFT_BLACK);

    return true;
}

#endif