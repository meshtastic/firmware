// Touch_LCD_2_SX1262_L76K variant.cpp
// Hardware initialization

#include "variant.h"
#include <Arduino.h>

#include "esp_log.h"

static const char *TAG = "Touch_LCD_2_SX1262_L76K";

/**
 * @brief Variant initialization function
 *
 * Called automatically by the Arduino framework at startup to initialize
 * hardware-specific pins and peripherals.
 * Note: The Arduino logging system may not be initialized at this point; use ESP_LOGI instead.
 */
void initVariant()
{
    ESP_LOGI(TAG, "Initializing Touch_LCD_2_SX1262_L76K variant");

    // Antenna control is not delegated to the SX126x library, so manual initialization is required
#if SX126X_RXEN == RADIOLIB_NC
    // Initialize LoRa antenna control pin
    // LORA_CTRL_GPIO (GPIO6) controls antenna enable, active low
    pinMode(LORA_CTRL_GPIO, OUTPUT);
    digitalWrite(LORA_CTRL_GPIO, LOW); // Set low (active)
#endif

    ESP_LOGI(TAG, "LORA_CTRL_GPIO (GPIO%d) initialized, set LOW (active)", LORA_CTRL_GPIO);
}
