.globl HardFault_Handler
.syntax unified
.thumb

.type HardFault_Handler, %function
HardFault_Handler:
tst lr, #4
ite eq
mrseq r0, msp
mrsne r0, psp
b HardFault_Handler_C