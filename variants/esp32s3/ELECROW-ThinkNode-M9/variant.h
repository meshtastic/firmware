#define CANNED_MESSAGE_MODULE_ENABLE 1
#define PRESET_MESSAGE_MODULE_ENABLE 1

/*Power*/
#define VEXT_ENABLE 18
#define VEXT_ON_VALUE LOW
#define PIN_GPS_EN 11
#define GPS_EN_ACTIVE LOW

#define USE_POWERSAVE
#define SLEEP_TIME 120

/*Wire Interface*/
#define WIRE_INTERFACES_COUNT 2
// I2C keyboard
#define I2C_SCL 21
#define I2C_SDA 20
#define KB_INT 12             // STC8H key-press interrupt (idle low, rising edge on press)
#define KB_INT_WAKE_ON_HIGH 1 // KB_INT rests low; wake light sleep on its HIGH (active) level
#define KB_LED 46             // STC8H keypad backlight LED
// I2C peripheral
#define I2C_SCL1 6
#define I2C_SDA1 7

/*BUZZER*/
#define PIN_BUZZER 9

/*CHARGE_CHECK*/
#define DONE 8
#define EXT_PWR_DETECT 1
// #define EXT_CHRG_DETECT 1
#define EXT_PWR_DETECT_VALUE LOW

/*GPS*/
#define HAS_GPS 1
#define GPS_BAUDRATE 115200
#define PIN_GPS_RESET 5
#define PIN_GPS_PPS 4
#define GPS_TX_PIN 3
#define GPS_RX_PIN 2
#define GPS_THREAD_INTERVAL 50

/*SPI*/
#define SPI_MOSI 47
#define SPI_SCK 40
#define SPI_MISO 38

/*Screen*/
#define ST7789_CS 16
#define ST7789_RS 15
#define ST7789_TE 19
#define ST7789_SDA SPI_MOSI // MOSI
#define ST7789_SCK SPI_SCK
#define ST7789_RESET 14
#define ST7789_MISO SPI_MISO
#define ST7789_BUSY -1
#define ST7789_BL 17
#define ST7789_SPI_HOST SPI3_HOST
#define SPI_READ_FREQUENCY 16000000

#define USE_TFTDISPLAY 1
#define HAS_SPI_TFT 1
#define TFT_CS ST7789_CS
#define TFT_BL ST7789_BL
#define TFT_HEIGHT 320
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0
#define TFT_PWM_FREQ 44000
#define TFT_PWM_CHANNEL 7
#define TFT_INVERT_LIGHT true
#define TFT_BACKLIGHT_ON LOW
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 10
#define BRIGHTNESS_DEFAULT 128

/*Lora radio*/
#define HW_SPI1_DEVICE
#define LORA_SCK SPI_SCK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI
#define LORA_CS 39
#define LORA_RESET 45
#define LORA_DIO0 41
#define LORA_DIO1 42

#define USE_LR1110
#define LR1110_IRQ_PIN LORA_DIO1
#define LR1110_NRESET_PIN LORA_RESET
#define LR1110_BUSY_PIN LORA_DIO0
#define LR1110_SPI_NSS_PIN LORA_CS
#define LR1110_SPI_SCK_PIN LORA_SCK
#define LR1110_SPI_MOSI_PIN LORA_MOSI
#define LR1110_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 3.3
#define LR11X0_DIO_AS_RF_SWITCH

/*RTC*/
#define PCF8563_RTC 0x51

/*BATTERY*/
#define BATTERY_PIN 13
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER 2.0f
#define BAT_MEASURE_ADC_UNIT ADC_UNIT_2
#define ADC_CHANNEL ADC_CHANNEL_2
#define OCV_ARRAY 4200, 4080, 3980, 3920, 3870, 3820, 3790, 3750, 3700, 3600, 3100
