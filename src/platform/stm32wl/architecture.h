#pragma once

#define ARCH_STM32WL

//
// defaults for STM32WL architecture
//

#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 1
#endif
#ifndef HAS_WIRE
#define HAS_WIRE 1
#endif

//
// set HW_VENDOR
//
#ifdef _VARIANT_WIOE5_
#define HW_VENDOR meshtastic_HardwareModel_WIO_E5
#elif defined(_VARIANT_RAK3172_)
#define HW_VENDOR meshtastic_HardwareModel_RAK3172
#else
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW
#endif

/* virtual pins */
#define SX126X_CS 1000
#define SX126X_DIO1 1001
#define SX126X_RESET 1003
#define SX126X_BUSY 1004
