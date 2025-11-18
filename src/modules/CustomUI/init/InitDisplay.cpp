#include "InitDisplay.h"
#include <LovyanGFX.hpp>
#include <Arduino.h>

// Add missing logging
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

// Display pins for Heltec V3 with external ST7789
#define TFT_MOSI 5   // Data line - GPIO5
#define TFT_SCLK 7   // Clock line - GPIO7
#define TFT_CS   6   // Chip select - GPIO6
#define TFT_DC   2   // Data/Command - GPIO2
#define TFT_RST  3   // Reset - GPIO3
#define TFT_BL   4   // Backlight - GPIO4

/**
 * LovyanGFX optimized display class for ST7789 with ESP32-S3
 * Configured for maximum performance with automatic PSRAM and DMA
 */
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

  public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();

            // Configure optimized SPI for ESP32-S3 with automatic PSRAM and DMA
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 45000000; // 45MHz write speed
            cfg.freq_read = 16000000;  // 16MHz read speed
            cfg.spi_3wire = false;
            cfg.use_lock = true;               // Enable transaction locking
            cfg.dma_channel = SPI_DMA_CH_AUTO; // Auto DMA for better performance
            cfg.pin_sclk = TFT_SCLK;
            cfg.pin_mosi = TFT_MOSI;
            cfg.pin_miso = -1;               // MISO not connected
            cfg.pin_dc = TFT_DC;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();

            cfg.pin_cs = TFT_CS;
            cfg.pin_rst = TFT_RST;
            cfg.pin_busy = -1;
            
            // Display configuration for 240x320 ST7789
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = true;  // Try inverting display colors
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;

            _panel_instance.config(cfg);
        }

        {
            auto cfg = _light_instance.config();

            cfg.pin_bl = TFT_BL;
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = 7;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

InitDisplay::InitDisplay() : tft(nullptr), initialized(false) {
    LOG_INFO("🔧 InitDisplay: Constructor");
}

InitDisplay::~InitDisplay() {
    cleanup();
}

bool InitDisplay::init() {
    LOG_INFO("🔧 InitDisplay: Initializing ST7789 display with LovyanGFX...");
    
    // Initialize backlight pin first
    if (TFT_BL >= 0) {
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH); // Turn on backlight
        delay(100);
    }
    
    // Create the LovyanGFX display instance
    tft = new LGFX();
    
    // Initialize the display - LovyanGFX handles SPI setup automatically
    tft->init();
    
    delay(100);
    tft->setRotation(1); // Landscape mode: 320x240
    tft->fillScreen(0x0000); // Pure black background for power efficiency
    
    initialized = true;
    LOG_INFO("🔧 InitDisplay: LovyanGFX initialized with 80MHz SPI, DMA, and PSRAM support");
    return true;
}

void InitDisplay::cleanup() {
    if (tft) {
        delete tft;
        tft = nullptr;
    }
    initialized = false;
    LOG_INFO("🔧 InitDisplay: Cleanup completed");
}