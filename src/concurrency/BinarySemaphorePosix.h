#pragma once

#include "../freertosinc.h"

namespace concurrency {

#ifndef HAS_FREE_RTOS

#ifdef ARCH_PORTDUINO
// Full pthread implementation for native Linux
#include <pthread.h>

class BinarySemaphorePosix {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool signaled;

public:
  BinarySemaphorePosix();
  ~BinarySemaphorePosix();

  /**
   * Returns false if we timed out
   */
  bool take(uint32_t msec);

  void give();

  void giveFromISR(BaseType_t *pxHigherPriorityTaskWoken);
};

#else // !ARCH_PORTDUINO - Stub class for bare-metal platforms

class BinarySemaphorePosix {
public:
  BinarySemaphorePosix();
  ~BinarySemaphorePosix();

  bool take(uint32_t msec);
  void give();
  void giveFromISR(BaseType_t *pxHigherPriorityTaskWoken);
};

#endif // ARCH_PORTDUINO

#endif // HAS_FREE_RTOS

} // namespace concurrency