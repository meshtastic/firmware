#include "variant.h"
#include "Arduino.h"

#ifdef SEEED_MESHPAGER_X2

#include "AudioBoard.h"
#include "DebugConfiguration.h"
#include "UptimeClock.h"
#include "concurrency/LockGuard.h"
#include "concurrency/OSThread.h"
#include "mesh/Throttle.h"
#include "sleep.h"
#include <Wire.h>
#include <driver/gpio.h>
#ifdef X2_POWER_KEY_UI
bool x2BaseScreenOn();
void x2RequestChargeMode();
static void x2PowerOff();
#endif

// PowerButton.cpp; kept apart because firmware headers and AudioBoard.h both define GpioPin.
void startPowerButtonMonitor();

namespace
{
using namespace meshpager_x2;

// TCA6424 register map
constexpr uint8_t TCA6424_INPUT_PORT0 = 0x00;
constexpr uint8_t TCA6424_OUTPUT_PORT0 = 0x04;
constexpr uint8_t TCA6424_CONFIG_PORT0 = 0x0C;

// The device-ui task polls the keypad while the main loop drives rails, and HAL locks are disabled.
concurrency::Lock expanderLock;

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

    bool readInputPort(uint8_t port, uint8_t &value) { return port < 3 && readReg(TCA6424_INPUT_PORT0 + port, value); }

  private:
    bool writeReg(uint8_t reg, uint8_t value)
    {
        concurrency::LockGuard guard(&expanderLock);
        wire->beginTransmission(address);
        wire->write(reg);
        wire->write(value);
        return wire->endTransmission() == 0;
    }

    bool readReg(uint8_t reg, uint8_t &value)
    {
        concurrency::LockGuard guard(&expanderLock);
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
bool expanderInitialized = false;
bool keypadReady = false;
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

constexpr uint32_t AUDIO_AMP_GRACE_MS = 2000;    // keeps the amp warm between back-to-back tones
constexpr uint32_t AUDIO_AMP_POWER_UP_MS = 600;  // amp rail + NS4150B start-up from a cold amp
constexpr uint32_t CODEC_IDLE_MS = 30000;        // awake idle time before the ES8311 powers down
constexpr uint32_t CODEC_BOOT_SETTLE_MS = 2000;  // the first frames after a codec init are swallowed
constexpr uint32_t CODEC_WAKE_SETTLE_MS = 500;
constexpr uint32_t AUDIO_POWER_POLL_MS = 100;

bool audioAmpOn = false;
bool audioPlaying = false;
bool audioPlayedOnce = false;
bool codecActive = false;
bool codecSuspended = false;
uint32_t audioAmpOffAt = 0; // 0 = no deadline pending
uint32_t codecIdleAt = 0;   // 0 = no deadline pending
uint32_t codecLeadInMs = 0;

CodecConfig codecConfig()
{
    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    return cfg;
}

// The ES7243E mic ADC is unused, but its power-on state draws current. A chip id of 0x32 means BMM350 instead.
void es7243ePowerDown()
{
    const uint8_t addr = ES7243E_I2C_ADDR;
    Wire1.beginTransmission(addr);
    Wire1.write(0x00);
    if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom((int)addr, 1, (int)true) != 1)
        return;
    const uint8_t id = Wire1.read();
    if (id == 0x32)
        return;
    static const uint8_t stop[][2] = {
        {0x04, 0x02}, {0x04, 0x01}, {0xF7, 0x30}, {0xF9, 0x01}, {0x16, 0xFF}, {0x17, 0x00}, {0x01, 0x38},
        {0x20, 0x00}, {0x21, 0x00}, {0x00, 0x00}, {0x00, 0x1E}, {0x01, 0x30}, {0x01, 0x00},
    };
    for (const auto &reg : stop) {
        Wire1.beginTransmission(addr);
        Wire1.write(reg[0]);
        Wire1.write(reg[1]);
        if (Wire1.endTransmission() != 0) {
            LOG_WARN("ES7243E power-down failed at reg 0x%02x", reg[0]);
            return;
        }
    }
}

void audioAmpForceOff()
{
    audioAmpOffAt = 0;
    if (audioAmpOn && writeExpanderPin(EXP_PIN_PA_PWR_EN, false))
        audioAmpOn = false;
}

void codecPowerDown()
{
    codecIdleAt = 0;
    if (!codecActive || codecSuspended)
        return;
    if (es8311_stop(CODEC_MODE_BOTH) != RESULT_OK) {
        LOG_WARN("ES8311 power-down failed");
        return;
    }
    codecSuspended = true;
    // The codec init leaves the I2S clocks high, feeding the powered-down codec; after a playback the I2S teardown leaves them low.
    if (!audioPlayedOnce) {
        pinMode(DAC_I2S_MCLK, INPUT_PULLDOWN);
        pinMode(DAC_I2S_BCK, INPUT_PULLDOWN);
        pinMode(DAC_I2S_WS, INPUT_PULLDOWN);
        pinMode(DAC_I2S_DOUT, INPUT_PULLDOWN);
    }
}

void codecResume()
{
    if (!codecSuspended)
        return;
    if (!board.begin(codecConfig())) {
        LOG_WARN("ES8311 re-init failed");
        return;
    }
    board.setVolume(75);
    codecSuspended = false;
    codecLeadInMs = CODEC_WAKE_SETTLE_MS;
}

class AudioPowerThread : public concurrency::OSThread
{
  public:
    AudioPowerThread() : OSThread("AudioPower")
    {
        preflightObserver.observe(&preflightSleep);
        lightSleepObserver.observe(&notifyLightSleep);
    }

  protected:
    int32_t runOnce() override
    {
        if (audioAmpOffAt && Throttle::deadlinePassed(audioAmpOffAt))
            audioAmpForceOff();
        if (codecIdleAt && Throttle::deadlinePassed(codecIdleAt))
            codecPowerDown();
        return (audioAmpOffAt || codecIdleAt) ? AUDIO_POWER_POLL_MS : INT32_MAX;
    }

  private:
    int preflight(void *deepSleep) { return !deepSleep && audioPlaying ? 1 : 0; }

    // The deadline ticker is frozen in light sleep, so power down now; playback re-inits the codec on demand.
    int beforeLightSleep(void *)
    {
        audioAmpForceOff();
        codecPowerDown();
        return 0;
    }

    CallbackObserver<AudioPowerThread, void *> preflightObserver{this, &AudioPowerThread::preflight};
    CallbackObserver<AudioPowerThread, void *> lightSleepObserver{this, &AudioPowerThread::beforeLightSleep};
};

AudioPowerThread *audioPowerThread = nullptr;

// Drive logic lines low before their rails go so they cannot back-power an unpowered chip.
void parkPeripherals()
{
    // device-ui polls the expander on Wire from its own task; stop it so it cannot interleave with the writes below.
    TaskHandle_t tftTask = xTaskGetHandle("tft");
    if (tftTask) {
        // Not while it holds the expander lock, or every write below would block forever.
        concurrency::LockGuard guard(&expanderLock);
        vTaskSuspend(tftTask);
    }

    if (codecActive) {
        board.end();
        codecActive = false;
    }

#ifdef LGFX_PIN_BL
    ledcDetach(LGFX_PIN_BL);
    pinMode(LGFX_PIN_BL, OUTPUT);
    digitalWrite(LGFX_PIN_BL, LOW);
#endif

    if (!expanderInitialized)
        return;

    auto shutDownPin = [](uint8_t pin) {
        if (!writeExpanderPin(pin, false))
            LOG_WARN("Failed to turn off expander pin %u", pin);
    };
    shutDownPin(EXP_PIN_LCD_RST);
    shutDownPin(EXP_PIN_GNSS_SLEEP_INT);

    shutDownPin(EXP_PIN_LCD_PWR_EN);
    shutDownPin(EXP_PIN_GNSS_PWR_EN);
    shutDownPin(EXP_PIN_SD_PWR_EN);
    shutDownPin(EXP_PIN_PA_PWR_EN);
    audioAmpOn = false;
    shutDownPin(EXP_PIN_SEN_EN);
    shutDownPin(EXP_PIN_BAT_ADC_EN);
}

} // namespace

void x2AudioAmpSet(bool on)
{
    if (!expanderInitialized)
        return;

    audioPlaying = on;
    if (!on) {
        audioAmpOffAt = Time::timerEndsAtMillis(AUDIO_AMP_GRACE_MS);
        codecIdleAt = Time::timerEndsAtMillis(CODEC_IDLE_MS);
        if (audioPowerThread)
            audioPowerThread->setIntervalFromNow(AUDIO_POWER_POLL_MS);
        return;
    }

    audioPlayedOnce = true;
    audioAmpOffAt = 0;
    codecIdleAt = 0;
    codecResume(); // codec before the amp, so the amp never amplifies the init transients
    if (!audioAmpOn) {
        audioAmpOn = writeExpanderPin(EXP_PIN_PA_PWR_EN, true);
        if (audioAmpOn) {
            // Streamed as silence by AudioThread, so the codec is clocked while the amp comes up.
            if (codecLeadInMs < AUDIO_AMP_POWER_UP_MS)
                codecLeadInMs = AUDIO_AMP_POWER_UP_MS;
        } else {
            LOG_WARN("Speaker amp enable failed");
        }
    }
}

uint32_t x2TakeCodecLeadInMs()
{
    const uint32_t ms = codecLeadInMs;
    codecLeadInMs = 0;
    return ms;
}

// device-ui's keypad driver reads through here so its tft task shares expanderLock with the main loop.
bool directionalPadReadPort(uint8_t port, uint8_t &value)
{
    return expanderInitialized && ioExpander.readInputPort(port, value);
}

bool readKeypadState(uint8_t &pressedKeys)
{
    uint8_t portState = 0;
    if (!keypadReady || !ioExpander.readInputPort(0, portState))
        return false;

    pressedKeys = static_cast<uint8_t>(~portState) & 0x3F;
#ifdef INPUTDRIVER_DIRECTIONALPAD_RETURN_GPIO
    pressedKeys &= ~(1U << EXP_PIN_BUTTON_RETURN);
    if (digitalRead(INPUTDRIVER_DIRECTIONALPAD_RETURN_GPIO) == LOW)
        pressedKeys |= 1U << EXP_PIN_BUTTON_RETURN;
#endif
#ifdef X2_POWER_KEY_UI
    static bool waitingForRelease = false;
    if (!x2BaseScreenOn())
        waitingForRelease = true;
    if (waitingForRelease) {
        waitingForRelease = pressedKeys != 0;
        pressedKeys = 0;
    }
#endif
    return true;
}

void earlyInitVariant()
{
    // A software reset keeps the level wake type the charge screen or light sleep armed; pinMode() would re-enable it.
    gpio_wakeup_disable((gpio_num_t)BUTTON_PIN);
    gpio_set_intr_type((gpio_num_t)BUTTON_PIN, GPIO_INTR_DISABLE);
    pinMode(BUTTON_PIN, INPUT);
#ifdef INPUTDRIVER_DIRECTIONALPAD_RETURN_GPIO
    pinMode(INPUTDRIVER_DIRECTIONALPAD_RETURN_GPIO, INPUT); // external pull-up
#endif
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
    expanderInitialized = true;

#ifdef X2_POWER_KEY_UI
    if (x2ChargeModeRequested()) {
        if (configureInput(EXP_PIN_BUTTON_PWR))
            x2RunChargeMode([](uint8_t port, uint8_t &value) { return ioExpander.readInputPort(port, value); }, configureOutput);
    }
    variantPowerOff = x2PowerOff;
#endif
    bool ok = configureOutput(EXP_PIN_PWR_HOLD, true);

    // Buttons + SD detect + IMU interrupt are inputs.
    bool keypadOk = true;
    keypadOk &= configureInput(EXP_PIN_BUTTON_UP);
    keypadOk &= configureInput(EXP_PIN_BUTTON_DOWN);
    keypadOk &= configureInput(EXP_PIN_BUTTON_LEFT);
    keypadOk &= configureInput(EXP_PIN_BUTTON_RIGHT);
    keypadOk &= configureInput(EXP_PIN_BUTTON_CONFIRM);
#ifdef INPUTDRIVER_DIRECTIONALPAD_RETURN_GPIO
    // P05 is unconnected on this revision: drive it LOW so it neither floats nor lets PORT0 read 0xFF.
    keypadOk &= configureOutput(EXP_PIN_BUTTON_RETURN, false);
#else
    keypadOk &= configureInput(EXP_PIN_BUTTON_RETURN);
#endif
    ok &= keypadOk;
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
    ok &= configureOutput(EXP_PIN_PA_PWR_EN, false); // powered per playback by x2AudioAmpSet()
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

    keypadReady = keypadOk;
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

    codecActive = board.begin(codecConfig());
    board.setVolume(75);
    codecLeadInMs = CODEC_BOOT_SETTLE_MS;
    codecIdleAt = Time::timerEndsAtMillis(CODEC_IDLE_MS);
    es7243ePowerDown();
    audioPowerThread = new AudioPowerThread();
    // LOG_INFO("ES8311 audio board initialized on I2C1");

    startPowerButtonMonitor();
}

extern "C" bool variant_pre_radio_reset(void)
{
    if (!expanderInitialized || !configureOutput(EXP_PIN_LORA_RESET, true)) {
        return false;
    }

    // Reapply output mode before each pulse in case the startup write failed.
    const bool resetAsserted = ioExpander.digitalWrite(EXP_PIN_LORA_RESET, false);
    delay(10);
    const bool resetReleased = ioExpander.digitalWrite(EXP_PIN_LORA_RESET, true);
    delay(10);
    return resetAsserted && resetReleased;
}

// Called by cpuDeepSleep() right before esp_deep_sleep_start(); earlyInitVariant() restores the rails on wake.
void variant_shutdown()
{
    // doDeepSleep() has already ended Wire and parked its pins.
    if (expanderInitialized)
        Wire.begin(I2C_SDA, I2C_SCL);
    parkPeripherals();
    // PWR_HOLD stays high so the RTC keeps time; GNSS_VRTC_EN stays on so the GNSS can hot-start after wake.
#ifdef LGFX_PIN_BL
    gpio_hold_en((gpio_num_t)LGFX_PIN_BL);
    gpio_deep_sleep_hold_en();
#endif
    if (expanderInitialized) {
        Wire.end();
        pinMode(I2C_SDA, ANALOG);
        pinMode(I2C_SCL, ANALOG);
    }
}

#ifdef X2_POWER_KEY_UI
// Called by doDeepSleep() for a shutdown, before the radio is slept: on battery the board dies here,
// with USB holding the rails up it restarts into the charge screen.
static void x2PowerOff()
{
    if (!expanderInitialized)
        return;
    parkPeripherals();
    if (!writeExpanderPin(EXP_PIN_PWR_HOLD, false)) {
        LOG_WARN("X2 power latch release failed");
        return;
    }
    delay(50);
    x2RequestChargeMode();
}
#endif

#endif