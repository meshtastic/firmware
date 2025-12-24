#pragma once

#include "../freertosinc.h"

#ifdef ARCH_PORTDUINO
#include <pthread.h>
#endif

namespace concurrency {

#ifndef HAS_FREE_RTOS

class BinarySemaphorePosix {
#ifdef ARCH_PORTDUINO
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool signaled;
#endif

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

#endif

} // namespace concurrency