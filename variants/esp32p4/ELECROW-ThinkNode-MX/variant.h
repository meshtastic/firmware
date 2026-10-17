#define I2C_SDA 46
#define I2C_SCL 45
#define I2C_SDA1 12
#define I2C_SCL1 13
#define I2C_SDA2 10 // STC8
#define I2C_SCL2 11

#define USE_POWERSAVE
#define WAKE_ON_TOUCH
#define SCREEN_TOUCH_INT 8
#define SLEEP_TIME 180

#define HAS_I2S
#define DAC_I2S_BCK 22
#define DAC_I2S_WS 21
#define DAC_I2S_DOUT 23
#define DAC_I2S_MCLK 0

// NS4168 amp
#define AUDIO_POWER_ENABLE LOW
#define AUDIO_POWER_DISABLE HIGH
#define AUDIO_AMP_CTRL 42
#define AUDIO_AMP_ENABLE(on) digitalWrite(AUDIO_AMP_CTRL, (on) ? AUDIO_POWER_ENABLE : AUDIO_POWER_DISABLE)
#define AUDIO_AMP_SETTLE_MS 0

#define PIN_TOUCH_POWER (4)               // Power enable for touch panel
#define TOUCH_POWER_ON_LEVEL LOW          // Touch panel power active level, 0: enable, 1: disable
#define PIN_TOUCH_RST (34)                // Touch controller reset, active low
#define PIN_DISPLAY_CHIP_POWER (25)       // Power enable for display chip
#define DISPLAY_CHIP_POWER_ON_LEVEL LOW   // Display chip power active level, 0: enable, 1: disable
#define PIN_DISPLAY_PANEL_POWER (5)       // Power enable for display panel
#define DISPLAY_PANEL_POWER_ON_LEVEL HIGH // Display panel power active level, 0: disable, 1: enable
#define PIN_LCD_GPIO_BLIGHT (26)          // Display backlight PWM
#define PIN_LCD_GPIO_RST (24)             // Display reset, active low

// PCF8563 RTC Module
// #define PCF8563_RTC 0x51

// #define GPS_DEFAULT_NOT_PRESENT 1
#define PIN_GPS_EN 3
#define GPS_EN_ACTIVE HIGH
#define GPS_RX_PIN 53
#define GPS_TX_PIN 54

#define LORA_SCK 33
#define LORA_MOSI 48
#define LORA_MISO 47
#define LORA_CS 30
#define LORA_DIO1 31
#define LORA_DIO0 29
#define LORA_RESET 32

// LoRa
#define USE_LR1110
#define LR1110_IRQ_PIN LORA_DIO1
#define LR1110_BUSY_PIN LORA_DIO0
#define LR1110_NRESET_PIN LORA_RESET
#define LR1110_SPI_NSS_PIN LORA_CS
#define LR1110_SPI_SCK_PIN LORA_SCK
#define LR1110_SPI_MOSI_PIN LORA_MOSI
#define LR1110_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 3.3F

#define BATTERY_PIN 20
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER 1.4F
#define BAT_MEASURE_ADC_UNIT ADC_UNIT_2
#define ADC_CHANNEL ADC_CHANNEL_2
#define OCV_ARRAY 4200, 4080, 3980, 3920, 3870, 3820, 3790, 3750, 3700, 3600, 3100
