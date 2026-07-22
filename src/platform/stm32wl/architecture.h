#pragma once

#define ARCH_STM32WL
#define ARCH_STM32

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
#ifndef HAS_LSE
#define HAS_LSE 0
#endif

// How long to wait for the LSE 32.768kHz crystal to lock before giving up on hardware RTC support.
// Override in a variant's variant.h if that board's crystal needs longer to stabilize.
#ifndef STM32WL_LSE_TIMEOUT_MS
#define STM32WL_LSE_TIMEOUT_MS 2000
#endif

// A variant that sets HAS_LSE must also define STM32WL_LSE_DRIVE - catch that mistake here, not as a confusing
// HAL compile error deep in main-stm32wl.cpp.
#if HAS_LSE && !defined(STM32WL_LSE_DRIVE)
#error                                                                                                                           \
    "HAS_LSE is set but STM32WL_LSE_DRIVE is not defined - set it in the variant's variant.h to one of RCC_LSEDRIVE_LOW/MEDIUMLOW/MEDIUMHIGH/HIGH"
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

#if !defined(DEBUG_MUTE) && !defined(PIO_FRAMEWORK_ARDUINO_NANOLIB_FLOAT_PRINTF)
#error                                                                                                                           \
    "You MUST enable PIO_FRAMEWORK_ARDUINO_NANOLIB_FLOAT_PRINTF if debug prints are enabled. printf will print uninitialized garbage instead of floats."
#endif