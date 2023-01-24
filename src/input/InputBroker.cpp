#include "InputBroker.h"
#include "PowerFSM.h" // needed for event trigger

InputBroker *inputBroker;

InputBroker::InputBroker(){};

void InputBroker::registerSource(Observable<const InputEvent *> *source)
{
    this->inputEventObserver.observe(source);
}

int InputBroker::handleInputEvent(const InputEvent *event)
{
    powerFSM.trigger(EVENT_INPUT);
    this->notifyObservers(event);
    return 0;
}