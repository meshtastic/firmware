/*
 * Digital pin mapping (logical Dx to nRF Port.Pin) and initVariant() for the
 * Seeed Wio Tracker L1 Pro 1W.
 */

#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

/**
 * @brief Digital pin to GPIO port/pin mapping table
 *
 * Format: Logical Pin (Dx) -> nRF Port.Pin (Px.xx)
 */
extern "C" {
const uint32_t g_ADigitalPinMap[] = {
    // D0 .. D10 - Peripheral control pins
    41, // D0  P1.09    GNSS_WAKEUP
    7,  // D1  P0.07     LORA_DIO1
    39, // D2  P1.07     LORA_RESET
    42, // D3  P1.10     LORA_BUSY
    46, // D4  P1.14     LORA_CS
    29, // D5  P0.29 (AIN5) LORA_VDET, Pro 1W uses P0.29 not P1.08
    27, // D6  P0.27     GNSS_TX
    26, // D7  P0.26     GNSS_RX
    30, // D8  P0.30     SPI_SCK
    3,  // D9  P0.03     SPI_MISO
    28, // D10 P0.28     SPI_MOSI

    // D11-D12 - LED outputs / Buzzer
    33, // D11 P1.01     Mesh_LED (orange), Pro 1W uses P1.01 not P1.15
    32, // D12 P1.00     Buzzer, shared with the LED_BLUE macro alias

    // D13 - User input
    8, // D13 P0.08     User Button

    // D14-D15 - OLED I2C0
    6, // D14 P0.06     OLED SDA
    5, // D15 P0.05     OLED SCL

    // D16 - Battery voltage ADC
    31, // D16 P0.31     VBAT_ADC

    // D17-D18 - Grove I2C1
    43, // D17 P1.11     GROVE SCL
    44, // D18 P1.12     GROVE SDA

    // D19-D24 - QSPI Flash
    21, // D19 P0.21     QSPI_SCK
    25, // D20 P0.25     QSPI_CSN
    20, // D21 P0.20     QSPI_SIO_0
    24, // D22 P0.24     QSPI_SIO_1
    22, // D23 P0.22     QSPI_SIO_2
    23, // D24 P0.23     QSPI_SIO_3

    // D25-D29 - Trackball
    36, // D25          TB_UP
    12, // D26          TB_DOWN
    11, // D27          TB_LEFT
    35, // D28          TB_RIGHT
    37, // D29          TB_PRESS

    // D30 - Battery divider enable
    4, // D30 P0.04     BAT_CTL

    // D31-D33 - Pro 1W only
    13, // D31 P0.13     BOOST_EN (Grove 5V Boost)
    47, // D32 P1.15     nRF_Sig_Charge_State (BQ25616 STAT)
    14, // D33 P0.14     LORA_PWR_EN (SX1262 + 1 W PA LDO)
};
}

void initVariant()
{
    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);

    // Enable battery divider for ADC sampling
    pinMode(BAT_READ, OUTPUT);
    digitalWrite(BAT_READ, HIGH);

    // Grove 5V Boost: default OFF to save power at boot / shipping state.
    // Apps that need Grove 5V can re-enable by writing BOOST_EN_ACTIVE to PIN_BOOST_EN.
    pinMode(PIN_BOOST_EN, OUTPUT);
    digitalWrite(PIN_BOOST_EN, !BOOST_EN_ACTIVE);

    // LED: default off
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
    // PIN_LED2 (D12) shares the buzzer pin; ExternalNotification configures it.
    // Forcing it LOW here would prevent PWM output.
}
