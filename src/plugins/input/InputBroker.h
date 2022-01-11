#pragma once
#include "Observer.h"
#include "HardwareInput.h"

class InputBroker :
    public Observable<const InputEvent *>
{
    CallbackObserver<InputBroker, const InputEvent *> inputEventObserver =
        CallbackObserver<InputBroker, const InputEvent *>(this, &InputBroker::handleInputEvent);

  public:
    InputBroker();
    void registerOrigin(Observable<const InputEvent *> *origin);

  protected:
    int handleInputEvent(const InputEvent *event);
};

extern InputBroker *inputBroker;