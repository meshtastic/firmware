#pragma once

#include "configuration.h"
#include "sys/time.h"
#include <Arduino.h>

enum RTCQuality {

    /// We haven't had our RTC set yet
    RTCQualityNone = 0,

    /// We got time from an onboard peripheral after boot.
    RTCQualityDevice = 1,

    /// Some other node gave us a time we can use
    RTCQualityFromNet = 2,

    /// Our time is based on NTP
    RTCQualityNTP = 3,

    /// Our time is based on our own GPS
    RTCQualityGPS = 4
};

RTCQuality getRTCQuality();

extern uint32_t lastSetFromPhoneNtpOrGps;

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
bool perhapsSetRTC(RTCQuality q, const struct timeval *tv, bool forceUpdate = false);
bool perhapsSetRTC(RTCQuality q, struct tm &t);

/// Return a string name for the quality
const char *RtcName(RTCQuality quality);

/// Return time since 1970 in secs.  While quality is RTCQualityNone we will be returning time based at zero
uint32_t getTime(bool local = false);

/// Return time since 1970 in secs.  If quality is RTCQualityNone return zero
uint32_t getValidTime(RTCQuality minQuality, bool local = false);

void readFromRTC();

time_t gm_mktime(struct tm *tm);

#define SEC_PER_DAY 86400
#define SEC_PER_HOUR 3600
#define SEC_PER_MIN 60
