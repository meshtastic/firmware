/*
 * variant.cpp - Digital pin mapping for TRACKER L1
 *
 * This file defines the pin mapping array that maps logical digital pins (D0-D17)
 * to physical GPIO ports/pins on the Nordic nRF52 series microcontroller.
 *
 * Board: [Seeed Studio WIO TRACKER L1]
 * Hardware Features:
 *  - LoRa module (CS/SCK/MISO/MOSI control pins)
 *  - GNSS module (TX/RX/Reset/Wakeup)
 *  - User LEDs (D11-D12)
 *  - User button (D13)
 *  - Grove/NFC interface (D14-D15)
 *  - Battery voltage monitoring (D16)
 *
 * Created [20250521]
 * By [Dylan]
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
 */

extern "C" {
const uint32_t g_ADigitalPinMap[] = {
    // D0 .. D10 - Peripheral control pins
    41, // D0  P1.09    GNSS_WAKEUP
    7,  // D1  P0.07     LORA_DIO1
    39, // D2  P1,07     LORA_RESET
    42, // D3  P1.10     LORA_BUSY
    46, // D4  P1.14 (A4/SDA) LORA_CS
    40, // D5  P1.08 (A5/SCL) LORA_SW
    27, // D6  P0.27 (UART_TX) GNSS_TX
    26, // D7  P0.26 (UART_RX) GNSS_RX
    30, // D8  P0.30 (SPI_SCK) LORA_SCK
    3,  // D9  P0.3 (SPI_MISO) LORA_MISO
    28, // D10 P0.28 (SPI_MOSI) LORA_MOSI

    // D11-D12 - LED outputs
    33, // D11 P1.1 User LED
    // Buzzzer
    32, // D12 P1.0 Buzzer

    // D13 - User input
    8, // D13 P0.08 User Button

    // D14-D15 - Grove interface
    6, // D14 P0.06 OLED SDA
    5, // D15 P0.05 OLED SCL

    // D16 - Battery voltage ADC input
    31, // D16 P0.31 VBAT_ADC
    // GROVE
    43, // D17 P0.00 GROVESDA
    44, // D18 P0.01 GROVESCL

    // FLASH
    21, // D19 P0.21 (QSPI_SCK)
    25, // D20 P0.25 (QSPI_CSN)
    20, // D21 P0.20 (QSPI_SIO_0 DI)
    24, // D22 P0.24 (QSPI_SIO_1 DO)
    22, // D23 P0.22 (QSPI_SIO_2 WP)
    23, // D24 P0.23 (QSPI_SIO_3 HOLD)

    36, // D25 TB_UP
    12, // D26 TB_DOWN
    11, // D27 TB_LEFT
    35, // D28 TB_RIGHT
    37, // D29 TB_PRESS
    4,  // D30 BAT_CTL
};
}

void initVariant()
{
    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);
    // This setup is crucial for ensuring low power consumption and proper initialization of the hardware components.
    // VBAT_ENABLE
    pinMode(BAT_READ, OUTPUT);
    digitalWrite(BAT_READ, HIGH);

    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
    pinMode(PIN_LED2, OUTPUT);
    digitalWrite(PIN_LED2, LOW);
    pinMode(PIN_LED2, OUTPUT);
}