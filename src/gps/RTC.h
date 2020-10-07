#pragma once

#include "configuration.h"
#include "sys/time.h"
#include <Arduino.h>

extern bool timeSetFromGPS; // We try to set our time from GPS each time we wake from sleep

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
bool perhapsSetRTC(const struct timeval *tv);
bool perhapsSetRTC(struct tm &t);

/// Return time since 1970 in secs.  Until we have a GPS lock we will be returning time based at zero
uint32_t getTime();

/// Return time since 1970 in secs.  If we don't have a GPS lock return zero
uint32_t getValidTime();

void readFromRTC();