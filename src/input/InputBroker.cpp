#include "InputBroker.h"

InputBroker *inputBroker;

InputBroker::InputBroker()
{
};

void InputBroker::registerSource(Observable<const InputEvent *> *source)
{
    this->inputEventObserver.observe(source);
}

int InputBroker::handleInputEvent(const InputEvent *event)
{
  this->notifyObservers(event);
  return 0;
}