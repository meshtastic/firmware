# New Hardware Variant

Guide for adding a new Meshtastic hardware variant to the firmware.

## Directory Structure

Create under `variants/<arch>/<name>/`:

```text
variants/
├── esp32/          # ESP32
├── esp32s3/        # ESP32-S3
├── esp32c3/        # ESP32-C3
├── esp32c6/        # ESP32-C6
├── nrf52840/       # nRF52840
├── rp2040/         # RP2040/RP2350
├── stm32/          # STM32WL
└── native/         # Linux/Portduino
```

Each variant needs at minimum:

- `variant.h` — Pin definitions and hardware capabilities
- `platformio.ini` — Build configuration

Optional files:

- `pins_arduino.h` — Arduino pin mapping overrides
- `rfswitch.h` — RF switch control for multi-band radios
- `nicheGraphics.h` — InkHUD e-ink configuration

## variant.h Template

```cpp
// Pin definitions
#define I2C_SDA 21
#define I2C_SCL 22

// LoRa radio
#define USE_SX1262              // Radio chip: USE_SX1262, USE_SX1268, USE_SX1280, USE_RF95, USE_LLCC68, USE_LR1110, USE_LR1120, USE_LR1121
#define LORA_CS   18
#define LORA_SCK  5
#define LORA_MOSI 27
#define LORA_MISO 19
#define LORA_DIO1 33            // SX126x: DIO1, SX128x: DIO1, RF95: IRQ
#define LORA_RESET 23
#define LORA_BUSY 32            // SX126x/SX128x only
#define SX126X_DIO2_AS_RF_SWITCH // Common for SX1262 boards

// GPS
#define HAS_GPS 1
#define GPS_RX_PIN 34
#define GPS_TX_PIN 12
// #define PIN_GPS_EN 47         // Optional GPS enable pin
// #define GPS_BAUDRATE 9600     // Override default 9600

// Display
#define HAS_SCREEN 1
// #define USE_SSD1306            // OLED type
// #define USE_SH1106             // Alternative OLED
// #define USE_ST7789             // TFT type
// #define SCREEN_WIDTH 128
// #define SCREEN_HEIGHT 64

// LEDs
#define LED_PIN 2               // Status LED (optional)
// #define HAS_NEOPIXEL 1        // WS2812 support

// Buttons
#define BUTTON_PIN 38
// #define BUTTON_PIN_ALT 0      // Secondary button

// Power management
// #define HAS_AXP192 1          // AXP192 PMU (T-Beam v1.0)
// #define HAS_AXP2101 1         // AXP2101 PMU (T-Beam v1.2+)
// #define BATTERY_PIN 35        // ADC battery voltage pin
// #define ADC_MULTIPLIER 2.0    // Voltage divider ratio

// Optional I2C devices
// #define HAS_RTC 1             // Real-time clock
// #define HAS_TELEMETRY 1       // Enable telemetry sensor support
// #define HAS_SENSOR 1          // I2C sensors present
```

## platformio.ini Template

```ini
[env:my_variant]
extends = esp32s3_base            ; Use architecture-specific base
board = esp32-s3-devkitc-1        ; PlatformIO board definition (or custom in boards/)
board_level = extra               ; Build level: extra, or omit for default
custom_meshtastic_support_level = 1  ; 1 = PR builds, 2 = merge builds only

build_flags =
    ${esp32s3_base.build_flags}
    -D MY_VARIANT_SPECIFIC_FLAG=1
    -I variants/esp32s3/my_variant  ; Include path for variant.h

upload_speed = 921600
```

### Common Base Configs

- `esp32_base` / `esp32-common.ini` — ESP32
- `esp32s3_base` — ESP32-S3
- `esp32c3_base` — ESP32-C3
- `esp32c6_base` — ESP32-C6
- `nrf52840_base` / `nrf52.ini` — nRF52840
- `rp2040_base` — RP2040/RP2350

### Support Levels

- `custom_meshtastic_support_level = 1` — Built on every PR (actively supported)
- `custom_meshtastic_support_level = 2` — Built only on merge to main branches
- `board_level = extra` — Only built on full releases

## Build Manifest Metadata

`bin/platformio-custom.py` emits UI capability flags in the build manifest:

- `custom_meshtastic_has_mui = true/false` — Override MUI detection
- `custom_meshtastic_has_ink_hud = true/false` — Override InkHUD detection
- Architecture names are normalized (e.g., `esp32s3` → `esp32-s3`)

## InkHUD E-Ink Variants

For e-ink display variants using the InkHUD framework, add `nicheGraphics.h`:

```cpp
// nicheGraphics.h — InkHUD configuration for this variant
#define INKHUD                     // Enable InkHUD
// Configure display, applets, and refresh behavior per device
```

InkHUD has its own PlatformIO config: `src/graphics/niche/InkHUD/PlatformioConfig.ini`

## I2C Device Detection

If the variant has I2C devices, ensure `src/detect/ScanI2C` will detect them. The auto-detection system handles 80+ device types including displays, sensors, RTCs, keyboards, PMUs, and touch controllers at boot.

## Custom Board Definitions

If the PlatformIO board doesn't exist, create a custom board JSON in `boards/`:

```json
{
  "build": {
    "arduino": { "ldscript": "esp32s3_out.ld" },
    "core": "esp32",
    "f_cpu": "240000000L",
    "f_flash": "80000000L",
    "flash_mode": "qio",
    "mcu": "esp32s3",
    "variant": "esp32s3"
  },
  "connectivity": ["wifi", "bluetooth"],
  "frameworks": ["arduino", "espidf"],
  "name": "My Custom Board",
  "upload": {
    "flash_size": "8MB",
    "maximum_ram_size": 327680,
    "maximum_size": 8388608
  },
  "url": "https://example.com",
  "vendor": "MyVendor"
}
```

## Checklist

- [ ] Create `variants/<arch>/<name>/variant.h` with pin definitions
- [ ] Create `variants/<arch>/<name>/platformio.ini` extending correct base
- [ ] Set `custom_meshtastic_support_level` (1 or 2)
- [ ] Verify radio chip define matches hardware (`USE_SX1262`, etc.)
- [ ] Set hardware capability flags (`HAS_GPS`, `HAS_SCREEN`, etc.)
- [ ] Add custom board JSON in `boards/` if needed
- [ ] Test build: `pio run -e my_variant`
- [ ] For e-ink: add `nicheGraphics.h` with InkHUD config
