/* 
 * variant.cpp - Digital pin mapping for nRF52-based development board
 * 
 * This file defines the pin mapping array that maps logical digital pins (D0-D17)
 * to physical GPIO ports/pins on the Nordic nRF52 series microcontroller.
 * 
 * Board: [Seeed Studio XIAO nRF52840 Sense (Seeed Solar Node)] 
 * Hardware Features:
 *  - LoRa module (CS/SCK/MISO/MOSI control pins)
 *  - GNSS module (TX/RX/Reset/Wakeup)
 *  - User LEDs (D11-D12)
 *  - User button (D13)
 *  - Grove/NFC interface (D14-D15)
 *  - Battery voltage monitoring (D16)
 * 
 * Created [20250225]
 * By [Dylan]
 * Version 1.0
 * License: [MIT]
 */

 #include "variant.h"
 #include "nrf.h"
 #include "wiring_constants.h"
 #include "wiring_digital.h"
 
 /**
  * @brief Digital pin to GPIO port/pin mapping table
  * 
  * Format: Logical Pin (Dx) -> nRF Port.Pin (Px.xx)
  * 
  * Pin Groupings:
  * [D0-D10]  - Peripheral control (LoRa, GNSS)
  * [D11-D12] - LED outputs
  * [D13]     - User button
  * [D14-D15] - Grove/NFC interface
  * [D16]     - Battery voltage ADC input
  * [D17]     - GNSS module reset
  */

  extern "C" {
 const uint32_t g_ADigitalPinMap[] = {
     // D0 .. D10 - Peripheral control pins
     2,  // D0  P0.02 (A0)    GNSS_WAKEUP
     3,  // D1  P0.03 (A1)    LORA_DIO1
     28, // D2  P0.28 (A2)    LORA_RESET
     29, // D3  P0.29 (A3)    LORA_BUSY
     4,  // D4  P0.04 (A4/SDA) LORA_CS
     5,  // D5  P0.05 (A5/SCL) LORA_SW
     43, // D6  P1.11 (UART_TX) GNSS_TX
     44, // D7  P1.12 (UART_RX) GNSS_RX
     45, // D8  P1.13 (SPI_SCK) LORA_SCK
     46, // D9  P1.14 (SPI_MISO) LORA_MISO
     47, // D10 P1.15 (SPI_MOSI) LORA_MOSI
 
     // D11-D12 - LED outputs
     15, // D11 P0.15 User LED
     19, // D12 P0.19 Breathing LED
 
     // D13 - User input
     33, // D13 P1.01 User Button
 
     // D14-D15 - Grove/NFC interface
     9,  // D14 P0.09 NFC1/GROVE_D1
     10, // D15 P0.10 NFC2/GROVE_D0
 
     // D16 - Power management
     31, // D16 P0.31 VBAT_ADC (Battery voltage)
 
     // D17 - GNSS control
     35  // D17 P1.03 GNSS_RESET
 };
}
 void initVariant()
{
    // VBAT_ENABLE
    pinMode(VBAT_ENABLE, OUTPUT);
    digitalWrite(VBAT_ENABLE, LOW);
    
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
    pinMode(PIN_LED2, OUTPUT);
    digitalWrite(PIN_LED2, LOW);
    pinMode(PIN_LED2, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
    // pinMode(PIN_WIRE_SCL, INPUT_PULLUP);

}
