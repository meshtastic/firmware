#pragma once

#include <Arduino.h>

#include "mesh/generated/meshtastic/mesh.pb.h" // For CriticalErrorCode

/// A macro that include filename and line
#define RECORD_CRITICALERROR(code) recordCriticalError(code, __LINE__, __FILE__)

/// Record an error that should be reported via analytics
void recordCriticalError(meshtastic_CriticalErrorCode code = meshtastic_CriticalErrorCode_UNSPECIFIED, uint32_t address = 0,
                         const char *filename = NULL);