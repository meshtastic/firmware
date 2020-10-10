#include "OSThread.h"
#include <assert.h>

namespace concurrency
{

ThreadController mainController, timerController;

void OSThread::setup()
{
    mainController.ThreadName = "mainController";
    timerController.ThreadName = "timerController";
}

OSThread::OSThread(const char *_name, uint32_t period, ThreadController *_controller)
    : Thread(NULL, period), controller(_controller)
{
    ThreadName = _name;

    if (controller)
        controller->add(this);
}

OSThread::~OSThread()
{
    if (controller)
        controller->remove(this);
}

void OSThread::run()
{
    auto newDelay = runOnce();

    runned();

    if (newDelay != 0)
        setInterval(newDelay);
}

} // namespace concurrency
