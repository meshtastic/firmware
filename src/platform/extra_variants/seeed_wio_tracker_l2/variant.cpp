#include "configuration.h"

#ifdef SEEED_WIO_TRACKER_L2

#include "AudioBoard.h"
#include "DebugConfiguration.h"
#include "SPILock.h"
#include "WakeKey.h"
#include "driver/gpio.h"
#include "input/InputBroker.h"

#include PCA95X5_INC
extern PCA95X5_CLS io;

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

static bool initOK = false;

void earlyInitVariant()
{
    gpio_intr_disable((gpio_num_t)LORA_DIO1);
    gpio_wakeup_disable((gpio_num_t)LORA_DIO1);
    gpio_set_intr_type((gpio_num_t)LORA_DIO1, GPIO_INTR_DISABLE);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // Pin main I2C0 bus to 100 kHz (TCA9535, ADS1115, AW35615, ES8311 share this Wire)
    if (io.begin(Wire, BOARD_PCA9535_ADDR, I2C_SDA, I2C_SCL)) {
        io.pinMode(EXPANDS_BTN_WAKE_UP, INPUT); // wakeup button
        io.pinMode(EXPANDS_I2C_0_INT, INPUT);   // I2C IRQ
        io.pinMode(EXPANDS_SD_DETECT, INPUT);   // SD detect

        io.pinMode(EXPANDS_EXP_OTG_EN, OUTPUT);   // OTG EN
        io.digitalWrite(EXPANDS_EXP_OTG_EN, LOW); // OTG EN low
        delay(10);
        io.pinMode(EXPANDS_PA_PWR_EN, OUTPUT);   // PA EN
        io.digitalWrite(EXPANDS_PA_PWR_EN, LOW); // PA EN low, controlled by AudioThread
        delay(10);
        io.pinMode(EXPANDS_GNSS_PWR_EN, OUTPUT);    // GNSS EN
        io.digitalWrite(EXPANDS_GNSS_PWR_EN, HIGH); // GNSS EN high
        delay(10);
        io.pinMode(EXPANDS_SD_PWR_EN, OUTPUT);    // TF EN
        io.digitalWrite(EXPANDS_SD_PWR_EN, HIGH); // TF EN high
        delay(10);
        io.pinMode(EXPANDS_BAT_ADC_EN, OUTPUT);    // BAT ADC EN
        io.digitalWrite(EXPANDS_BAT_ADC_EN, HIGH); // BAT ADC EN high
        delay(10);
        io.pinMode(EXPANDS_GNSS_RST, OUTPUT); // GNSS RST (active HIGH on this board)
        // Expander output defaults to HIGH, so module is already in reset.
        // Hold reset for 10ms, then release LOW so the module starts running.
        delay(10);
        io.digitalWrite(EXPANDS_GNSS_RST, LOW); // release reset - module starts running
        io.pinMode(EXPANDS_LED_USER, OUTPUT);   // User LED
        io.digitalWrite(EXPANDS_LED_USER, LOW); // User LED
        delay(10);
        io.pinMode(EXPANDS_GROVE_PWR_EN, OUTPUT);    // GROVE EN
        io.digitalWrite(EXPANDS_GROVE_PWR_EN, HIGH); // GROVE EN high
        delay(10);

        io.pinMode(EXPANDS_LCD_PWR_EN, OUTPUT);    // LCD EN
        io.digitalWrite(EXPANDS_LCD_PWR_EN, HIGH); // LCD EN high
        delay(50);
        io.pinMode(EXPANDS_LCD_RST, OUTPUT);    // LCD RST
        io.digitalWrite(EXPANDS_LCD_RST, HIGH); // LCD RST high
        delay(5);
        io.digitalWrite(EXPANDS_LCD_RST, LOW); // LCD RST low
        delay(10);
        io.digitalWrite(EXPANDS_LCD_RST, HIGH); // LCD RST high
        delay(500);
        io.pinMode(EXPANDS_LCD_CS, OUTPUT);    // LCD CS
        io.digitalWrite(EXPANDS_LCD_CS, HIGH); // LCD CS high
        delay(10);

        io.pinMode(EXPANDS_TP_RST, OUTPUT);   // TP RST
        io.digitalWrite(EXPANDS_TP_RST, LOW); // TP RST low
        io.pinMode(EXPANDS_TP_INT, OUTPUT);   // TP INT: disable (we use wake button for wakeup)
        io.digitalWrite(EXPANDS_TP_INT, LOW); // TP INT low
        delay(10);
        io.digitalWrite(EXPANDS_TP_INT, LOW); // TP INT low
        delay(1);
        io.digitalWrite(EXPANDS_TP_RST, HIGH); // TP RST high
        delay(60);
        initOK = true;
    }
}

void lateInitVariant()
{
    if (!initOK) {
        LOG_ERROR("PCA9555 initialization failed");
        return;
    }

    // wake button initialization
    WakeKeyInterruptThread::instance()->begin();

    // AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Debug);
    // I2C: function, scl, sda
    PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire);
    // I2S: function, mclk, bck, ws, data_out, data_in
    PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    // configure codec
    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    board.begin(cfg);
    board.setVolume(75); // 75% volume
    LOG_INFO("ES8311 Audio board initialized");
}

static void lp5814Write(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(LP5814_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

// The LP5814 backlight and the PCA9555 rails are externally powered and keep their state through deep sleep.
void variant_shutdown()
{
    // doDeepSleep() has already released the I2C bus
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    for (uint8_t i = 0; i < 4; i++)
        lp5814Write(0x18 + i, 0); // LED0..3 PWM
    lp5814Write(0x02, 0x00);      // disable all channels
    lp5814Write(0x0F, 0x55);      // latch
    lp5814Write(0x00, 0x00);      // chip standby

    if (initOK) {
        // Turn off Port 1 peripherals (GNSS, SD, PA) first, then Port 0 (LCD/touch) last.
        // Once the LCD rail drops, the unpowered touch controller clamps SDA/SCL.
        // Starting at register 0x03 writes Port 1, then decrements to Port 0.
        Wire.beginTransmission(BOARD_PCA9535_ADDR);
        Wire.write(0x03); // output port 1, then wraps to port 0
        Wire.write(0x00);
        Wire.write(0x00);
        Wire.endTransmission();
    }

    Wire.end();
    pinMode(I2C_SDA, ANALOG);
    pinMode(I2C_SCL, ANALOG);
}

#endif