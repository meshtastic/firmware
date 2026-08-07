#pragma once

#include "configuration.h"

#if HAS_CELLULAR

// Create the modem thread for the driver selected at build time, and start the
// power-on sequence.
void initCellModem();

#endif
