#ifdef T_LORA_PAGER

#include "RotaryEncoderImpl.h"
#include "InputBroker.h"
#include "RotaryEncoder.h"

#define ORIGIN_NAME "RotaryEncoder"

#define ROTARY_INTERRUPT_FLAG _BV(0)

RotaryEncoderImpl *rotaryEncoderImpl;

RotaryEncoderImpl::RotaryEncoderImpl() : concurrency::OSThread(ORIGIN_NAME), originName(ORIGIN_NAME)
{
    rotary = nullptr;
}

bool RotaryEncoderImpl::init()
{
    if (!moduleConfig.canned_message.updown1_enabled || moduleConfig.canned_message.inputbroker_pin_a == 0 ||
        moduleConfig.canned_message.inputbroker_pin_b == 0) {
        // Input device is disabled.
        disable();
        return false;
    }

    eventCw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_cw);
    eventCcw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_ccw);
    eventPressed = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_press);

    rotary = new RotaryEncoder(moduleConfig.canned_message.inputbroker_pin_a, moduleConfig.canned_message.inputbroker_pin_b,
                               moduleConfig.canned_message.inputbroker_pin_press);
    rotary->resetButton();

    inputQueue = xQueueCreate(5, sizeof(input_broker_event));
    interruptFlag = xEventGroupCreate();
    interruptInstance = this;
    auto interruptHandler = []() { xEventGroupSetBits(interruptInstance->interruptFlag, ROTARY_INTERRUPT_FLAG); };
    attachInterrupt(moduleConfig.canned_message.inputbroker_pin_a, interruptHandler, CHANGE);
    attachInterrupt(moduleConfig.canned_message.inputbroker_pin_b, interruptHandler, CHANGE);
    attachInterrupt(moduleConfig.canned_message.inputbroker_pin_press, interruptHandler, CHANGE);
    xTaskCreate(inputWorker, "rotary", 2 * 1024, this, 10, &inputWorkerTask);

    inputBroker->registerSource(this);

    LOG_INFO("RotaryEncoder initialized pins(%d, %d, %d), events(%d, %d, %d)", moduleConfig.canned_message.inputbroker_pin_a,
             moduleConfig.canned_message.inputbroker_pin_b, moduleConfig.canned_message.inputbroker_pin_press, eventCw, eventCcw,
             eventPressed);
    return true;
}

void RotaryEncoderImpl::dispatchInputs()
{
    static uint32_t lastPressed = millis();
    if (rotary->readButton() == RotaryEncoder::ButtonState::BUTTON_PRESSED) {
        if (lastPressed + 200 < millis()) {
            // LOG_DEBUG("Rotary event Press");
            lastPressed = millis();
            xQueueSend(inputQueue, &this->eventPressed, portMAX_DELAY);
        }
    }

    switch (rotary->process()) {
    case RotaryEncoder::DIRECTION_CW:
        // LOG_DEBUG("Rotary event CW");
        xQueueSend(inputQueue, &this->eventCw, portMAX_DELAY);
        break;
    case RotaryEncoder::DIRECTION_CCW:
        // LOG_DEBUG("Rotary event CCW");
        xQueueSend(inputQueue, &this->eventCcw, portMAX_DELAY);
        break;
    default:
        break;
    }
}

void RotaryEncoderImpl::inputWorker(void *p)
{
    RotaryEncoderImpl *instance = (RotaryEncoderImpl *)p;
    while (true) {
        xEventGroupWaitBits(instance->interruptFlag, ROTARY_INTERRUPT_FLAG, pdTRUE, pdTRUE, portMAX_DELAY);
        instance->dispatchInputs();
    }
    vTaskDelete(NULL);
}

RotaryEncoderImpl *RotaryEncoderImpl::interruptInstance;

int32_t RotaryEncoderImpl::runOnce()
{
    InputEvent e{originName, INPUT_BROKER_NONE, 0, 0, 0};
    while (xQueueReceive(inputQueue, &e.inputEvent, 0) == pdPASS) {
        this->notifyObservers(&e);
    }
    return 10;
}

#endif