#pragma once

#include "Arduino.h"
#include "Observer.h"
#include "esp_sleep.h"

void doDeepSleep(uint64_t msecToWake);
esp_sleep_wakeup_cause_t doLightSleep(uint64_t msecToWake);
void setGPSPower(bool on);

// Perform power on init that we do on each wake from deep sleep
void initDeepSleep();

void setCPUFast(bool on);
void setLed(bool ledOn);

extern int bootCount;
extern esp_sleep_source_t wakeCause;

// is bluetooth sw currently running?
extern bool bluetoothOn;

/// Called to ask any observers if they want to veto sleep. Return 1 to veto or 0 to allow sleep to happen
extern Observable<void *> preflightSleep;

/// Called to tell observers we are now entering (light or deep) sleep and you should prepare.  Must return 0
extern Observable<void *> notifySleep;

/// Called to tell observers we are now entering (deep) sleep and you should prepare.  Must return 0
extern Observable<void *> notifyDeepSleep;