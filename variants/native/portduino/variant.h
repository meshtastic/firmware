#ifndef HAS_SCREEN
#define HAS_SCREEN 1
#endif
#define USE_TFTDISPLAY 1
#define HAS_GPS 1
#define MAX_RX_TOPHONE portduino_config.maxtophone
#define MAX_NUM_NODES portduino_config.MaxNodes

// RAK12002 RTC Module
#define RV3028_RTC (uint8_t)0b1010010

// Enable Traffic Management Module for native/portduino
#ifdef HAS_TRAFFIC_MANAGEMENT
#undef HAS_TRAFFIC_MANAGEMENT
#endif
#define HAS_TRAFFIC_MANAGEMENT 1
#ifdef TRAFFIC_MANAGEMENT_CACHE_SIZE
#undef TRAFFIC_MANAGEMENT_CACHE_SIZE
#endif
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 2048
