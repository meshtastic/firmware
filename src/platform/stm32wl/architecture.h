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
// ADC defaults for STM32WL
//
#if defined(LL_ADC_RESOLUTION_12B)
#define LL_ADC_RESOLUTION LL_ADC_RESOLUTION_12B
#elif defined(LL_ADC_DS_DATA_WIDTH_12_BIT)
#define LL_ADC_RESOLUTION LL_ADC_DS_DATA_WIDTH_12_BIT
#else
#error "ADC resolution could not be defined!"
#endif
#define ADC_RANGE 4096

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
