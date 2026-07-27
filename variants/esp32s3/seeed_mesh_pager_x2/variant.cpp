#include "variant.h"
#include "Arduino.h"

#include <Wire.h>
#include "AudioBoard.h"

namespace {

// TCA6424 register map
constexpr uint8_t TCA6424_INPUT_PORT0 = 0x00;
constexpr uint8_t TCA6424_OUTPUT_PORT0 = 0x04;
constexpr uint8_t TCA6424_CONFIG_PORT0 = 0x0C;

constexpr uint8_t EXP_PIN_BUTTON_UP = 0;
constexpr uint8_t EXP_PIN_BUTTON_DOWN = 1;
constexpr uint8_t EXP_PIN_BUTTON_LEFT = 2;
constexpr uint8_t EXP_PIN_BUTTON_RIGHT = 3;
constexpr uint8_t EXP_PIN_BUTTON_CONFIRM = 4;
constexpr uint8_t EXP_PIN_BUTTON_RETURN = 5;
constexpr uint8_t EXP_PIN_BUTTON_PWR = 6;
constexpr uint8_t EXP_PIN_SD_DETECT = 7;
constexpr uint8_t EXP_PIN_GNSS_RTC_INT = 8;
constexpr uint8_t EXP_PIN_GNSS_SLEEP_INT = 9;
constexpr uint8_t EXP_PIN_IMU_INT1 = 10;

constexpr uint8_t EXP_PIN_SEN_EN = 12;
constexpr uint8_t EXP_PIN_SD_PWR_EN = 13;
constexpr uint8_t EXP_PIN_LCD_PWR_EN = 14;
constexpr uint8_t EXP_PIN_GNSS_VRTC_EN = 15;
constexpr uint8_t EXP_PIN_GNSS_PWR_EN = 16;
constexpr uint8_t EXP_PIN_BAT_ADC_EN = 17;
constexpr uint8_t EXP_PIN_PA_PWR_EN = 18;
constexpr uint8_t EXP_PIN_PWR_HOLD = 20;
constexpr uint8_t EXP_PIN_LCD_RST = 21;
constexpr uint8_t EXP_PIN_LORA_RESET = 22;
constexpr uint8_t EXP_PIN_GNSS_RST = 23;

class Tca6424 {
  public:
    explicit Tca6424(uint8_t address) : address(address) {}

    bool begin(TwoWire &wire)
    {
        this->wire = &wire;

        // Default all pins as inputs and all outputs low in our local shadow.
        config[0] = 0xFF;
        config[1] = 0xFF;
        config[2] = 0xFF;
        out[0] = 0x00;
        out[1] = 0x00;
        out[2] = 0x00;

        return writeReg(TCA6424_OUTPUT_PORT0 + 0, out[0]) && writeReg(TCA6424_OUTPUT_PORT0 + 1, out[1]) &&
               writeReg(TCA6424_OUTPUT_PORT0 + 2, out[2]) && writeReg(TCA6424_CONFIG_PORT0 + 0, config[0]) &&
               writeReg(TCA6424_CONFIG_PORT0 + 1, config[1]) && writeReg(TCA6424_CONFIG_PORT0 + 2, config[2]);
    }

    bool pinMode(uint8_t pin, bool output)
    {
        const uint8_t port = pin / 8;
        const uint8_t bit = pin % 8;
        if (port > 2) {
            return false;
        }

        if (output) {
            config[port] &= ~(1U << bit); // 0 = output
        } else {
            config[port] |= (1U << bit); // 1 = input
        }
        return writeReg(TCA6424_CONFIG_PORT0 + port, config[port]);
    }

    bool digitalWrite(uint8_t pin, bool high)
    {
        const uint8_t port = pin / 8;
        const uint8_t bit = pin % 8;
        if (port > 2) {
            return false;
        }

        if (high) {
            out[port] |= (1U << bit);
        } else {
            out[port] &= ~(1U << bit);
        }
        return writeReg(TCA6424_OUTPUT_PORT0 + port, out[port]);
    }

  private:
    bool writeReg(uint8_t reg, uint8_t value)
    {
        wire->beginTransmission(address);
        wire->write(reg);
        wire->write(value);
        return wire->endTransmission() == 0;
    }

    uint8_t address;
    TwoWire *wire = nullptr;
    uint8_t config[3] = {0xFF, 0xFF, 0xFF};
    uint8_t out[3] = {0x00, 0x00, 0x00};
};

Tca6424 ioExpander(IO_EXPANDER_I2C_ADDR);
DriverPins pinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, pinsAudioBoardES8311);
bool expanderReady = false;
bool earlyInitFailed = false;

bool configureInput(uint8_t pin)
{
    return ioExpander.pinMode(pin, false);
}

bool configureOutput(uint8_t pin, bool level)
{
    return ioExpander.pinMode(pin, true) && ioExpander.digitalWrite(pin, level);
}

} // namespace

void earlyInitVariant()
{
    // Use explicit pins to avoid accidental macro drift during early startup.
    Wire.begin(17, 18);

    if (!ioExpander.begin(Wire)) {
        earlyInitFailed = true;
        return;
    }

    // Buttons + SD detect + IMU interrupt are inputs.
    bool ok = true;
    ok &= configureInput(EXP_PIN_BUTTON_UP);
    ok &= configureInput(EXP_PIN_BUTTON_DOWN);
    ok &= configureInput(EXP_PIN_BUTTON_LEFT);
    ok &= configureInput(EXP_PIN_BUTTON_RIGHT);
    ok &= configureInput(EXP_PIN_BUTTON_CONFIRM);
    ok &= configureInput(EXP_PIN_BUTTON_RETURN);
    ok &= configureInput(EXP_PIN_BUTTON_PWR);
    ok &= configureInput(EXP_PIN_SD_DETECT);
    ok &= configureInput(EXP_PIN_IMU_INT1);

    // Keep board alive and power domains in known state.
    ok &= configureOutput(EXP_PIN_PWR_HOLD, true);
    ok &= configureOutput(EXP_PIN_BAT_ADC_EN, false);
    ok &= configureOutput(EXP_PIN_SEN_EN, true);
    ok &= configureOutput(EXP_PIN_SD_PWR_EN, true);
    ok &= configureOutput(EXP_PIN_PA_PWR_EN, true);
    ok &= configureOutput(EXP_PIN_LORA_RESET, true);

    // ST7789P3 power-on and reset sequence (mirrors bsp_lcd_hard_reset in BSP demo).
    ok &= configureOutput(EXP_PIN_LCD_PWR_EN, true);
    delay(20);
    ok &= configureOutput(EXP_PIN_LCD_RST, false); // assert reset (active low)
    delay(20);
    ok &= ioExpander.digitalWrite(EXP_PIN_LCD_RST, true); // release reset
    delay(120); // allow ST7789P3 to complete internal initialization

    // Match demo LR20xx reset sequence on expander pin 22.
    ok &= ioExpander.digitalWrite(EXP_PIN_LORA_RESET, false);
    delay(10);
    ok &= ioExpander.digitalWrite(EXP_PIN_LORA_RESET, true);
    delay(10);

    // GNSS sequence from demo firmware.
    ok &= configureOutput(EXP_PIN_GNSS_PWR_EN, true);
    delay(10);
    ok &= configureOutput(EXP_PIN_GNSS_VRTC_EN, true);
    delay(10);
    ok &= configureOutput(EXP_PIN_GNSS_RST, true);
    delay(10);
    ok &= ioExpander.digitalWrite(EXP_PIN_GNSS_RST, false);
    delay(10);
    ok &= configureOutput(EXP_PIN_GNSS_SLEEP_INT, true);
    delay(10);
    ok &= configureOutput(EXP_PIN_GNSS_RTC_INT, false);

    expanderReady = ok;
    earlyInitFailed = !ok;
}

void lateInitVariant()
{
    if (!expanderReady) {
        if (earlyInitFailed) {
            //LOG_ERROR("Skipping ES8311 init because TCA6424 init/config failed");
        } else {
            //LOG_ERROR("Skipping ES8311 init because TCA6424 is not ready");
        }
        return;
    }

    Wire1.begin(I2C_SDA1, I2C_SCL1);

    // I2C: function, bus
    pinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire1);
    // I2S: function, mclk, bck, ws, data_out, data_in
    pinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;

    board.begin(cfg);
    board.setVolume(75);
    //LOG_INFO("ES8311 audio board initialized on I2C1");
}

extern "C" void meshtastic_variant_pre_radio_reset(void)
{
    if (!expanderReady) {
        return;
    }

    // Match SenseCAP demo: pulse LR20xx reset through expander right before radio init.
    ioExpander.digitalWrite(EXP_PIN_LORA_RESET, false);
    delay(10);
    ioExpander.digitalWrite(EXP_PIN_LORA_RESET, true);
    delay(10);
}
