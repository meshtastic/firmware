#include "variant.h"
#include "Arduino.h"
#include "esp_private/startup_internal.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"

// Take the battery power latch before Arduino's PSRAM init and memtest (CORE priority 99), so the power
// button only has to be held through ROM and bootloader rather than until setup()
ESP_SYSTEM_INIT_FN(nomad_power_latch, CORE, BIT(0), 10)
{
    esp_rom_gpio_pad_select_gpio(PIN_POWER_EN);
    gpio_ll_set_level(&GPIO, PIN_POWER_EN, 1);
    gpio_ll_output_enable(&GPIO, PIN_POWER_EN);
    return ESP_OK;
}

void earlyInitVariant()
{
    // LR1121 and SD card share one SPI bus; deselect both before the SD card is probed
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    // Hold the GPS in shutdown until GPS init: with VBAT kept alive across a reset it only leaves backup mode
    // on an ON/OFF rising edge, which a floating pin does not give it
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);

    // Ramp the LCD/GPS rail: switched hard, its bulk caps dip VCC3.3V and reset the CH340K.
    // The 1k/100nF RC on the switch gate averages this 20 kHz PWM into a ~100 ms soft start.
    pinMode(VEXT_ENABLE, OUTPUT);
    for (uint32_t onUs = 0; onUs <= 50; onUs++) {
        for (int i = 0; i < 40; i++) {
            digitalWrite(VEXT_ENABLE, VEXT_ON_VALUE);
            delayMicroseconds(onUs);
            digitalWrite(VEXT_ENABLE, !VEXT_ON_VALUE);
            delayMicroseconds(50 - onUs);
        }
    }
    digitalWrite(VEXT_ENABLE, VEXT_ON_VALUE);
}
