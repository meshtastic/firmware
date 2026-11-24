/*
Seeed SenseCAP S2100 data logger (Wio-E5 module, STM32WLE5JC) running Meshtastic.
Polls an RS485 Modbus sensor and sends its readings as telemetry.
*/

#ifndef _VARIANT_SEEED_M2100_
#define _VARIANT_SEEED_M2100_

#define USE_STM32WLx

#define SEEED_M2100

// TODO(S2100 schematic): Wio-E5 dev board placeholders, unverified for the S2100.
// Serial2 (PA2 TX -> DI, PA3 RX <- RO) is set in platformio.ini, as the STM32 core never sees this file.
// #define MODBUS_DE_PIN        <pin> // driver enable, if the transceiver is not auto-direction
// #define MODBUS_PWR_EN_PIN    <pin> // switch for the 12 V sensor supply
// #define MODBUS_PWR_WARMUP_MS 2000
// #define BUTTON_PIN           <pin>
// #define LED_POWER            <pin>

// Client API tunnel on the RS485 bus, for provisioning without Bluetooth
#define MODBUS_API_ADDR 240

// LoRa
// https://github.com/Seeed-Studio/LoRaWan-E5-Node/blob/163c05379b1805dd8f2c061d4557a69985acc953/Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio_driver.c#L94
#define SX126X_DIO3_TCXO_VOLTAGE 1.7

#endif
