#pragma once

#include <Arduino.h>

#include "mesh/mesh.pb.h" // For CriticalErrorCode

/// Record an error that should be reported via analytics
void recordCriticalError(CriticalErrorCode code = CriticalErrorCode_Unspecified, uint32_t address = 0);
