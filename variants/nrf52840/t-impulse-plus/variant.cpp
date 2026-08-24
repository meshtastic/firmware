/*
 * variant.cpp - Digital pin mapping for LilyGo T-Impulse Plus
 *
 * Board: T-Impulse Plus V1.0 (nRF52840)
 * Hardware:
 * - SSD1315 OLED
 * - SX1262 (S62F)
 * - MIA-M10Q GPS
 * - ICM20948 IMU
 * - ZD25WQ32C Flash
 * - TTP223 Touch Button
 * - Vibration Motor
 */

#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

extern "C" {
const uint32_t g_ADigitalPinMap[] = {
    // D0-D6: LoRa SX1262 (S62F module) SPI + control
    2,  // D0  P0.02  SX1262_RST
    29, // D1  P0.29  SX1262_DIO1
    31, // D2  P0.31  SX1262_BUSY
    46, // D3  P1.14  SX1262_CS
    3,  // D4  P0.03  SPI_SCK
    30, // D5  P0.30  SPI_MISO
    28, // D6  P0.28  SPI_MOSI

    // D7-D8: RF switch control
    45, // D7  P1.13  SX1262_RF_VC1 (TXEN)
    39, // D8  P1.07  SX1262_RF_VC2 (RXEN)

    // D9-D11: GPS (u-blox MIA-M10Q)
    44, // D9  P1.12  GPS_TX (MCU TX -> GPS RX)
    43, // D10 P1.11  GPS_RX (MCU RX <- GPS TX)
    42, // D11 P1.10  GPS_EN

    // D12-D13: Display I2C (SSD1315)
    20, // D12 P0.20  SCREEN_SDA
    15, // D13 P0.15  SCREEN_SCL

    // D14-D15: Sensor I2C (ICM20948, SGM41562)
    40, // D14 P1.08  IMU_SDA
    11, // D15 P0.11  IMU_SCL

    // D16-D17: Battery management
    5,  // D16 P0.05  BATTERY_ADC
    25, // D17 P0.25  BATTERY_MEASUREMENT_CONTROL

    // D18: Touch button (TTP223)
    36, // D18 P1.04  TTP223_KEY

    // D19: Vibration motor
    22, // D19 P0.22  VIBRATION_MOTOR

    // D20: LDO enable
    14, // D20 P0.14  RT9080_EN

    // D21-D26: Flash QSPI (ZD25WQ32C)
    12, // D21 P0.12  FLASH_CS
    4,  // D22 P0.04  FLASH_SCLK
    6,  // D23 P0.06  FLASH_IO0
    41, // D24 P1.09  FLASH_IO1
    8,  // D25 P0.08  FLASH_IO2
    26, // D26 P0.26  FLASH_IO3

    // D27-D28: Interrupt lines
    7,  // D27 P0.07  ICM20948_INT
    16, // D28 P0.16  SGM41562_INT

    // D29: Boot button
    24, // D29 P0.24  BOOT
};
}

void initVariant()
{
    // Flash CS high (deselect)
    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);

    // Enable battery voltage measurement
    pinMode(BAT_READ, OUTPUT);
    digitalWrite(BAT_READ, HIGH);

    // Enable RT9080 LDO
    pinMode(D20, OUTPUT);
    digitalWrite(D20, HIGH);
}