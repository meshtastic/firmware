#ifdef T_LORA_PAGER

#include "RotaryEncoderImpl.h"
#include "InputBroker.h"
#include "RotaryEncoder.h"
#include "mesh/RadioLibInterface.h"
#include "mesh/Throttle.h"
#ifdef ARCH_ESP32
#include "sleep.h"
#endif

#define ORIGIN_NAME "RotaryEncoder"

RotaryEncoderImpl *rotaryEncoderImpl;

RotaryEncoderImpl::RotaryEncoderImpl()
{
#ifdef ARCH_ESP32
    isFirstInit = true;
#endif
}

RotaryEncoderImpl::~RotaryEncoderImpl()
{
    LOG_DEBUG("RotaryEncoderImpl destructor");
    detachRotaryEncoderInterrupts();
}

bool RotaryEncoderImpl::init()
{
    if (!moduleConfig.canned_message.updown1_enabled || moduleConfig.canned_message.inputbroker_pin_a == 0 ||
        moduleConfig.canned_message.inputbroker_pin_b == 0) {
        // Input device is disabled.
        return false;
    }

    eventCw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_cw);
    eventCcw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_ccw);
    eventPressed = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_press);

    if (rotary == nullptr) {
        rotary.reset(new RotaryEncoder(moduleConfig.canned_message.inputbroker_pin_a,
                                       moduleConfig.canned_message.inputbroker_pin_b,
                                       moduleConfig.canned_message.inputbroker_pin_press));
    }

    attachRotaryEncoderInterrupts();

#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    // Used to detach and reattach interrupts
    if (isFirstInit) {
        lsObserver.observe(&notifyLightSleep);
        lsEndObserver.observe(&notifyLightSleepEnd);
        isFirstInit = false;
    }
#endif

    LOG_INFO("RotaryEncoder initialized pins(%d, %d, %d), events(%d, %d, %d)", moduleConfig.canned_message.inputbroker_pin_a,
             moduleConfig.canned_message.inputbroker_pin_b, moduleConfig.canned_message.inputbroker_pin_press, eventCw, eventCcw,
             eventPressed);
    return true;
}

void RotaryEncoderImpl::pollOnce()
{
    InputEvent e{ORIGIN_NAME, INPUT_BROKER_NONE, 0, 0, 0};

    static uint32_t lastPressed = millis();
    static bool wasPressed = false;
    // Polled continuously, and readButton() reports the level: emit only on the released->pressed edge
    bool pressed = rotary->readButton() == RotaryEncoder::ButtonState::BUTTON_PRESSED;
    if (pressed && !wasPressed && Throttle::hasElapsed(lastPressed, 200)) {
        LOG_DEBUG("Rotary event Press");
        lastPressed = millis();
        e.inputEvent = this->eventPressed;
        inputBroker->queueInputEvent(&e);
    }
    wasPressed = pressed;

    switch (rotary->process()) {
    case RotaryEncoder::DIRECTION_CW:
        LOG_DEBUG("Rotary event CW");
        e.inputEvent = this->eventCw;
        inputBroker->queueInputEvent(&e);
        break;
    case RotaryEncoder::DIRECTION_CCW:
        LOG_DEBUG("Rotary event CCW");
        e.inputEvent = this->eventCcw;
        inputBroker->queueInputEvent(&e);
        break;
    default:
        break;
    }
}

void RotaryEncoderImpl::detachRotaryEncoderInterrupts()
{
    LOG_DEBUG("RotaryEncoderImpl detach button interrupts");
    if (interruptInstance == this) {
        interruptInstance = nullptr; // polling task idles
    } else {
        LOG_WARN("RotaryEncoderImpl: interrupts already detached");
    }
}

void RotaryEncoderImpl::attachRotaryEncoderInterrupts()
{
    LOG_DEBUG("RotaryEncoderImpl attach button interrupts");
    if (rotary != nullptr && interruptInstance == nullptr) {
        rotary->resetButton();

        interruptInstance = this;
        // Poll instead of using pin-change interrupts. On battery (no USB ground) LoRa TX couples noise into the
        // encoder lines; the resulting GPIO interrupt storm (>2k IRQs/s measured) starves the tick interrupt and
        // trips the interrupt watchdog ("Interrupt wdt timeout on CPU1" during SX126x startTransmit).
        static TaskHandle_t pollTask = nullptr;
        if (!pollTask) {
            xTaskCreate(
                [](void *) {
                    while (true) {
                        // TX also makes the lines noisy enough to fake steps: don't sample while transmitting
                        bool txActive = RadioLibInterface::instance && RadioLibInterface::instance->isSending();
                        RotaryEncoderImpl *inst = interruptInstance; // detach may clear it concurrently
                        if (inst && !txActive)
                            inst->pollOnce();
                        vTaskDelay(pdMS_TO_TICKS(2)); // 500 Hz is plenty for a hand-turned encoder
                    }
                },
                "rotary-poll", 2 * 1024, nullptr, 10, &pollTask);
        }
    } else {
        LOG_WARN("RotaryEncoderImpl: interrupts already attached");
    }
}

#ifdef ARCH_ESP32

int RotaryEncoderImpl::beforeLightSleep(void *unused)
{
    detachRotaryEncoderInterrupts();
    return 0; // Indicates success;
}

int RotaryEncoderImpl::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachRotaryEncoderInterrupts();
    return 0; // Indicates success;
}
#endif

RotaryEncoderImpl *RotaryEncoderImpl::interruptInstance;

#endif