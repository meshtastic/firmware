#pragma once

#include <Arduino.h>

#include "mesh/generated/mesh.pb.h" // For CriticalErrorCode

/// A macro that include filename and line
#define RECORD_CRITICALERROR(code) recordCriticalError(code, __LINE__, __FILE__)

/// Record an error that should be reported via analytics
void recordCriticalError(CriticalErrorCode code = CriticalErrorCode_Unspecified, uint32_t address = 0, const char *filename = NULL);
