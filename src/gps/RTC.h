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

/// The RTC set result codes
/// Used to indicate the result of an attempt to set the RTC.
enum RTCSetResult {
    RTCSetResultNotSet = 0,      ///< RTC was set successfully
    RTCSetResultSuccess = 1,     ///< RTC was set successfully
    RTCSetResultInvalidTime = 3, ///< The provided time was invalid (e.g., before the build epoch)
    RTCSetResultError = 4        ///< An error occurred while setting the RTC
};

RTCQuality getRTCQuality();

extern uint32_t lastSetFromPhoneNtpOrGps;

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
RTCSetResult perhapsSetRTC(RTCQuality q, const struct timeval *tv, bool forceUpdate = false);
RTCSetResult perhapsSetRTC(RTCQuality q, struct tm &t);

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
