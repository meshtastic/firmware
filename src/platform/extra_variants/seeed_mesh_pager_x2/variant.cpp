#include "variant.h"
#include "Arduino.h"

#include "AudioBoard.h"
#include <Wire.h>
#include <driver/gpio.h>

// PowerButton.cpp; kept apart because firmware headers and AudioBoard.h both define GpioPin.
void startPowerButtonMonitor();

namespace
{

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

class Tca6424
{
  public:
    explicit Tca6424(uint8_t address) : address(address) {}

    bool begin(TwoWire &wire)
    {
        this->wire = &wire;

        // Adopt the live register state: the expander survives a CPU reset and still drives PWR_HOLD and the rails.
        for (uint8_t port = 0; port < 3; port++) {
            if (!readReg(TCA6424_OUTPUT_PORT0 + port, out[port]) || !readReg(TCA6424_CONFIG_PORT0 + port, config[port])) {
                return false;
            }
        }
        return true;
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

    bool readReg(uint8_t reg, uint8_t &value)
    {
        wire->beginTransmission(address);
        wire->write(reg);
        if (wire->endTransmission(false) != 0 || wire->requestFrom(address, (uint8_t)1) != 1) {
            return false;
        }
        value = wire->read();
        return true;
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
    return ioExpander.digitalWrite(pin, level) && ioExpander.pinMode(pin, true);
}

bool writeExpanderPin(uint8_t pin, bool level)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        if (ioExpander.digitalWrite(pin, level))
            return true;
        delay(2);
    }
    return false;
}

} // namespace

void earlyInitVariant()
{
    pinMode(BUTTON_PIN, INPUT);
#ifdef LGFX_PIN_BL
    gpio_hold_dis((gpio_num_t)LGFX_PIN_BL); // held low by variant_shutdown()
#endif

    // After a warm reboot (RTC_SW_CPU_RST) I2C slaves-particularly the ES8311
    // audio codec at 0x18-can be left mid-transaction, holding SDA low.  This
    // causes ESP_ERR_INVALID_STATE during the I2C scan, which leaves the I2C1
    // peripheral generating spurious interrupt requests.  When RadioLib later
    // calls gpio_install_isr_service(), it sends an IPI to CPU1; the IPI ISR
    // runs esp_intr_alloc_intrstatus_bind() and cannot acquire the interrupt-
    // controller lock because the spurious I2C ISR is competing, so it spins
    // until the interrupt WDT fires.
    //
    // Fix: send 9 SCL clock pulses (enough to clock out any stuck byte) plus a
    // STOP condition on both buses before the Wire driver initialises them.  On
    // a cold boot the SDA line is already high so the loop exits after the first
    // check and the overhead is negligible (~10 µs).
    auto recoverI2C = [](uint8_t sda, uint8_t scl) {
        pinMode(scl, OUTPUT);
        digitalWrite(scl, HIGH);
        pinMode(sda, INPUT_PULLUP);
        delayMicroseconds(5);
        for (int i = 0; i < 9; i++) {
            if (digitalRead(sda))
                break; // SDA released - slave is no longer driving the bus
            digitalWrite(scl, LOW);
            delayMicroseconds(5);
            digitalWrite(scl, HIGH);
            delayMicroseconds(5);
        }
        // STOP condition: SDA goes LOW then HIGH while SCL remains HIGH.
        digitalWrite(scl, HIGH);
        delayMicroseconds(2);
        pinMode(sda, OUTPUT);
        digitalWrite(sda, LOW);
        delayMicroseconds(5);
        digitalWrite(sda, HIGH);
        delayMicroseconds(5);
        // Release both pins so Wire.begin() can claim them as I2C.
        pinMode(scl, INPUT);
        pinMode(sda, INPUT);
    };
    recoverI2C(I2C_SDA, I2C_SCL);
    recoverI2C(I2C_SDA1, I2C_SCL1);

    Wire.begin(I2C_SDA, I2C_SCL);

    if (!ioExpander.begin(Wire)) {
        earlyInitFailed = true;
        return;
    }

    bool ok = configureOutput(EXP_PIN_PWR_HOLD, true);

    // Buttons + SD detect + IMU interrupt are inputs.
    ok &= configureInput(EXP_PIN_BUTTON_UP);
    ok &= configureInput(EXP_PIN_BUTTON_DOWN);
    ok &= configureInput(EXP_PIN_BUTTON_LEFT);
    ok &= configureInput(EXP_PIN_BUTTON_RIGHT);
    ok &= configureInput(EXP_PIN_BUTTON_CONFIRM);
    ok &= configureInput(EXP_PIN_BUTTON_RETURN);
    ok &= configureInput(EXP_PIN_BUTTON_PWR);
    ok &= configureInput(EXP_PIN_SD_DETECT);
    ok &= configureInput(EXP_PIN_IMU_INT1);

    // Stagger rail enables as in bsp_power_startup_peripherals() to limit inrush current.
    ok &= configureOutput(EXP_PIN_BAT_ADC_EN, true);
    delay(10);
    ok &= configureOutput(EXP_PIN_SEN_EN, true);
    delay(10);
    ok &= configureOutput(EXP_PIN_SD_PWR_EN, true);
    delay(100);
    ok &= configureOutput(EXP_PIN_PA_PWR_EN, true);
    delay(10);
    ok &= configureOutput(EXP_PIN_LORA_RESET, true);

    // ST7789P3 power-on and reset sequence (mirrors bsp_lcd_hard_reset in BSP demo).
    ok &= configureOutput(EXP_PIN_LCD_PWR_EN, true);
    delay(20);
    ok &= configureOutput(EXP_PIN_LCD_RST, false); // assert reset (active low)
    delay(20);
    ok &= ioExpander.digitalWrite(EXP_PIN_LCD_RST, true); // release reset
    delay(120);                                           // allow ST7789P3 to complete internal initialization

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
            // LOG_ERROR("Skipping ES8311 init because TCA6424 init/config failed");
        } else {
            // LOG_ERROR("Skipping ES8311 init because TCA6424 is not ready");
        }
    }

    // main.cpp already called Wire1.begin() during its I2C init block.  End +
    // begin here ensures the driver starts from a clean state before the codec
    // init, removing any residual error flags from the warm-reboot I2C scan.
    Wire1.end();
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
    // LOG_INFO("ES8311 audio board initialized on I2C1");

    startPowerButtonMonitor();
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

// Called by cpuDeepSleep() right before esp_deep_sleep_start(); earlyInitVariant() restores the rails on wake.
void variant_shutdown()
{
    // device-ui polls the expander on Wire from its own task; stop it so it cannot interleave with the writes below.
    TaskHandle_t tftTask = xTaskGetHandle("tft");
    if (tftTask)
        vTaskSuspend(tftTask);

    board.end();

#ifdef LGFX_PIN_BL
    ledcDetach(LGFX_PIN_BL);
    pinMode(LGFX_PIN_BL, OUTPUT);
    digitalWrite(LGFX_PIN_BL, LOW);
    gpio_hold_en((gpio_num_t)LGFX_PIN_BL);
    gpio_deep_sleep_hold_en();
#endif

    if (!expanderReady) {
        return;
    }

    // doDeepSleep() has already ended Wire and parked its pins.
    Wire.begin(I2C_SDA, I2C_SCL);

    // Drive logic lines low first so they cannot back-power a chip whose rail is about to go.
    writeExpanderPin(EXP_PIN_LCD_RST, false);
    writeExpanderPin(EXP_PIN_GNSS_SLEEP_INT, false);

    writeExpanderPin(EXP_PIN_LCD_PWR_EN, false);
    writeExpanderPin(EXP_PIN_GNSS_PWR_EN, false);
    writeExpanderPin(EXP_PIN_SD_PWR_EN, false);
    writeExpanderPin(EXP_PIN_PA_PWR_EN, false);
    writeExpanderPin(EXP_PIN_SEN_EN, false);
    writeExpanderPin(EXP_PIN_BAT_ADC_EN, false);
    // PWR_HOLD stays high so the RTC keeps time; GNSS_VRTC_EN stays on so the GNSS can hot-start after wake.

    Wire.end();
    pinMode(I2C_SDA, ANALOG);
    pinMode(I2C_SCL, ANALOG);
}
