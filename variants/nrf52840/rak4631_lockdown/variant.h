/*
 * RAK4631 LOCKDOWN Build variant header
 * Includes the parent RAK4631 variant and adds LOCKDOWN-specific overrides.
 */

#ifndef _VARIANT_RAK4631_LOCKDOWN_
#define _VARIANT_RAK4631_LOCKDOWN_

// Include the parent RAK4631 variant - all pin definitions, radio config, etc.
#include "../rak4631/variant.h"

// Disable ethernet — reduces attack surface and avoids W5100S dependency
#ifdef HAS_ETHERNET
#undef HAS_ETHERNET
#endif
#define HAS_ETHERNET 0

#endif // _VARIANT_RAK4631_LOCKDOWN_
