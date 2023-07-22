#include "configuration.h"

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON HIGH
#endif

// convert 24-bit color to 16-bit (56K)
#define COLOR565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))
#define TFT_MESH COLOR565(0x67, 0xEA, 0x94)

#if defined(ST7735S)
#include <LovyanGFX.hpp> // Graphics and font library for ST7735 driver chip

#if defined(ST7735_BACKLIGHT_EN) && !defined(TFT_BL)
#define TFT_BL ST7735_BACKLIGHT_EN
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
            cfg.invert = true;             // Set to true if the light/darkness of the panel is reversed
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

        // Set the backlight control
        {
            auto cfg = _light_instance.config(); // Gets a structure for backlight settings.

            cfg.pin_bl = ST7735_BL; // Pin number to which the backlight is connected
            cfg.invert = true;      // true to invert the brightness of the backlight
            // cfg.freq = 44100;    // PWM frequency of backlight
            // cfg.pwm_channel = 1; // PWM channel number to use

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance); // Set the backlight on the panel.
        }

        setPanel(&_panel_instance);
    }
};

static LGFX tft;

#elif defined(ST7789_CS)
#include <LovyanGFX.hpp> // Graphics and font library for ST7735 driver chip

#if defined(ST7789_BACKLIGHT_EN) && !defined(TFT_BL)
#define TFT_BL ST7789_BACKLIGHT_EN
#endif

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;
#ifdef T_WATCH_S3
    lgfx::Touch_FT5x06 _touch_instance;
#else
    lgfx::Touch_GT911 _touch_instance;
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
            cfg.spi_3wire = false;              // Set to true if reception is done on the MOSI pin
            cfg.use_lock = true;                // Set to true to use transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO;  // SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch /
            cfg.pin_sclk = ST7789_SCK;          // Set SPI SCLK pin number
            cfg.pin_mosi = ST7789_SDA;          // Set SPI MOSI pin number
            cfg.pin_miso = ST7789_MISO;         // Set SPI MISO pin number (-1 = disable)
            cfg.pin_dc = ST7789_RS;             // Set SPI DC pin number (-1 = disable)

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

            cfg.panel_width = TFT_WIDTH;   // actual displayable width
            cfg.panel_height = TFT_HEIGHT; // actual displayable height
            cfg.offset_x = TFT_OFFSET_X;   // Panel offset amount in X direction
            cfg.offset_y = TFT_OFFSET_Y;   // Panel offset amount in Y direction
            cfg.offset_rotation = 0;       // Rotation direction value offset 0~7 (4~7 is mirrored)
            cfg.dummy_read_pixel = 9;      // Number of bits for dummy read before pixel readout
            cfg.dummy_read_bits = 1;       // Number of bits for dummy read before non-pixel data read
            cfg.readable = true;           // Set to true if data can be read
            cfg.invert = true;             // Set to true if the light/darkness of the panel is reversed
            cfg.rgb_order = false;         // Set to true if the panel's red and blue are swapped
            cfg.dlen_16bit =
                false;             // Set to true for panels that transmit data length in 16-bit units with 16-bit parallel or SPI
            cfg.bus_shared = true; // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            // Set the following only when the display is shifted with a driver with a variable number of pixels, such as the
            // ST7735 or ILI9163.
            // cfg.memory_width = TFT_WIDTH;   // Maximum width supported by the driver IC
            // cfg.memory_height = TFT_HEIGHT; // Maximum height supported by the driver IC
            _panel_instance.config(cfg);
        }

        // Set the backlight control. (delete if not necessary)
        {
            auto cfg = _light_instance.config(); // Gets a structure for backlight settings.

            cfg.pin_bl = ST7789_BL; // Pin number to which the backlight is connected
            cfg.invert = true;      // true to invert the brightness of the backlight
            // cfg.pwm_channel = 0;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance); // Set the backlight on the panel.
        }

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
            cfg.offset_rotation = 0;
            // cfg.freq = 2500000;

            // I2C
            cfg.i2c_port = 1;
            cfg.i2c_addr = TOUCH_SLAVE_ADDRESS;
#ifdef SCREEN_TOUCH_USE_I2C1
            cfg.pin_sda = I2C_SDA1;
            cfg.pin_scl = I2C_SCL1;
#else
            cfg.pin_sda = I2C_SDA;
            cfg.pin_scl = I2C_SCL;
#endif
            cfg.freq = 400000;

            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance); // Sets the panel to use.
    }
};

static LGFX tft;

#elif defined(ST7735_CS) || defined(ILI9341_DRIVER)
#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip

static TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h

#endif

#if defined(ST7735_CS) || defined(ST7789_CS) || defined(ILI9341_DRIVER)
#include "SPILock.h"
#include "TFTDisplay.h"
#include <SPI.h>

TFTDisplay::TFTDisplay(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus)
{
#ifdef SCREEN_ROTATE
    setGeometry(GEOMETRY_RAWMODE, TFT_HEIGHT, TFT_WIDTH);
#else
    setGeometry(GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
}

// Write the buffer to the display memory
void TFTDisplay::display(void)
{
    concurrency::LockGuard g(spiLock);

    uint16_t x, y;

    for (y = 0; y < displayHeight; y++) {
        for (x = 0; x < displayWidth; x++) {
            // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
            auto isset = buffer[x + (y / 8) * displayWidth] & (1 << (y & 7));
            auto dblbuf_isset = buffer_back[x + (y / 8) * displayWidth] & (1 << (y & 7));
            if (isset != dblbuf_isset) {
                tft.drawPixel(x, y, isset ? TFT_MESH : TFT_BLACK);
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
#if defined(TFT_BL) && defined(TFT_BACKLIGHT_ON)
        digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif
#ifdef VTFT_CTRL
        digitalWrite(VTFT_CTRL, LOW);
#endif
        break;
    }
    case DISPLAYOFF: {
#if defined(TFT_BL) && defined(TFT_BACKLIGHT_ON)
        digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
#endif
#ifdef VTFT_CTRL
        digitalWrite(VTFT_CTRL, HIGH);
#endif
        break;
    }
    default:
        break;
    }

    // Drop all other commands to device (we just update the buffer)
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

#ifdef TFT_BL
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    pinMode(TFT_BL, OUTPUT);
#endif

    tft.init();
#if defined(M5STACK) || defined(T_DECK)
    tft.setRotation(1); // M5Stack/T-Deck have the TFT in landscape
#elif defined(T_WATCH_S3)
    tft.setRotation(0); // T-Watch S3 has the TFT in portrait
#else
    tft.setRotation(3); // Orient horizontal and wide underneath the silkscreen name label
#endif
    tft.fillScreen(TFT_BLACK);
    return true;
}

// Get touch coords from the display
void TFTDisplay::getTouch(int *x, int *y)
{
#ifndef M5STACK
    tft.getTouch(x, y);
#endif
}

#endif