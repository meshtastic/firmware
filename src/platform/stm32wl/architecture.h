#pragma once

#define ARCH_STM32WL

//
// defaults for STM32WL architecture
//

//
// set HW_VENDOR
//

#ifndef HW_VENDOR
    #define HW_VENDOR HardwareModel_PRIVATE_HW
#endif

#ifdef __cplusplus
extern "C" {
#endif
    void stm32wl_emulate_digitalWrite(long unsigned int pin, long unsigned int value);
    int stm32wl_emulate_digitalRead(long unsigned int pin);
#ifdef __cplusplus
}
#endif

/* virtual pins for stm32wl_emulate_digitalWrite() / stm32wl_emulate_digitalRead() to recognize */
#define SX126X_CS    1000
#define SX126X_DIO1  1001
#define SX126X_RESET 1003
#define SX126X_BUSY  1004

