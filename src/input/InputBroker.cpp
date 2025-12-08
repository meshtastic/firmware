#include "InputBroker.h"
#include "PowerFSM.h" // needed for event trigger
#include "configuration.h"
#include "modules/ExternalNotificationModule.h"

InputBroker *inputBroker = nullptr;

InputBroker::InputBroker()
{
#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
    inputEventQueue = xQueueCreate(5, sizeof(InputEvent));
    pollSoonQueue = xQueueCreate(5, sizeof(InputPollable *));
    xTaskCreate(pollSoonWorker, "input-pollSoon", 2 * 1024, this, 10, &pollSoonTask);
#endif
}

void InputBroker::registerSource(Observable<const InputEvent *> *source)
{
    this->inputEventObserver.observe(source);
}

#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
void InputBroker::requestPollSoon(InputPollable *pollable)
{
    if (xPortInIsrContext() == pdTRUE) {
        xQueueSendFromISR(pollSoonQueue, &pollable, NULL);
    } else {
        xQueueSend(pollSoonQueue, &pollable, 0);
    }
}

void InputBroker::queueInputEvent(const InputEvent *event)
{
    if (xPortInIsrContext() == pdTRUE) {
        xQueueSendFromISR(inputEventQueue, event, NULL);
    } else {
        xQueueSend(inputEventQueue, event, portMAX_DELAY);
    }
}

void InputBroker::processInputEventQueue()
{
    InputEvent event;
    while (xQueueReceive(inputEventQueue, &event, 0)) {
        handleInputEvent(&event);
    }
}
#endif

int InputBroker::handleInputEvent(const InputEvent *event)
{
    powerFSM.trigger(EVENT_INPUT); // todo: not every input should wake, like long hold release

    if (event && event->inputEvent != INPUT_BROKER_NONE && externalNotificationModule &&
        moduleConfig.external_notification.enabled && externalNotificationModule->nagging()) {
        externalNotificationModule->stopNow();
    }

    this->notifyObservers(event);
    return 0;
}

#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
void InputBroker::pollSoonWorker(void *p)
{
    InputBroker *instance = (InputBroker *)p;
    while (true) {
        InputPollable *pollable = NULL;
        xQueueReceive(instance->pollSoonQueue, &pollable, portMAX_DELAY);
        if (pollable) {
            pollable->pollOnce();
        }
    }
    vTaskDelete(NULL);
}
#endif
