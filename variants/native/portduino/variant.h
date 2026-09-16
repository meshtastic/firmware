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
#ifndef HAS_TRAFFIC_MANAGEMENT
#define HAS_TRAFFIC_MANAGEMENT 1
#endif
#ifndef HAS_VARIABLE_HOPS
#define HAS_VARIABLE_HOPS 1
#endif
