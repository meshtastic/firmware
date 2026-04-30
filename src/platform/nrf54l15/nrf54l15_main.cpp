/*
 * nrf54l15_main.cpp — Zephyr entry point for Meshtastic nRF54L15 port
 *
 * Zephyr calls main() instead of Arduino's setup()/loop().
 * This file provides the main() that bootstraps the Arduino-style
 * Meshtastic application loop.
 */

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>

// Forward declarations from src/main.cpp
void setup();
void loop();

// ── Crash info saved to noinit RAM (survives soft reset) ─────────────────────
// Zephyr's arch_esf does not expose the faulting SP directly; we capture PSP
// at entry to the fatal handler (the exception-basic frame lives there) and
// store xPSR alongside PC/LR for context.
struct crash_info {
    uint32_t magic;
    uint32_t reason;
    uint32_t pc;
    uint32_t psp;  // stack pointer captured at fault entry
    uint32_t xpsr; // saved program status (flags + exception number)
    uint32_t lr;
    uint32_t cfsr; // Configurable Fault Status Register
};
static struct crash_info saved_crash __attribute__((section(".noinit")));
#define CRASH_MAGIC 0xDEADBEEF

// Override Zephyr's weak fatal handler to save crash info. On Cortex-M33 the
// handler runs on MSP; k_fatal_halt below either resets the SoC (CONFIG_RESET_
// ON_FATAL_ERROR=y) or spins forever, so what we record here will only be
// visible after a subsequent reset.
extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    saved_crash.magic = CRASH_MAGIC;
    saved_crash.reason = reason;
    // Capture the faulting thread's stack pointer before we start using the
    // handler's own stack for logging.
    uint32_t psp_at_entry;
    __asm__ volatile("mrs %0, psp" : "=r"(psp_at_entry));
    saved_crash.psp = psp_at_entry;
    if (esf) {
        saved_crash.pc = esf->basic.pc;
        saved_crash.xpsr = esf->basic.xpsr;
        saved_crash.lr = esf->basic.lr;
    }
    // Read Cortex-M33 SCB CFSR
    saved_crash.cfsr = *((volatile uint32_t *)0xE000ED28U);
    printk("[nrf54l15] FATAL reason=%u pc=0x%08x lr=0x%08x cfsr=0x%08x\n", reason, saved_crash.pc, saved_crash.lr,
           saved_crash.cfsr);

    // Walk the failing thread's stack and print any word that looks like a
    // Thumb code address (0x1000 — flash end, with the Thumb-mode low bit set).
    // The Cortex-M exception frame at PSP holds r0,r1,r2,r3,r12,lr,pc,xpsr
    // (8 words); deeper words are the caller's saved frame, which gives a
    // crude but useful poor-man's backtrace when CONFIG_DEBUG_COREDUMP is off.
    // Found the BLE-init bad_alloc → abort() chain (heap exhaustion under
    // CONFIG_BT_BUF_ACL_RX_SIZE=251) when the fault dump alone showed only
    // abort itself.  Cheap (~150 B of code) and silent until a fault.
    uint32_t psp;
    __asm__ volatile("mrs %0, psp" : "=r"(psp));
    printk("[nrf54l15] PSP=0x%08x — stack walk:\n", psp);
    const uint32_t *sp = (const uint32_t *)psp;
    for (int i = 0; i < 96; i++) {
        uint32_t v = sp[i];
        if (v >= 0x00001000 && v < 0x00080000 && (v & 1)) {
            printk("[nrf54l15]   sp[%d]=0x%08x (code)\n", i, v);
        }
    }

    k_fatal_halt(reason);
}

int main(void)
{
    uint32_t reset_cause = 0;
    hwinfo_get_reset_cause(&reset_cause);
    hwinfo_clear_reset_cause();
    printk("[nrf54l15] Reset cause: 0x%08x\n", reset_cause);

    if (saved_crash.magic == CRASH_MAGIC) {
        printk("[nrf54l15] Prev crash: reason=%u pc=0x%08x lr=0x%08x psp=0x%08x xpsr=0x%08x cfsr=0x%08x\n", saved_crash.reason,
               saved_crash.pc, saved_crash.lr, saved_crash.psp, saved_crash.xpsr, saved_crash.cfsr);
        saved_crash.magic = 0;
    }

    printk("[nrf54l15] A: main() entry\n");
    printk("[nrf54l15] B: calling setup()\n");
    setup();
    printk("[nrf54l15] C: setup() returned\n");
    while (true) {
        loop();
    }
    return 0;
}
