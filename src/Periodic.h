#pragma once

#include <Arduino.h>
#include "PeriodicTask.h"

/**
 * Periodically invoke a callback.
 * 
 * This just provides C style callback conventions rather than a virtual function - FIXME, remove?
 */
class Periodic : public PeriodicTask
{
  uint32_t (*callback)();

public:
  // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
  Periodic(uint32_t (*_callback)()) : callback(_callback) {}

protected:

  uint32_t doTask();
};
