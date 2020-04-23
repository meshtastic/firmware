#include <assert.h>
#include <ble_gap.h>
#include <memory.h>
#include <nrf52840.h>

// #define USE_SOFTDEVICE

static inline void debugger_break(void)
{
    __asm volatile("bkpt #0x01\n\t"
                   "mov pc, lr\n\t");
}

// handle standard gcc assert failures
void __attribute__((noreturn)) __assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
    debugger_break();
    while (1)
        ;
}

void getMacAddr(uint8_t *dmac)
{
    ble_gap_addr_t addr;

#ifdef USE_SOFTDEVICE
    uint32_t res = sd_ble_gap_addr_get(&addr);
    assert(res == NRF_SUCCESS);
    memcpy(dmac, addr.addr, 6);
#else
    // FIXME - byte order might be wrong and high bits might be wrong
    memcpy(dmac, (const void *)NRF_FICR->DEVICEADDR, 6);
#endif
}