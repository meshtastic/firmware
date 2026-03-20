# Touch_LCD_2_SX1262_L76K ESP32-S3 Board Configuration

## Hardware Specifications

- **MCU**: ESP32-S3
- **Display**: 2-inch 240×320 ST7789 TFT color LCD
- **Touch**: CST816D capacitive touch controller
- **LoRa**: SX1262 RF LoRa module
- **GPS**: L76K module, supports GPS and BeiDou positioning

## Pin Assignments

### ST7789 TFT LCD (SPI)

- CS (Chip Select): GPIO45
- DC (Data/Command): GPIO42
- MOSI: GPIO38
- SCK: GPIO39
- MISO: not used
- RST (Reset): not used
- BL (Backlight): GPIO1

### CST816D Touchscreen (I2C)

- SDA: GPIO48
- SCL: GPIO47
- INT (Interrupt): GPIO46
- I2C Address: 0x15
- **Driver**: Uses LovyanGFX Touch_CST816S driver (CST816D is fully compatible with CST816S)
- **Note**: Configured in TFTDisplay.cpp to use lgfx::Touch_CST816S class

### SX1262 LoRa Module (SPI)

- CS (NSS): GPIO18
- MOSI: GPIO7
- SCK: GPIO8
- MISO: GPIO10
- RST: GPIO2
- DIO1 (Interrupt): GPIO17
- BUSY: GPIO4
- CTRL (Antenna Enable): GPIO6

### L76K GPS Module (UART)

- TX: GPIO9 (connected to GPS RX)
- RX: GPIO14 (connected to GPS TX)
- RST: GPIO5
- PPS: not connected
- EN: not connected

### Other Pins

- User Button: GPIO0
- LED Indicator: not connected
- Battery Monitor: GPIO5
- External Power Enable: not connected

## Build Configuration

Use the following commands to build the firmware:

```bash
pio run -e touch_lcd_2_sx1262_l76k
pio run -e touch_lcd_2_sx1262_l76k --target upload --target monitor
```

## Reference Variants

This configuration was based on the following existing variants:

- `variants/esp32s3/t-deck/` - ST7789 TFT configuration (build failed)
- `variants/esp32s3/heltec_vision_master_t190/` - ST7789 TFT configuration (build succeeded)
- `variants/esp32s3/t-watch-s3/` - ST7789 TFT configuration (runs successfully)
- `variants/esp32s3/heltec_v3/` - SX1262 LoRa configuration
- `variants/esp32s3/heltec_v4/` - L76K GPS configuration
- `variants/esp32s3/rak_wismesh_tap_v2/` - touchscreen configuration

## Notes

1. Arduino's default SPI (unit 2) is connected to LoRa, default I2C is shared, and default UART (unit 0) is connected to GPS.
2. The GPS RX/TX pins must not have pull-up resistors. Choose pins carefully. The same applies to other pins — avoid connecting anything else to prevent interference.
3. GPS requires sufficient supply voltage to operate correctly. Pay attention to power supply.
4. Tested with `lovyan03/LovyanGFX@1.2.0` — newer versions do not work; pin the version for successful compilation. The ST7789 driver library is included by default and does not need to be imported separately.
5. Adding debug features and similar options causes build failures. Even `CONFIG_ARDUHAL_LOG_COLORS` breaks the build. Do not add unnecessary configuration options.
6. The codebase has inconsistent naming — many macros with the same meaning exist. For example, `USE_ST7789` and `HAS_TFT` must not be defined; the correct macro to enable the display is `ST7789_CS`. Do not add redundant macros.
7. The `seeed-sensecap-indicator` and `t-deck` variants use a custom `bb_captouch` library with board-specific configuration baked in — they cannot be used directly.
8. For battery voltage monitoring, ensure `ADC_MULTIPLIER`, `ADC_ATTENUATION`, `BATTERY_PIN`, and `ADC_CHANNEL` are configured correctly, otherwise readings will be wrong.
9. There is an overridable initialization function `lateInitVariant()`, currently used by only three variants. Similar to `initVariant()` in `variant.cpp`, but executes after hardware has been partially initialized. The `t_deck_pro` variant overrides this for touchscreen initialization.
10. After tracing through the touch and display driver code, the configuration only needs to be set in `src\graphics\TFTDisplay.cpp`. The touch chip driver is already included in the library: `<project>\.pio\libdeps\touch_lcd_2_sx1262_l76k\LovyanGFX\src\lgfx\v1\touch\Touch_CST816S.cpp`.
11. LoRa configuration updated for broader compatibility, supporting `rxen` and `tcxo` pin functions to work with modules such as E22-900M20S.

```cpp
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;
#if HAS_TOUCHSCREEN
#if defined(T_WATCH_S3) || defined(ELECROW)
    lgfx::Touch_FT5x06 _touch_instance;
#elif defined(TOUCH_LCD_2_SX1262_L76K)
    lgfx::Touch_CST816S _touch_instance;  // CST816D is compatible with CST816S driver
#else
    lgfx::Touch_GT911 _touch_instance;
#endif
#endif
}
// ...
```

## Features

- ✅ 2-inch color TFT display
- ✅ Capacitive touch input
- ✅ LoRa mesh networking
- ✅ GPS / BeiDou positioning
- ✅ Battery voltage monitoring
- ✅ Power saving mode
- ✅ User button interaction
