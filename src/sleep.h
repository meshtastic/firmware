#pragma once

#include "Arduino.h"
#include "Observer.h"
#include "configuration.h"

#ifdef HAS_PMU
#include "XPowersLibInterface.hpp"
extern XPowersLibInterface *PMU;
#endif

#ifdef ARCH_ESP32
#include "esp_sleep.h"

#define LIGHT_SLEEP_ABORT 0

void initLightSleep();
void doLightSleep(uint32_t msecToWake);
#endif

// perform power on init that we do on each wake from deep sleep
void initDeepSleep();
void doDeepSleep(uint32_t msecToWake, bool skipPreflight, bool skipSaveNodeDb), cpuDeepSleep(uint32_t msecToWake);

void setCPUFast(bool on);

// returns true if sleep is allowed right now
bool doPreflightSleep();

extern int bootCount;

// called to ask any observers if they want to veto sleep. Return 1 to veto or 0 to allow sleep to happen
extern Observable<void *> preflightSleep;

// called to tell observers we are now entering (deep) sleep and you should prepare.  Must return 0
extern Observable<void *> notifyDeepSleep;

// called to tell observers we are rebooting ASAP.  Must return 0
extern Observable<void *> notifyReboot;

#ifdef ARCH_ESP32
// wake cause, set when init from deep sleep is called
extern esp_sleep_source_t wakeCause;

/// called to tell observers that light sleep is about to begin
extern Observable<void *> notifyLightSleep;

/// called to tell observers that light sleep has just ended, and why it ended
extern Observable<esp_sleep_wakeup_cause_t> notifyLightSleepEnd;
#endif
