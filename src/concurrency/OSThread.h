#pragma once

#include <cstdlib>
#include <stdint.h>

#include "Thread.h"
#include "freertosinc.h"

namespace concurrency
{

/**
 * @brief Base threading
 *
 * TODO FIXME @geeksville
 * basic functionality
 * sleeping the correct amount of time in main
 * NotifiedWorkerThread set/clears enabled
 *
 * stopping sleep instantly as soon as an event occurs.
 * use global functions delayTillWakeEvent(time), doWakeEvent(isInISR) - use freertos mutex or somesuch
 *
 * remove lock/lockguard
 */
class OSThread
{
  public:
    virtual ~OSThread() {}

    // uint32_t getStackHighwaterMark() { return uxTaskGetStackHighWaterMark(taskHandle); }

  protected:
    /**
     * The method that will be called each time our thread gets a chance to run
     */
    virtual void runOnce() = 0;
};

} // namespace concurrency
