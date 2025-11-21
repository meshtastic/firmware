#include "InitDisplay.h"
#include <LovyanGFX.hpp>
#include <Arduino.h>

#ifdef ESP32
#include <esp_heap_caps.h>
#include <driver/gpio.h>  // For gpio_hold_en() function
#endif

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

            // Configure SPI3_HOST to avoid conflict with LoRa radio on SPI2_HOST
            cfg.spi_host = SPI3_HOST;  // Use SPI3 instead of SPI2 to avoid LoRa conflict
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
    
    // // Initialize backlight pin first
    // if (TFT_BL >= 0) {
    //     pinMode(TFT_BL, OUTPUT);
    //     digitalWrite(TFT_BL, HIGH); // Turn on backlight
    //     delay(100);
    // }
    
    // Create the LovyanGFX display instance
    tft = new LGFX();
    
    // Initialize the display - LovyanGFX handles SPI setup automatically
    tft->init();
    
    // Report memory status and PSRAM availability
    LOG_INFO("🔧 InitDisplay: Memory Status Report:");
    LOG_INFO("🔧 InitDisplay: - Total Heap: %zu bytes (%.1fKB)", ESP.getHeapSize(), ESP.getHeapSize()/1024.0);
    LOG_INFO("🔧 InitDisplay: - Free Heap: %zu bytes (%.1fKB)", ESP.getFreeHeap(), ESP.getFreeHeap()/1024.0);
    
#if defined(CONFIG_SPIRAM_SUPPORT) && defined(BOARD_HAS_PSRAM)
    size_t psramSize = ESP.getPsramSize();
    if (psramSize > 0) {
        size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        LOG_INFO("🔧 InitDisplay: - PSRAM Total: %zu bytes (%.1fMB)", psramSize, psramSize/(1024.0*1024.0));
        LOG_INFO("🔧 InitDisplay: - PSRAM Free: %zu bytes (%.1fMB)", freePsram, freePsram/(1024.0*1024.0));
        LOG_INFO("🔧 InitDisplay: ✅ PSRAM ENABLED for graphics operations");
        tft->setColorDepth(16); // Use 16-bit color to optimize PSRAM usage
    } else {
        LOG_INFO("🔧 InitDisplay: ⚠️  No PSRAM detected, using standard configuration");
    }
#else
    LOG_INFO("🔧 InitDisplay: ⚠️  PSRAM support not compiled in");
#endif
    
    delay(100);
    tft->setRotation(1); // Landscape mode: 320x240
    tft->fillScreen(0x0000); // Pure black background for power efficiency
    
    initialized = true;
    LOG_INFO("🔧 InitDisplay: LovyanGFX initialized with 80MHz SPI, DMA, and PSRAM support");
    return true;
}

void InitDisplay::cleanup() {

    if (tft) {
        LOG_INFO("🔧 InitDisplay: Starting enhanced display shutdown sequence");
        
        // Phase 1: Proper ST7789 Controller Shutdown
        // Send sleep command to enter low power mode first
        tft->sleep();
        tft->setBrightness(0);
        
        // Send the ST7789 deep sleep command (0x10) directly for complete charge pump shutdown
        tft->writecommand(0x10); 
        delay(120); // Mandatory delay for charge pump decay
        
        // Phase 2: Latch Backlight OFF with Hold
        // We explicitly drive the pin LOW (assuming active High logic).
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, LOW);
        
        // CRITICAL: Enable RTC Pad Hold
        // This freezes the pin state at the I/O MUX level during deep sleep.
        gpio_hold_en((gpio_num_t)TFT_BL);
        
        // Phase 3: Isolate Data Lines (Prevent Parasitic Power)
        // When VCC is cut, logic High signals can feed power through ESD diodes.
        // We drive critical lines LOW.
        pinMode(TFT_CS, OUTPUT);
        digitalWrite(TFT_CS, LOW); // Drive CS low to isolate SPI
        gpio_hold_en((gpio_num_t)TFT_CS);
        
        // Also isolate other critical SPI lines if needed
        pinMode(TFT_DC, OUTPUT);
        digitalWrite(TFT_DC, LOW);
        gpio_hold_en((gpio_num_t)TFT_DC);
        
        // Phase 4: Cut External Power (Vext)
        // Heltec V3 Vext Logic: High = OFF (Active Low Enable) 
        // GPIO 36 controls external 3.3V rail
        pinMode(36, OUTPUT); // Vext pin (GPIO 36)
        digitalWrite(36, HIGH); // Turn OFF external power (active low)
        gpio_hold_en((gpio_num_t)36);
        
        delete tft;
        tft = nullptr;
        
        LOG_INFO("🔧 InitDisplay: Enhanced shutdown complete - All power paths disabled with hold");
    }
    
    initialized = false;
}