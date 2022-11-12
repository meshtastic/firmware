#pragma once

#define ARCH_RP2040

#ifndef HAS_SCREEN
    #define HAS_SCREEN 1
#endif
#ifndef HAS_WIRE
    #define HAS_WIRE 1
#endif

#if defined(PRIVATE_HW)
    #define HW_VENDOR HardwareModel_PRIVATE_HW
#endif