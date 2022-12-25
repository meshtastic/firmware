// This board do not have extra serial to connect GPS to
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// This board do not have easy accessible I2C pins
#undef I2C_SDA
#undef I2C_SCL

#define LED_PIN 27     // If defined we will blink this LED

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_DIO1 25 // PA pin in this module
#define LORA_RESET 14 // RST for SX1276, and for SX1262/SX1268

// In transmitting, set TXEN as high communication level，RXEN pin is low level;
// In receiving, set RXEN as high communication level, TXEN is lowlevel;
// Before powering off, set TXEN、RXEN as low level.
#define LORA_RXEN 13  // Input - RF switch RX control, connecting external MCU IO, valid in high level
#define LORA_TXEN 12  // Input - RF switch TX control, connecting external MCU IO or DIO2, valid in high level

// RFM95-specific settings
#undef RF95_SCK
#define RF95_SCK 18
#undef RF95_MISO
#define RF95_MISO 19
#undef RF95_MOSI
#define RF95_MOSI 23
#undef RF95_NSS
#define RF95_NSS 5
// RX/TX for RFM95/SX127x
#define RF95_RXEN LORA_RXEN
#define RF95_TXEN LORA_TXEN

// This module has fan
#define RF95_FAN_EN 17

// This module has PA
#define RF95_PA_EN LORA_DIO1
// Either use ESP32 DAC ability (if proper pin is set) or generic PWM one
// DAC is highly recommended over PWM
#define RF95_PA_DAC_EN
// PA level >200 gives little to no benefit meanwhile you can fry your board
// Approximate mapping of PA_LEVEL to Power output:
// 0 -> 10mW
// 41 -> 25mW
// 60 -> 50mW
// 73 -> 100mW
// 90 -> 250mW
// 110 -> 500mW
// 132 -> 1000mW
// 190 -> 2000mW
// 250 -> 3000mW // DO NOT USE, YOU WILL FRY YOUR BOARD!
#define RF95_PA_LEVEL 90
// PA PWM set up if by some reason DAC is not used (not recommended)
#define RF95_PA_PWM_CH 15  // PWM channel to use (0-15)
