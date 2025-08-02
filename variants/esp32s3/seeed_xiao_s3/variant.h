/*
MCU board information: https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html
LoRa module hardware interfacing information: https://wiki.seeedstudio.com/wio_sx1262_with_xiao_esp32s3_kit/#hardware-overview
Expansion board infomation: https://www.seeedstudio.com/Seeeduino-XIAO-Expansion-board-p-4746.html
L76K GPS module information: https://www.seeedstudio.com/L76K-GNSS-Module-for-Seeed-Studio-XIAO-p-5864.html
*/

#define LED_PIN 48 // This LED is on the PCB containing the Wio-1262 module. The LED on the XIAO ESP32S3 board is connected to the button on the Wio-SX1262 board so cannot be used.
#define LED_STATE_ON 1

#define BUTTON_PIN 21 // The PCB containing the Wio-SX1262 module includes a pull-up resistor on the button pin

#define BATTERY_PIN -1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define BATTERY_SENSE_RESOLUTION_BITS 12


#define USCREEN_SSD1306 // XIAO S3 Expansion board has 1.3 inch OLED Screen

#define I2C_SDA 5
#define I2C_SCL 6


/*Warning:
    https://www.seeedstudio.com/L76K-GNSS-Module-for-Seeed-Studio-XIAO-p-5864.html
    L76K Expansion Board can not directly used, L76K Reset Pin needs to override or physically remove it,
    otherwise it will conflict with the SPI pins
*/
#define GPS_L76K
#ifdef GPS_L76K
#define HAS_GPS 1
#define GPS_TX_PIN 43
#define GPS_RX_PIN 44
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX
#define PIN_GPS_STANDBY 1
#endif


#define SX126X_CS 41
#define LORA_SCK 7
#define LORA_MOSI 9
#define LORA_MISO 8
#define SX126X_RESET 42
#define SX126X_BUSY 40
#define SX126X_DIO1 39

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_RXEN 38

#define USE_SX1262

#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define LORA_CS SX126X_CS
#define LORA_RESET SX126X_RESET
#define LORA_DIO1 SX126X_DIO1
