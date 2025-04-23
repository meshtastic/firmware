#include "SPILock.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "sleep.h"
#include <Arduino.h>
#include <assert.h>

concurrency::Lock *spiLock;

class SPILock : public concurrency::Lock
{
  public:
    SPILock();
    ~SPILock();

    void lock() override;
    void unlock() override;

  private:
    bool locked;

    int preflightSleepCb(void *unused = NULL) { return locked ? 1 : 0; }

    CallbackObserver<SPILock, void *> preflightSleepObserver =
        CallbackObserver<SPILock, void *>(this, &SPILock::preflightSleepCb);
};

SPILock::SPILock() : Lock()
{
    locked = false;
    preflightSleepObserver.observe(&preflightSleep);
}

SPILock::~SPILock()
{
    preflightSleepObserver.unobserve(&preflightSleep);
}

void SPILock::lock()
{
    powerFSM.trigger(EVENT_WAKE_TIMER);

    Lock::lock();
    locked = true;
}

void SPILock::unlock()
{
    locked = false;
    Lock::unlock();
}

void initSPI()
{
    assert(!spiLock);
    spiLock = new SPILock();
}