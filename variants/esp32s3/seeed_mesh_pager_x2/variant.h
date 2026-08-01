#pragma once

/**
 * SenseCAP MeshPager X2 - ESP32-S3 LoRa/GNSS/Audio Pager
 *
 * Target: ESP32-S3
 * Display: ST7789P3 LCD 240×320 RGB565 (SPI)
 * Radio: LR20xx wideband (LR1120/LR1121)
 * Audio: ES8311 DAC + ES7243E ADC
 * GNSS:  AG3335MV/B UART
 * Storage: SD card (1-bit SDIO mode)
 * IO Expander: TCA6424
 */

// Buttons - via IO Expander (TCA6424)
#define BUTTON_UP GPIO_NUM_NC      // EXP pin 0, active low
#define BUTTON_DOWN GPIO_NUM_NC    // EXP pin 1, active low
#define BUTTON_LEFT GPIO_NUM_NC    // EXP pin 2, active low
#define BUTTON_RIGHT GPIO_NUM_NC   // EXP pin 3, active low
#define BUTTON_CONFIRM GPIO_NUM_NC // EXP pin 4, active low
#define BUTTON_RETURN GPIO_NUM_NC  // EXP pin 5, active low
#define BUTTON_PIN 9               // Power/wake button (GPIO 9), active high

// I2C Buses
#define I2C_SDA 17 // I2C0: SDA (IO Expander, RTC)
#define I2C_SCL 18 // I2C0: SCL

#define I2C_SDA1 47 // I2C1: SDA (Sensors, Audio codecs)
#define I2C_SCL1 48 // I2C1: SCL

// I2C0 devices: TCA6424 IO expander, YSN8900E RTC
// I2C1 devices: ES8311, ES7243E, SHT4X, LSM6DSO, BMM350

// IO Expander (TCA6424)
#define HAS_IO_EXPANDER
#define IO_EXPANDER_I2C_PORT 0
#define IO_EXPANDER_I2C_ADDR 0x22
#define IO_EXPANDER_INT 2 // GPIO 2 interrupt from expander

// Battery & Power
#define BATTERY_PIN 1              // GPIO 1, ADC for VBAT with 2:1 divider
#define ADC_CHANNEL ADC_CHANNEL_0  // ADC1_GPIO1_CHANNEL
#define ADC_MULTIPLIER 2.0 * 1.045 // 2.0 divider + 4.5% correction
#define ADC_ATTEN ADC_ATTEN_DB_12

#define USE_POWERSAVE
#define SLEEP_TIME 120

#if 0
#define TFT_CS 10    // Chip select (GPIO 10, SPI2)
#define TFT_SDA 11   // MOSI (GPIO 11)
#define TFT_SCK 12   // Clock (GPIO 12)
#define TFT_MISO -1  // Not used for display
#define TFT_A0 13    // DC / Data-Command (GPIO 13)
#define TFT_BL 46    // Backlight PWM (GPIO 46, LEDC)
#define TFT_RESET -1 // Reset via expander (EXP pin 21), managed separately

// TFT uses SPI bus (SPI2_HOST on ESP32-S3)
#define ST7789_CS TFT_CS
#define ST7789_RS TFT_A0
#define ST7789_SDA TFT_SDA
#define ST7789_SCK TFT_SCK
#define ST7789_MISO TFT_MISO
#define ST7789_RESET TFT_RESET
#define ST7789_BUSY -1
#define ST7789_BL TFT_BL
#define ST7789_SPI_HOST SPI2_HOST

//#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 320
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0

#define BRIGHTNESS_DEFAULT 130 // Medium-low brightness (0-255)
#define SCREEN_TRANSITION_FRAMERATE 5
#endif

// LoRa Radio - Semtech LR2021 (Wio-LR2021 module)
#define USE_LR2021 // Wideband radio supporting sub-GHz and 2.4 GHz

// LoRa uses SPI1 bus
#define HW_SPI1_DEVICE

#define LORA_SCK 3  // GPIO 3
#define LORA_MOSI 4 // GPIO 4
#define LORA_MISO 5 // GPIO 5
#define LORA_CS 6   // GPIO 6

#define LORA_DIO0 RADIOLIB_NC // Not connected
#define LORA_DIO1 7           // GPIO 7 (IRQ)
#define LORA_DIO2 8           // GPIO 8 (BUSY)
#define LORA_DIO3 RADIOLIB_NC // Not connected (managed via IO expander pin 22 for reset)

#define LORA_RESET RADIOLIB_NC // Reset via IO Expander (TCA6424 pin 22)

#define LR2021_IRQ_PIN LORA_DIO1
#define LR2021_NRESET_PIN LORA_RESET
#define LR2021_BUSY_PIN LORA_DIO2
#define LR2021_SPI_NSS_PIN LORA_CS
#define LR2021_SPI_SCK_PIN LORA_SCK
#define LR2021_SPI_MOSI_PIN LORA_MOSI
#define LR2021_SPI_MISO_PIN LORA_MISO

// LR2021 IRQ is on chip DIO8 (not the default DIO5).
#define LR2021_IRQ_DIO_NUM 8

// LR2021 TCXO supply voltage on DIO3 (1.8 V).
#define LR2021_DIO3_TCXO_VOLTAGE 1.8
#define LR2021_DIO_AS_RF_SWITCH

// GPS/GNSS - L76K (UART)
#define HAS_GPS 1
#define GPS_BAUDRATE 115200
#define GPS_TX_PIN 43          // UART1 TX
#define GPS_RX_PIN 44          // UART1 RX
#define GPS_THREAD_INTERVAL 50 // 50 ms update interval

// Audio - I2S with ES8311 DAC + ES7243E ADC
#define HAS_I2S
#define DAC_I2S_BCK 39  // SCLK (GPIO 39)
#define DAC_I2S_WS 40   // LRLK (GPIO 40)
#define DAC_I2S_DOUT 41 // SDOUT/Speaker (GPIO 41)
#define DAC_I2S_DIN -1  // Not used for DAC
#define DAC_I2S_MCLK 38 // MCLK (GPIO 38)

#define ADC_I2S_BCK 39  // SCLK (GPIO 39, shared with DAC)
#define ADC_I2S_WS 40   // LRLK (GPIO 40, shared with DAC)
#define ADC_I2S_DOUT -1 // Not used for ADC
#define ADC_I2S_DIN 42  // SDIN/Microphone (GPIO 42)
#define ADC_I2S_MCLK 38 // MCLK (GPIO 38, shared with DAC)

// Audio Codec Addresses
#define HAS_ES8311
#define ES8311_I2C_ADDR 0x18 // I2C1 address for ES8311

#define HAS_ES7243E
#define ES7243E_I2C_ADDR                                                                                                         \
    0x14 /* I2C1 address for ES7243E ADC                                                                                         \
            Note: May conflict with BMM350 sensor in some configs */

// Sensors - I2C1
#define HAS_BMM350  /* Magnetometer on I2C1 */
#define HAS_SHT4X   /* Temperature/Humidity on I2C1 */
#define HAS_LSM6DSO /* IMU (Accel/Gyro) on I2C1 */

// RTC - via I2C0
#define RTC_INT 45 // GPIO 45 (RTC interrupt input)

// Debug UART
#define SERIAL_TX_PIN 43 // Shared with GPS TX
#define SERIAL_RX_PIN 44 // Shared with GPS RX

// Display backlight control via LEDC
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO TFT_BL
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY 5000 // 5 kHz PWM
