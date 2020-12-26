#pragma once

#include <Arduino.h>

/** Error codes for critical errors
 * 
 * The device might report these fault codes on the screen.  If you encounter a fault code, please
 * post on the meshtastic.discourse.group and we'll try to help.
 */
enum CriticalErrorCode { 
    NoError = 0, 

    /// A software bug was detected while trying to send lora packets
    ErrTxWatchdog = 1, 

    /// A software bug was detected on entry to sleep
    ErrSleepEnterWait = 2, 

    /// No Lora radio hardware could be found
    ErrNoRadio = 3, 

    /// Not normally used
    ErrUnspecified = 4, 

    /// We failed while configuring a UBlox GPS
    ErrUBloxInitFailed = 5,

    /// This board was expected to have a power management chip and it is missing or broken
    ErrNoAXP192 = 6 
    };

/// Record an error that should be reported via analytics
void recordCriticalError(CriticalErrorCode code, uint32_t address = 0);
