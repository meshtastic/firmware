#pragma once

#include "BaseThread.h"

#ifdef __unix__

namespace concurrency
{

/**
 * @brief Base threading
 */
class PosixThread : public BaseThread
{
  protected:
  public:
    void start(const char *name, size_t stackSize = 1024, uint32_t priority = tskIDLE_PRIORITY) {}

    virtual ~PosixThread() {}

    // uint32_t getStackHighwaterMark() { return uxTaskGetStackHighWaterMark(taskHandle); }

  protected:
    /**
     * The method that will be called when start is called.
     */
    virtual void doRun() = 0;

};

} // namespace concurrency

#endif