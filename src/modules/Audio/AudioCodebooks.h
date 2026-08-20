#pragma once

#include "configuration.h"

// Binds the codec2 VQ codebooks when the build leaves them out of the image
// (CODEC2_EXTERNAL_CODEBOOKS) and expects them in memory-mapped storage.
//
// Returns nullptr on success, or a short reason suitable for a log line and a
// ClientNotification. A board flashed with the app but no codebook blob has to
// say so rather than going quiet.
const char *audioCodebooksBind();
