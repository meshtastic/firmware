#include "InputBroker.h"

InputBroker *inputBroker;

InputBroker::InputBroker()
{
};

void InputBroker::registerOrigin(Observable<const InputEvent *> *origin)
{
    this->inputEventObserver.observe(origin);
}

int InputBroker::handleInputEvent(const InputEvent *event)
{
  this->notifyObservers(event);
}