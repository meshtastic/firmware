#include "configuration.h"

#ifdef SEEED_WIO_TRACKER_L2

#include "AudioBoard.h"
#include "DebugConfiguration.h"
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "input/TouchScreenImpl1.h"
#include "mesh/MeshLED.h"
#include "sleep.h"
#include "variant.h"

#include PCA95X5_INC
extern PCA95X5_CLS io;

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

// wake button handling
#if defined(BOARD_PCA9535_ADDR) && defined(BOARD_PCA9535_BUTTON_MASK)
static bool isPca9535WakeKeyPressed()
{
    return !io.digitalRead(EXPANDS_BTN_WAKE_UP);
}

class WakeKeyInterruptThread : public concurrency::OSThread
{
  public:
    WakeKeyInterruptThread() : concurrency::OSThread("WioL2WakeKeyInt", SAMPLE_MS)
    {
        // Do not run unless an edge arrives.
        OSThread::disable();
        instance = this;
#ifdef ARCH_ESP32
        lsObserver.observe(&notifyLightSleep);
        lsEndObserver.observe(&notifyLightSleepEnd);
#endif
    }

    void begin()
    {
        pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
        attachInterrupt(BOARD_PCA9535_INT, WakeKeyInterruptThread::isr, FALLING);
    }

  protected:
    int32_t runOnce() override
    {
        const uint32_t now = millis();

        // Ignore side-key handling while BOOT/user button is held.
        if (digitalRead(BUTTON_PIN) == LOW) {
            resetStateAndStop();
            return OSThread::disable();
        }

        switch (state) {
        case State::IRQ_PENDING:
            // Initial debounce after expander interrupt edge.
            if ((uint32_t)(now - irqAtMs) < DEBOUNCE_MS) {
                return SAMPLE_MS;
            }

            if (isPca9535WakeKeyPressed()) {
                LOG_DEBUG("wake button pressed");
                powerFSM.trigger(EVENT_PRESS);
                state = State::PRESSED;
                return SAMPLE_MS;
            }

            // Spurious/cleared edge.
            resetStateAndStop();
            return OSThread::disable();

        case State::PRESSED: {
            if (isPca9535WakeKeyPressed()) {
                return SAMPLE_MS;
            }

            resetStateAndStop();
            return OSThread::disable();
        }

        case State::REST:
        default:
            return OSThread::disable();
        }
    }

  private:
    enum class State : uint8_t {
        REST,
        IRQ_PENDING,
        PRESSED,
    };

    static constexpr uint32_t SAMPLE_MS = 15;
    static constexpr uint32_t DEBOUNCE_MS = 25;

    static WakeKeyInterruptThread *instance;

    static void isr()
    {
        if (instance) {
            instance->onInterruptEdge();
        }
    }

    void onInterruptEdge()
    {
        if (state != State::REST) {
            return;
        }

        state = State::IRQ_PENDING;
        irqAtMs = millis();
        startThread();
    }

    void startThread()
    {
        if (!OSThread::enabled) {
            OSThread::setIntervalFromNow(0);
            OSThread::enabled = true;
            runASAP = true;
        }
    }

    void resetStateAndStop()
    {
        state = State::REST;
        if (OSThread::enabled) {
            OSThread::disable();
        }
    }

#ifdef ARCH_ESP32
    int onLightSleep(void *)
    {
        detachInterrupt(BOARD_PCA9535_INT);
        // Clear any latched PCA9535 interrupt before enabling GPIO wake.
        // If INT is left asserted low, light sleep exits immediately.
        (void)io.digitalRead(EXPANDS_BTN_WAKE_UP);
        resetStateAndStop();
        return 0;
    }

    int onLightSleepEnd(esp_sleep_wakeup_cause_t cause)
    {
        (void)cause;
        // Consume any pending interrupt source before reattaching ISR.
        // Check BEFORE clearing: if INT is still asserted the button may still be held,
        // and no new falling edge will fire until it is released.
        bool intAsserted = (digitalRead(BOARD_PCA9535_INT) == LOW);
        (void)io.digitalRead(EXPANDS_BTN_WAKE_UP);
        pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
        attachInterrupt(BOARD_PCA9535_INT, WakeKeyInterruptThread::isr, FALLING);
        if (intAsserted) {
            onInterruptEdge();
        }
        return 0;
    }

    CallbackObserver<WakeKeyInterruptThread, void *> lsObserver{this, &WakeKeyInterruptThread::onLightSleep};
    CallbackObserver<WakeKeyInterruptThread, esp_sleep_wakeup_cause_t> lsEndObserver{this,
                                                                                     &WakeKeyInterruptThread::onLightSleepEnd};
#endif

    volatile State state = State::REST;
    volatile uint32_t irqAtMs = 0;
};

WakeKeyInterruptThread *WakeKeyInterruptThread::instance = nullptr;
WakeKeyInterruptThread *wakeKeyThread = nullptr;
#endif

class WioTrackerMeshLED : public MeshLED
{
  public:
    void init() override { io.digitalWrite(EXPANDS_LED_USER, LOW); }
    void on() override { io.digitalWrite(EXPANDS_LED_USER, HIGH); }
    void off() override { io.digitalWrite(EXPANDS_LED_USER, LOW); }
};

static bool initOK = false;

void earlyInitVariant()
{
    if (io.begin(Wire, BOARD_PCA9535_ADDR, I2C_SDA, I2C_SCL)) {
        io.pinMode(EXPANDS_BTN_WAKE_UP, INPUT); // wakeup button
        io.pinMode(EXPANDS_I2C_0_INT, INPUT);   // I2C IRQ
        io.pinMode(EXPANDS_SD_DETECT, INPUT);   // SD detect

        io.pinMode(EXPANDS_EXP_OTG_EN, OUTPUT);   // OTG EN
        io.digitalWrite(EXPANDS_EXP_OTG_EN, LOW); // OTG EN low
        delay(10);
        io.pinMode(EXPANDS_PA_PWR_EN, OUTPUT);    // PA EN
        io.digitalWrite(EXPANDS_PA_PWR_EN, HIGH); // PA EN high
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
        io.digitalWrite(EXPANDS_TP_INT, LOW); // TP INT low
        delay(10);
        io.digitalWrite(EXPANDS_TP_INT, LOW); // TP INT low
        delay(1);
        io.digitalWrite(EXPANDS_TP_RST, HIGH); // TP RST high
        delay(60);
        io.pinMode(EXPANDS_TP_INT, INPUT); // TP INT
        initOK = true;

        meshLED = std::make_shared<WioTrackerMeshLED>();
        meshLED->init();
    }
}

void lateInitVariant()
{
    if (!initOK) {
        LOG_ERROR("TCA9535 initialization failed");
        return;
    }

    // wake button initialization
#if defined(BOARD_PCA9535_ADDR) && defined(BOARD_PCA9535_BUTTON_MASK)
    //    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
    if (!wakeKeyThread) {
        wakeKeyThread = new WakeKeyInterruptThread();
        wakeKeyThread->begin();
    }
//    }
#endif

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

#endif