#include "RTC.h"
#include "configuration.h"
#include <stdarg.h>
#include <stm32wle5xx.h>
#include <stm32wlxx_hal.h>

void setBluetoothEnable(bool enable) {}

void playStartMelody() {}

void updateBatteryLevel(uint8_t level) {}

void getMacAddr(uint8_t *dmac)
{
    // https://flit.github.io/2020/06/06/mcu-unique-id-survey.html
    const uint32_t uid0 = HAL_GetUIDw0(); // X/Y coordinate on wafer
    const uint32_t uid1 = HAL_GetUIDw1(); // [31:8] Lot number (23:0), [7:0] Wafer number
    const uint32_t uid2 = HAL_GetUIDw2(); // Lot number (55:24)

    // Need to go from 96-bit to 48-bit unique ID
    dmac[5] = (uint8_t)uid0;
    dmac[4] = (uint8_t)(uid0 >> 16);
    dmac[3] = (uint8_t)uid1;
    dmac[2] = (uint8_t)(uid1 >> 8);
    dmac[1] = (uint8_t)uid2;
    dmac[0] = (uint8_t)(uid2 >> 8);
}

void cpuDeepSleep(uint32_t msecToWake) {}

// Hacks to force more code and data out.

// By default __assert_func uses fiprintf which pulls in stdio.
extern "C" void __wrap___assert_func(const char *, int, const char *, const char *)
{
    while (true)
        ;
    return;
}

// By default strerror has a lot of strings we probably don't use. Make it return an empty string instead.
char empty = 0;
extern "C" char *__wrap_strerror(int)
{
    return &empty;
}

#ifdef MESHTASTIC_EXCLUDE_TZ
struct _reent;

// Even if you don't use timezones, mktime will try to set the timezone anyway with _tzset_unlocked(), which pulls in scanf and
// friends. The timezone is initialized to UTC by default.
extern "C" void __wrap__tzset_unlocked_r(struct _reent *reent_ptr)
{
    return;
}
#endif

// Taken from https://interrupt.memfault.com/blog/cortex-m-hardfault-debug
typedef struct __attribute__((packed)) ContextStateFrame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t return_address;
    uint32_t xpsr;
} sContextStateFrame;

// NOTE: If you are using CMSIS, the registers can also be
// accessed through CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk
#define HALT_IF_DEBUGGING()                                                                                                      \
    do {                                                                                                                         \
        if ((*(volatile uint32_t *)0xE000EDF0) & (1 << 0)) {                                                                     \
            __asm("bkpt 1");                                                                                                     \
        }                                                                                                                        \
    } while (0)

static char hardfault_message_buffer[256];

// printf directly using srcwrapper's debug UART function.
static void debug_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(hardfault_message_buffer, sizeof(hardfault_message_buffer), format, args);
    va_end(args);

    if (length < 0)
        return;
    uart_debug_write((uint8_t *)hardfault_message_buffer, min((unsigned int)length, sizeof(hardfault_message_buffer) - 1));
}

// N picked by guessing
#define DOT_TIME 1200000
static void dot()
{
    digitalWrite(LED_POWER, LED_STATE_ON);
    for (volatile int i = 0; i < DOT_TIME; i++) { /* busy wait */
    }
    digitalWrite(LED_POWER, LED_STATE_OFF);
    for (volatile int i = 0; i < DOT_TIME; i++) { /* busy wait */
    }
}

static void dash()
{
    digitalWrite(LED_POWER, LED_STATE_ON);
    for (volatile int i = 0; i < (DOT_TIME * 3); i++) { /* busy wait */
    }
    digitalWrite(LED_POWER, LED_STATE_OFF);
    for (volatile int i = 0; i < DOT_TIME; i++) { /* busy wait */
    }
}

static void space()
{
    for (volatile int i = 0; i < (DOT_TIME * 3); i++) { /* busy wait */
    }
}

// Disable optimizations for this function so "frame" argument
// does not get optimized away
extern "C" __attribute__((optimize("O0"))) void HardFault_Handler_C(sContextStateFrame *frame)
{
    debug_printf("HardFault!\r\n");
    debug_printf("r0: %08x\r\n", frame->r0);
    debug_printf("r1: %08x\r\n", frame->r1);
    debug_printf("r2: %08x\r\n", frame->r2);
    debug_printf("r3: %08x\r\n", frame->r3);
    debug_printf("r12: %08x\r\n", frame->r12);
    debug_printf("lr: %08x\r\n", frame->lr);
    debug_printf("pc[return address]: %08x\r\n", frame->return_address);
    debug_printf("xpsr: %08x\r\n", frame->xpsr);

    HALT_IF_DEBUGGING();

    // blink SOS forever
    while (1) {
        dot();
        dot();
        dot();
        dash();
        dash();
        dash();
        dot();
        dot();
        dot();
        space();
    }
}