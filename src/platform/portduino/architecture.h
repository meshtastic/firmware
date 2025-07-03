#pragma once

#define ARCH_PORTDUINO 1

//
// set HW_VENDOR
//

#define HW_VENDOR meshtastic_HardwareModel_PORTDUINO

#ifndef HAS_BUTTON
#define HAS_BUTTON 1
#endif
#ifndef HAS_WIFI
#define HAS_WIFI 1
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_RTC
#define HAS_RTC 1
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 1
#endif
#ifndef HAS_SENSOR
#define HAS_SENSOR 1
#endif
#ifndef HAS_TRACKBALL
#define HAS_TRACKBALL 1
#define TB_DOWN (uint8_t) settingsMap[tbDownPin]
#define TB_UP (uint8_t) settingsMap[tbUpPin]
#define TB_LEFT (uint8_t) settingsMap[tbLeftPin]
#define TB_RIGHT (uint8_t) settingsMap[tbRightPin]
#define TB_PRESS (uint8_t) settingsMap[tbPressPin]
#endif