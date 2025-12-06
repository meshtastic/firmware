#include "configuration.h"
#include <core_cm4.h>

// Based on reading/modifying https://blog.feabhas.com/2013/02/developing-a-generic-hard-fault-handler-for-arm-cortex-m3cortex-m4/

enum { r0, r1, r2, r3, r12, lr, pc, psr };

// we can't use the regular LOG_DEBUG for these crash dumps because it depends on threading still being running.  Instead use the
// segger in memory tool
#define FAULT_MSG(...) SEGGER_MSG(__VA_ARGS__)

// Per http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihcfefj.html
static void printUsageErrorMsg(uint32_t cfsr)
{
    FAULT_MSG("Usage fault: ");
    cfsr >>= SCB_CFSR_USGFAULTSR_Pos; // right shift to lsb
    if ((cfsr & (1 << 9)) != 0)
        FAULT_MSG("Divide by zero\n");
    else if ((cfsr & (1 << 8)) != 0)
        FAULT_MSG("Unaligned\n");
    else if ((cfsr & (1 << 1)) != 0)
        FAULT_MSG("Invalid state\n");
    else if ((cfsr & (1 << 0)) != 0)
        FAULT_MSG("Invalid instruction\n");
    else
        FAULT_MSG("FIXME add to printUsageErrorMsg!\n");
}

static void printBusErrorMsg(uint32_t cfsr)
{
    FAULT_MSG("Bus fault: ");
    cfsr >>= SCB_CFSR_BUSFAULTSR_Pos; // right shift to lsb
    if ((cfsr & (1 << 0)) != 0)
        FAULT_MSG("Instruction bus error\n");
    if ((cfsr & (1 << 1)) != 0)
        FAULT_MSG("Precise data bus error\n");
    if ((cfsr & (1 << 2)) != 0)
        FAULT_MSG("Imprecise data bus error\n");
}

static void printMemErrorMsg(uint32_t cfsr)
{
    FAULT_MSG("Memory fault: ");
    cfsr >>= SCB_CFSR_MEMFAULTSR_Pos; // right shift to lsb
    if ((cfsr & (1 << 0)) != 0)
        FAULT_MSG("Instruction access violation\n");
    if ((cfsr & (1 << 1)) != 0)
        FAULT_MSG("Data access violation\n");
}

extern "C" void HardFault_Impl(uint32_t stack[])
{
    FAULT_MSG("Hard Fault occurred! SCB->HFSR = 0x%08lx\n", SCB->HFSR);

    if ((SCB->HFSR & SCB_HFSR_FORCED_Msk) != 0) {
        FAULT_MSG("Forced Hard Fault: SCB->CFSR = 0x%08lx\n", SCB->CFSR);

        if ((SCB->CFSR & SCB_CFSR_USGFAULTSR_Msk) != 0) {
            printUsageErrorMsg(SCB->CFSR);
        }
        if ((SCB->CFSR & SCB_CFSR_BUSFAULTSR_Msk) != 0) {
            printBusErrorMsg(SCB->CFSR);
        }
        if ((SCB->CFSR & SCB_CFSR_MEMFAULTSR_Msk) != 0) {
            printMemErrorMsg(SCB->CFSR);
        }

        FAULT_MSG("r0  = 0x%08lx\n", stack[r0]);
        FAULT_MSG("r1  = 0x%08lx\n", stack[r1]);
        FAULT_MSG("r2  = 0x%08lx\n", stack[r2]);
        FAULT_MSG("r3  = 0x%08lx\n", stack[r3]);
        FAULT_MSG("r12 = 0x%08lx\n", stack[r12]);
        FAULT_MSG("lr  = 0x%08lx\n", stack[lr]);
        FAULT_MSG("pc  = 0x%08lx\n", stack[pc]);
        FAULT_MSG("psr = 0x%08lx\n", stack[psr]);
    }

    FAULT_MSG("Done with fault report - Waiting to reboot\n");
    asm volatile("bkpt #01"); // Enter the debugger if one is connected

    // Don't spin, so that the debugger will let the user step to next instruction
    // while (1) ;
}

#ifndef INC_FREERTOS_H
// This is a generic cortex M entrypoint that doesn't assume freertos

extern "C" void HardFault_Handler(void)
{
    asm volatile(" mrs r0,msp\n"
                 " b HardFault_Impl \n");
}
#elif !defined(ARCH_NRF52)

/* The prototype shows it is a naked function - in effect this is just an
assembly function. */
extern "C" void HardFault_Handler(void) __attribute__((naked));

/* The fault handler implementation calls a function called
prvGetRegistersFromStack(). */
extern "C" void HardFault_Handler(void)
{
    __asm volatile(" tst lr, #4                                                \n"
                   " ite eq                                                    \n"
                   " mrseq r0, msp                                             \n"
                   " mrsne r0, psp                                             \n"
                   " ldr r1, [r0, #24]                                         \n"
                   " ldr r2, handler2_address_const                            \n"
                   " bx r2                                                     \n"
                   " handler2_address_const: .word HardFault_Impl    \n");
}
#endif
