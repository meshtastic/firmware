#pragma once

#include <Arduino.h>

/// Error codes for critical error
enum CriticalErrorCode { NoError, ErrTxWatchdog, ErrSleepEnterWait, ErrNoRadio, ErrUnspecified, UBloxInitFailed };

/// Record an error that should be reported via analytics
void recordCriticalError(CriticalErrorCode code, uint32_t address = 0);
