#pragma once

#include "Observer.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"

class BuzzerFeedbackThread
{
    CallbackObserver<BuzzerFeedbackThread, const InputEvent *> inputObserver =
        CallbackObserver<BuzzerFeedbackThread, const InputEvent *>(this, &BuzzerFeedbackThread::handleInputEvent);

  public:
    BuzzerFeedbackThread();
    int handleInputEvent(const InputEvent *event);
};

extern BuzzerFeedbackThread *buzzerFeedbackThread;
