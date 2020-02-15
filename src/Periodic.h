#pragma once

#include <Arduino.h>

/**
 * Periodically invoke a callback.
 * 
 * FIXME: currently just syntatic sugar for polling in loop (you must call .loop), but eventually
 * generalize with the freertos scheduler so we can save lots of power by having everything either in
 * something like this or triggered off of an irq.
 */
class Periodic
{
  uint32_t lastMsec = 0;
  uint32_t period = 1; // call soon after creation
  uint32_t (*callback)();

public:
  // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
  Periodic(uint32_t (*_callback)()) : callback(_callback) {}

  // for the time being this must be called from loop
  void loop()
  {
    uint32_t now = millis();
    if (period && (now - lastMsec) >= period)
    {
      lastMsec = now;
      period = (*callback)();
    }
  }
};
