#pragma once

#define ARCH_STM32WL

//
// defaults for STM32WL architecture
//

#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif

//
// set HW_VENDOR
//

#ifndef HW_VENDOR
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW
#endif

/* virtual pins */
#define LORA_CS 1000
#define LORA_DIO1 1001
#define LORA_RESET 1003
#define LORA_BUSY 1004
