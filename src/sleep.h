#pragma once

#include "Arduino.h"
#include "esp_sleep.h"

void doDeepSleep(uint64_t msecToWake);
esp_sleep_wakeup_cause_t doLightSleep(uint64_t msecToWake);
void setBluetoothEnable(bool on);
void setGPSPower(bool on);

// Perform power on init that we do on each wake from deep sleep
void initDeepSleep();

void setCPUFast(bool on);
void setLed(bool ledOn);

extern int bootCount;
extern esp_sleep_source_t wakeCause;

// is bluetooth sw currently running?
extern bool bluetoothOn;