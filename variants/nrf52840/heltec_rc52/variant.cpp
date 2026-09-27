#include "variant.h"
#include "Arduino.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // P0 - pins 0 and 1 are hardwired for xtal and should never be enabled
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, LED_STATE_OFF);

    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
    pinMode(PIN_GPS_RESET, OUTPUT);
    digitalWrite(PIN_GPS_RESET, !GPS_RESET_MODE);
    pinMode(PIN_GPS_PPS, INPUT);

    pinMode(SENSOR_POWER_CTRL_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_CTRL_PIN, SENSOR_POWER_ON);
    pinMode(SENSOR_RST, OUTPUT);
    digitalWrite(SENSOR_RST, HIGH);
    pinMode(SENSOR_INT, INPUT);

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}

void variant_shutdown()
{
    detachInterrupt(PIN_BUTTON1);
    detachInterrupt(PIN_GPS_PPS);
    detachInterrupt(SENSOR_INT);

    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);

    pinMode(SENSOR_POWER_CTRL_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_CTRL_PIN, !SENSOR_POWER_ON);

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
    pinMode(ADC_CTRL, OUTPUT);
    digitalWrite(ADC_CTRL, !ADC_CTRL_ENABLED);
    pinMode(RADIOCORE_FEM_EN, OUTPUT);
    digitalWrite(RADIOCORE_FEM_EN, LOW);
    pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
    digitalWrite(RADIOCORE_VFEM_CTRL, LOW);

    pinMode(PIN_WIRE_SDA, OUTPUT);
    digitalWrite(PIN_WIRE_SDA, LOW);
    pinMode(PIN_WIRE_SCL, OUTPUT);
    digitalWrite(PIN_WIRE_SCL, LOW);
    nrf_gpio_cfg_default(PIN_WIRE_SDA);
    nrf_gpio_cfg_default(PIN_WIRE_SCL);

    nrf_gpio_cfg_default(PIN_GPS_PPS);
    nrf_gpio_cfg_default(GPS_RX_PIN);
    nrf_gpio_cfg_default(GPS_TX_PIN);
    nrf_gpio_cfg_default(PIN_GPS_RESET);
    nrf_gpio_cfg_default(SENSOR_POWER_CTRL_PIN);
    nrf_gpio_cfg_default(SENSOR_RST);
    nrf_gpio_cfg_default(SENSOR_INT);

    nrf_gpio_cfg_default(SX126X_DIO1);
    nrf_gpio_cfg_default(SX126X_BUSY);
    nrf_gpio_cfg_default(SX126X_RESET);
    nrf_gpio_cfg_default(PIN_SPI_MISO);
    nrf_gpio_cfg_default(PIN_SPI_MOSI);
    nrf_gpio_cfg_default(PIN_SPI_SCK);
    nrf_gpio_cfg_default(RADIOCORE_FEM_EN);
    nrf_gpio_cfg_default(RADIOCORE_VFEM_CTRL);

    pinMode(TFT_EN, OUTPUT);
    digitalWrite(TFT_EN, HIGH);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, LOW);
    pinMode(TFT_RS, OUTPUT);
    digitalWrite(TFT_RS, LOW);
    pinMode(TFT_SCL, OUTPUT);
    digitalWrite(TFT_SCL, LOW);
    pinMode(TFT_SDA, OUTPUT);
    digitalWrite(TFT_SDA, LOW);
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);
    nrf_gpio_cfg_default(TFT_EN);
    nrf_gpio_cfg_default(TFT_BL);
    nrf_gpio_cfg_default(TFT_CS);
    nrf_gpio_cfg_default(TFT_RS);
    nrf_gpio_cfg_default(TFT_SCL);
    nrf_gpio_cfg_default(TFT_SDA);
    nrf_gpio_cfg_default(TFT_RST);
}