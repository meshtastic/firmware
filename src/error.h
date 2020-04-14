#pragma once

/// Error codes for critical error
enum CriticalErrorCode { NoError, ErrTxWatchdog, ErrSleepEnterWait, ErrNoRadio };

/// Record an error that should be reported via analytics
void recordCriticalError(CriticalErrorCode code, uint32_t address = 0);
