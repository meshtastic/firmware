#include "InputBroker.h"
#include "PowerFSM.h" // needed for event trigger

InputBroker *inputBroker = nullptr;

InputBroker::InputBroker()
{
#ifdef HAS_FREE_RTOS
    inputEventQueue = xQueueCreate(5, sizeof(InputEvent));
    pollSoonQueue = xQueueCreate(5, sizeof(InputPollable *));
    xTaskCreate(pollSoonWorker, "input-pollSoon", 2 * 1024, this, 10, &pollSoonTask);
#endif
}

void InputBroker::registerSource(Observable<const InputEvent *> *source)
{
    this->inputEventObserver.observe(source);
}

#ifdef HAS_FREE_RTOS
void InputBroker::pollSoonRequestFromIsr(InputPollable *pollable)
{
    xQueueSendFromISR(pollSoonQueue, &pollable, NULL);
}

void InputBroker::queueInputEvent(const InputEvent *event)
{
    xQueueSend(inputEventQueue, event, portMAX_DELAY);
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
    this->notifyObservers(event);
    return 0;
}

#ifdef HAS_FREE_RTOS
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
