#pragma once

#include "Arduino.h"
#include "Observer.h"
#include "configuration.h"

void doDeepSleep(uint32_t msecToWake, bool skipPreflight), cpuDeepSleep(uint32_t msecToWake);

#ifdef ARCH_ESP32
#include "esp_sleep.h"
esp_sleep_wakeup_cause_t doLightSleep(uint64_t msecToWake);

extern esp_sleep_source_t wakeCause;
#endif

#ifdef HAS_PMU
#include "XPowersLibInterface.hpp"
extern XPowersLibInterface *PMU;
#endif

// Perform power on init that we do on each wake from deep sleep
void initDeepSleep();

void setCPUFast(bool on);
void setLed(bool ledOn);

/** return true if sleep is allowed right now */
bool doPreflightSleep();

extern int bootCount;

// is bluetooth sw currently running?
extern bool bluetoothOn;

/// Called to ask any observers if they want to veto sleep. Return 1 to veto or 0 to allow sleep to happen
extern Observable<void *> preflightSleep;

/// Called to tell observers we are now entering (light or deep) sleep and you should prepare.  Must return 0
extern Observable<void *> notifySleep;

/// Called to tell observers we are now entering (deep) sleep and you should prepare.  Must return 0
extern Observable<void *> notifyDeepSleep;

void enableModemSleep();
#ifdef ARCH_ESP32
void enableLoraInterrupt();
bool shouldLoraWake(uint32_t msecToWake);
#endif