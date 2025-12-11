#ifdef T_LORA_PAGER

#include "RotaryEncoderImpl.h"
#include "InputBroker.h"
#include "RotaryEncoder.h"
#ifdef ARCH_ESP32
#include "sleep.h"
#endif

#define ORIGIN_NAME "RotaryEncoder"

RotaryEncoderImpl *rotaryEncoderImpl;

RotaryEncoderImpl::RotaryEncoderImpl()
{
    rotary = nullptr;
#ifdef ARCH_ESP32
    isFirstInit = true;
#endif
}

RotaryEncoderImpl::~RotaryEncoderImpl()
{
    LOG_DEBUG("RotaryEncoderImpl destructor");
    detachRotaryEncoderInterrupts();

    if (rotary != nullptr) {
        delete rotary;
        rotary = nullptr;
    }
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
        rotary = new RotaryEncoder(moduleConfig.canned_message.inputbroker_pin_a, moduleConfig.canned_message.inputbroker_pin_b,
                                   moduleConfig.canned_message.inputbroker_pin_press);
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
    if (rotary->readButton() == RotaryEncoder::ButtonState::BUTTON_PRESSED) {
        if (lastPressed + 200 < millis()) {
            LOG_DEBUG("Rotary event Press");
            lastPressed = millis();
            e.inputEvent = this->eventPressed;
            inputBroker->queueInputEvent(&e);
        }
    }

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
        detachInterrupt(moduleConfig.canned_message.inputbroker_pin_a);
        detachInterrupt(moduleConfig.canned_message.inputbroker_pin_b);
        detachInterrupt(moduleConfig.canned_message.inputbroker_pin_press);
        interruptInstance = nullptr;
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
        auto interruptHandler = []() { inputBroker->requestPollSoon(interruptInstance); };
        attachInterrupt(moduleConfig.canned_message.inputbroker_pin_a, interruptHandler, CHANGE);
        attachInterrupt(moduleConfig.canned_message.inputbroker_pin_b, interruptHandler, CHANGE);
        attachInterrupt(moduleConfig.canned_message.inputbroker_pin_press, interruptHandler, CHANGE);
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