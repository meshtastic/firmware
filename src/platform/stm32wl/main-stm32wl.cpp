#include "FSCommon.h"
#include "configuration.h"
#include "error.h"
#include "gps/GPS.h"
#include "gps/RTC.h"
#include <Throttle.h>
#include <cstring>
#include <stdarg.h>
#include <stm32wle5xx.h>
#include <stm32wlxx_hal.h>

#if HAS_LSE
#include <STM32LowPower.h>
#include <STM32RTC.h>

// LSEDRV is a 2-bit RCC_BDCR field where every combination is a legal drive level, so this covers all 4 values.
static_assert((STM32WL_LSE_DRIVE & ~RCC_LSEDRIVE_HIGH) == 0,
              "STM32WL_LSE_DRIVE must be one of RCC_LSEDRIVE_LOW/MEDIUMLOW/MEDIUMHIGH/HIGH");

static bool stm32wlRtcValid = false;
#endif

// ─── Bootloader redirect ──────────────────────────────────────────────────────
// Magic word in .noinit, not TAMP backup regs (STM32duino clock init can wipe those): it
// survives NVIC_SystemReset(), and the .preinit_array hook below runs before HAL_Init().

// STM32WLxx system-memory bootloader base, AN2606 "STM32WLxx bootloader":
// https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf
#define BOOTLOADER_MAGIC 0xD00DB007UL
#define SYS_MEM_BASE 0x1FFF0000UL

// .noinit - not zeroed at startup, survives NVIC_SystemReset().
__attribute__((section(".noinit"), used)) volatile uint32_t g_bootloaderMagic;

// Runs from .preinit_array (below), before every constructor incl. the core's premain()/init(),
// so RCC/SysTick/HAL are still at reset. Core Cortex-M / CMSIS registers only.
__attribute__((used)) static void earlyBootCheck(void)
{
    if (g_bootloaderMagic != BOOTLOADER_MAGIC)
        return;
    g_bootloaderMagic = 0;

    // Return SysTick/NVIC/RCC to reset state before the jump - ST's system bootloader expects it.
    // https://community.st.com/t5/stm32-mcus/how-to-jump-to-system-bootloader-from-application-code-on-stm32/ta-p/49424
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // Same writes CMSIS system_stm32wlxx.c SystemInit() makes, repeated here in case a hook
    // between reset and .preinit_array moved SYSCLK onto the PLL (RM0461 reset values).
    RCC->CR |= 0x00000061U;  // MSION, MSI range 4 MHz
    RCC->CFGR = 0x00070000U; // SYSCLK=MSI, AHB/APB prescalers /1, MCO off
    RCC->CR = 0x00000061U;   // HSEON/HSEBYP/CSSON/PLLON off
    RCC->PLLCFGR = 0x22040100U;
    RCC->CIER = 0x00000000U;
    RCC->CICR = 0x0000033FU; // clear all RCC interrupt flags

    __DSB();
    __ISB();
    SCB->VTOR = SYS_MEM_BASE;
    __set_MSP(*(volatile uint32_t *)SYS_MEM_BASE);
    ((void (*)(void))(*(volatile uint32_t *)(SYS_MEM_BASE + 4)))();
    // Not reached: the bootloader ROM does not return. Reset rather than return, to avoid
    // unwinding this function's epilogue against the now-repointed MSP.
    NVIC_SystemReset();
}

// __libc_init_array runs .preinit_array entries before any .init_array constructor, so this
// precedes the core's premain() (constructor(101)) regardless of link order.
__attribute__((section(".preinit_array"), used)) static void (*const earlyBootCheckEntry)(void) = &earlyBootCheck;

// Drain and release a UART before the reset, as cpuDeepSleep() does.
static void quiesceSerial(HardwareSerial &port)
{
    if (port) {
        port.flush();
        port.end();
    }
}

void enterDfuMode()
{
    g_bootloaderMagic = BOOTLOADER_MAGIC;

    // The ROM bootloader autobauds off the first byte on USART1 (PB6/PB7) or USART2 (PA2/PA3),
    // and every WL UART and GPS sits on those pins. Silence them all before the reset.
#if !MESHTASTIC_EXCLUDE_GPS
    if (gps)
        gps->disable();
#endif
    quiesceSerial(Serial);
#ifdef ENABLE_HWSERIAL1
    quiesceSerial(Serial1);
#endif
#ifdef ENABLE_HWSERIAL2
    quiesceSerial(Serial2);
#endif
    HAL_NVIC_SystemReset();
}

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

bool getDeviceId(uint8_t *deviceId)
{
    // STM32WL: 96-bit factory silicon UID (words w0..w2, little-endian) in bytes 0-11 (rest stay zero).
    uint32_t uid[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
    memcpy(deviceId, uid, sizeof(uid));
    return true;
}

#if HAS_LSE
// Starts the LSE crystal with a bounded timeout and, if it locks, brings up the STM32 hardware RTC on it.
void stm32wlSetup()
{
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(STM32WL_LSE_DRIVE);
    __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);

    uint32_t start = millis();
    bool lseReady = false;
    while (Throttle::isWithinTimespanMs(start, STM32WL_LSE_TIMEOUT_MS)) {
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY)) {
            lseReady = true;
            break;
        }
        delay(5);
    }

    if (lseReady) {
        STM32RTC &rtc = STM32RTC::getInstance();
        rtc.setClockSource(STM32RTC::LSE_CLOCK);
        rtc.begin();
        stm32wlRtcValid = true;
        LowPower.begin();
        LOG_INFO("STM32WL: LSE locked, hardware RTC available");
    } else {
        // Don't leave a failed oscillator burning current.
        __HAL_RCC_LSE_CONFIG(RCC_LSE_OFF);
        LOG_WARN("STM32WL: LSE failed to start within %dms (crystal missing/faulty?) - hardware RTC unavailable",
                 STM32WL_LSE_TIMEOUT_MS);
    }
}

// True once stm32wlSetup() has confirmed the LSE crystal is locked and the hardware RTC is running.
bool stm32wlRtcAvailable()
{
    return stm32wlRtcValid;
}
#else
void stm32wlSetup() {}
#endif

void cpuDeepSleep(uint32_t msecToWake)
{
#if HAS_LSE
    if (!stm32wlRtcAvailable()) {
        // Hardware can't shutdown, but firmware has already prepared itself for shutdown
        // Do not leave the device unresponsive, reset instead
        LOG_WARN("STM32WL: hardware RTC failed, can't deep sleep/shutdown");
        quiesceSerial(Serial);
        HAL_NVIC_SystemReset();
    } else {
        quiesceSerial(Serial);

        if (msecToWake != portMAX_DELAY) {
            LowPower.shutdown(msecToWake);
        } else {
            LowPower.shutdown();
        }
        // RTC wakes from shutdown into MCU reset, so this code should never be reached
        HAL_NVIC_SystemReset();
    }
#endif
}

// ─── Linker hacks to reduce code size ─────────────────────────────────────────

// Requests a reformat-on-next-boot instead of reformatting mid-callback (see lfs_assert() below).
// Same .noinit survives-NVIC_SystemReset() trick as g_bootloaderMagic above.
#define LFS_CORRUPT_MAGIC 0xC0FFEEEEUL

__attribute__((section(".noinit"), used)) static volatile uint32_t g_lfsCorruptMagic;

// Not .noinit - resets every boot, so this only throttles reformats within one power-on session.
static constexpr uint32_t LFS_CORRUPTION_RETRY_DELAY_MS = 20 * 60 * 1000;
static uint32_t lastLfsFormatMs = 0;

extern "C" void lfs_assert(const char *reason)
{
    LOG_ERROR("LittleFS corruption detected: %s", reason);
    if (lastLfsFormatMs != 0 && Throttle::isWithinTimespanMs(lastLfsFormatMs, LFS_CORRUPTION_RETRY_DELAY_MS)) {
        uint32_t msRemain = LFS_CORRUPTION_RETRY_DELAY_MS - (millis() - lastLfsFormatMs);
        LOG_WARN("Pausing %u seconds to avoid wearing the flash with repeated reformats", msRemain / 1000);
        delay(msRemain);
    }
    LOG_INFO("Rebooting to reformat LittleFS");
    g_lfsCorruptMagic = LFS_CORRUPT_MAGIC;
    HAL_NVIC_SystemReset();
}

// Weak hook in FSCommon.cpp, called before FSBegin(). Reformats if the last boot's lfs_assert() requested it.
void preFSBegin()
{
    if (g_lfsCorruptMagic != LFS_CORRUPT_MAGIC)
        return;
    g_lfsCorruptMagic = 0;
    lastLfsFormatMs = millis();
    RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_FLASH_CORRUPTION_UNRECOVERABLE);
    fsFormat();
    LOG_INFO("LittleFS format complete; restoring default settings");
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

// ─── Fault handling & recovery ────────────────────────────────────────────────

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

// By default __assert_func uses fiprintf which pulls in stdio.
extern "C" void __wrap___assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
    debug_printf("assert: %s:%d in %s: %s\r\n", file, line, func, failedexpr);
    HAL_NVIC_SystemReset();
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

    HAL_NVIC_SystemReset();
}
