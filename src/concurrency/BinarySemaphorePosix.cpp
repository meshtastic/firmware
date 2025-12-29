#include "concurrency/BinarySemaphorePosix.h"
#include "configuration.h"

#ifndef HAS_FREE_RTOS

// Only use pthread implementation on native Linux (Portduino)
#ifdef ARCH_PORTDUINO

#include <errno.h>
#include <sys/time.h>

namespace concurrency {

BinarySemaphorePosix::BinarySemaphorePosix() : signaled(false) {
  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);
}

BinarySemaphorePosix::~BinarySemaphorePosix() {
  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&mutex);
}

/**
 * Block until signaled or timeout expires.
 * Returns true if we were signaled, false if we timed out.
 */
bool BinarySemaphorePosix::take(uint32_t msec) {
  struct timespec ts;
  struct timeval tv;

  gettimeofday(&tv, NULL);
  ts.tv_sec = tv.tv_sec + (msec / 1000);
  ts.tv_nsec = (tv.tv_usec * 1000) + ((msec % 1000) * 1000000);
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }

  pthread_mutex_lock(&mutex);

  while (!signaled) {
    int rc = pthread_cond_timedwait(&cond, &mutex, &ts);
    if (rc == ETIMEDOUT) {
      pthread_mutex_unlock(&mutex);
      return false;
    }
  }

  signaled = false;
  pthread_mutex_unlock(&mutex);
  return true;
}

void BinarySemaphorePosix::give() {
  pthread_mutex_lock(&mutex);
  signaled = true;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);
}

IRAM_ATTR void
BinarySemaphorePosix::giveFromISR(BaseType_t *pxHigherPriorityTaskWoken) {
  // POSIX doesn't distinguish ISR context; delegate to regular give()
  give();
  if (pxHigherPriorityTaskWoken) {
    *pxHigherPriorityTaskWoken = 1; // Equivalent to pdTRUE
  }
}

} // namespace concurrency

#else // !ARCH_PORTDUINO - Stub implementation for bare-metal platforms

namespace concurrency {

BinarySemaphorePosix::BinarySemaphorePosix() {}
BinarySemaphorePosix::~BinarySemaphorePosix() {}

bool BinarySemaphorePosix::take(uint32_t msec) {
  // Stub: just delay and return
  delay(msec);
  return false;
}

void BinarySemaphorePosix::give() {
  // Stub: no-op on bare-metal without FreeRTOS
}

IRAM_ATTR void
BinarySemaphorePosix::giveFromISR(BaseType_t *pxHigherPriorityTaskWoken) {
  if (pxHigherPriorityTaskWoken) {
    *pxHigherPriorityTaskWoken = 0;
  }
}

} // namespace concurrency

#endif // ARCH_PORTDUINO

#endif // HAS_FREE_RTOS