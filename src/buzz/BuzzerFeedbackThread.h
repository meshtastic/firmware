#pragma once

#include "Observer.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"

class BuzzerFeedbackThread : public concurrency::OSThread
{
    CallbackObserver<BuzzerFeedbackThread, const InputEvent *> inputObserver =
        CallbackObserver<BuzzerFeedbackThread, const InputEvent *>(this, &BuzzerFeedbackThread::handleInputEvent);

  public:
    BuzzerFeedbackThread();
    int handleInputEvent(const InputEvent *event);

  protected:
    virtual int32_t runOnce() override;

  private:
    uint32_t lastEventTime = 0;
    bool needsUpdate = false;
};

extern BuzzerFeedbackThread *buzzerFeedbackThread;
