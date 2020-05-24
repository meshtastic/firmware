#include "configuration.h"
#include <core_cm4.h>

// Based on reading/modifying https://blog.feabhas.com/2013/02/developing-a-generic-hard-fault-handler-for-arm-cortex-m3cortex-m4/

enum { r0, r1, r2, r3, r12, lr, pc, psr };

// Per http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihcfefj.html
static void printUsageErrorMsg(uint32_t cfsr)
{
    DEBUG_MSG("Usage fault: ");
    cfsr >>= SCB_CFSR_USGFAULTSR_Pos; // right shift to lsb
    if ((cfsr & (1 << 9)) != 0)
        DEBUG_MSG("Divide by zero\n");
    if ((cfsr & (1 << 8)) != 0)
        DEBUG_MSG("Unaligned\n");
}

static void printBusErrorMsg(uint32_t cfsr)
{
    DEBUG_MSG("Usage fault: ");
    cfsr >>= SCB_CFSR_BUSFAULTSR_Pos; // right shift to lsb
    if ((cfsr & (1 << 0)) != 0)
        DEBUG_MSG("Instruction bus error\n");
    if ((cfsr & (1 << 1)) != 0)
        DEBUG_MSG("Precise data bus error\n");
    if ((cfsr & (1 << 2)) != 0)
        DEBUG_MSG("Imprecise data bus error\n");
}

static void printMemErrorMsg(uint32_t cfsr)
{
    DEBUG_MSG("Usage fault: ");
    cfsr >>= SCB_CFSR_MEMFAULTSR_Pos; // right shift to lsb
    if ((cfsr & (1 << 0)) != 0)
        DEBUG_MSG("Instruction access violation\n");
    if ((cfsr & (1 << 1)) != 0)
        DEBUG_MSG("Data access violation\n");
}

static void HardFault_Impl(uint32_t stack[])
{
    DEBUG_MSG("In Hard Fault Handler\n");
    DEBUG_MSG("SCB->HFSR = 0x%08lx\n", SCB->HFSR);

    if ((SCB->HFSR & SCB_HFSR_FORCED_Msk) != 0) {
        DEBUG_MSG("Forced Hard Fault\n");
        DEBUG_MSG("SCB->CFSR = 0x%08lx\n", SCB->CFSR);

        if ((SCB->CFSR & SCB_CFSR_USGFAULTSR_Msk) != 0) {
            printUsageErrorMsg(SCB->CFSR);
        }
        if ((SCB->CFSR & SCB_CFSR_BUSFAULTSR_Msk) != 0) {
            printBusErrorMsg(SCB->CFSR);
        }
        if ((SCB->CFSR & SCB_CFSR_MEMFAULTSR_Msk) != 0) {
            printMemErrorMsg(SCB->CFSR);
        }

        DEBUG_MSG("r0  = 0x%08lx\n", stack[r0]);
        DEBUG_MSG("r1  = 0x%08lx\n", stack[r1]);
        DEBUG_MSG("r2  = 0x%08lx\n", stack[r2]);
        DEBUG_MSG("r3  = 0x%08lx\n", stack[r3]);
        DEBUG_MSG("r12 = 0x%08lx\n", stack[r12]);
        DEBUG_MSG("lr  = 0x%08lx\n", stack[lr]);
        DEBUG_MSG("pc  = 0x%08lx\n", stack[pc]);
        DEBUG_MSG("psr = 0x%08lx\n", stack[psr]);
        asm volatile("bkpt #01");
        while (1)
            ;
    }
}

void HardFault_Handler(void)
{
    asm volatile(" mrs r0,msp\n"
                 " b HardFault_Impl \n");
}
