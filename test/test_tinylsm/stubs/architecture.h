#pragma once

// Minimal stub architecture.h for native testing
// Provides basic architecture defines

// Minimal architecture feature flags - all disabled for tests
#ifndef HAS_WIFI
#define HAS_WIFI 0
#endif

#ifndef HAS_ETHERNET
#define HAS_ETHERNET 0
#endif

// Minimal required defines
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

#ifndef EXT_RAM_ATTR
#define EXT_RAM_ATTR
#endif

#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif
