#pragma once

#include "Arduino.h"
#include "Observer.h"
#include "configuration.h"

void doDeepSleep(uint32_t msecToWake, bool skipPreflight, bool skipSaveNodeDb), cpuDeepSleep(uint32_t msecToWake);

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

/** return true if sleep is allowed right now
 * @param deepSleep true when the hardware (radio) is about to be powered down (deep sleep or
 * shutdown), false for a light sleep where the radio keeps running. Observers may veto more
 * aggressively for deep sleep, e.g. while a LoRa transmission is still in flight.
 */
bool doPreflightSleep(bool deepSleep = false);

/// When a power-saving module wants to deep sleep but doPreflightSleep() vetoes it (e.g. the
/// radio is still transmitting), re-check this often, and give up waiting after this many
/// attempts so a busy mesh can't keep the node awake forever
static constexpr uint32_t PREFLIGHT_SLEEP_RETRY_MS = 1000;
static constexpr uint32_t MAX_PREFLIGHT_SLEEP_DEFERRALS = 30;

extern int bootCount;

// is bluetooth sw currently running?
extern bool bluetoothOn;

/// Called to ask any observers if they want to veto sleep. Return 1 to veto or 0 to allow sleep to happen
extern Observable<void *> preflightSleep;

/// Called to tell observers we are now entering (deep) sleep and you should prepare.  Must return 0
extern Observable<void *> notifyDeepSleep;

/// Called to tell observers we are rebooting ASAP.  Must return 0
extern Observable<void *> notifyReboot;

#ifdef ARCH_ESP32
/// Called to tell observers that light sleep is about to begin
extern Observable<void *> notifyLightSleep;

/// Called to tell observers that light sleep has just ended, and why it ended
extern Observable<esp_sleep_wakeup_cause_t> notifyLightSleepEnd;
#endif

void enableModemSleep();
#ifdef ARCH_ESP32
void enableLoraInterrupt();
bool shouldLoraWake(uint32_t msecToWake);
#endif