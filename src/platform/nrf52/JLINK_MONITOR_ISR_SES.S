/*********************************************************************
*                SEGGER Microcontroller GmbH & Co. KG                *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*       (c) 1995 - 2015  SEGGER Microcontroller GmbH & Co. KG        *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************

----------------------------------------------------------------------
File    : JLINK_MONITOR_ISR_SES.s
Purpose : Implementation of debug monitor for J-Link monitor mode
          debug on Cortex-M devices, supporting SES compiler.
--------  END-OF-HEADER  ---------------------------------------------
*/

        .name JLINK_MONITOR_ISR
        .syntax unified

        .extern JLINK_MONITOR_OnEnter
        .extern JLINK_MONITOR_OnExit
        .extern JLINK_MONITOR_OnPoll

        .global DebugMon_Handler

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#define _MON_VERSION  100  // V x.yy

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

#define _APP_SP_OFF_R0                  0x00
#define _APP_SP_OFF_R1                  0x04
#define _APP_SP_OFF_R2                  0x08
#define _APP_SP_OFF_R3                  0x0C
#define _APP_SP_OFF_R12                 0x10
#define _APP_SP_OFF_R14_LR              0x14
#define _APP_SP_OFF_PC                  0x18
#define _APP_SP_OFF_XPSR                0x1C
#define _APP_SP_OFF_S0                  0x20
#define _APP_SP_OFF_S1                  0x24
#define _APP_SP_OFF_S2                  0x28
#define _APP_SP_OFF_S3                  0x2C
#define _APP_SP_OFF_S4                  0x30
#define _APP_SP_OFF_S5                  0x34
#define _APP_SP_OFF_S6                  0x38
#define _APP_SP_OFF_S7                  0x3C
#define _APP_SP_OFF_S8                  0x40
#define _APP_SP_OFF_S9                  0x44
#define _APP_SP_OFF_S10                 0x48
#define _APP_SP_OFF_S11                 0x4C
#define _APP_SP_OFF_S12                 0x50
#define _APP_SP_OFF_S13                 0x54
#define _APP_SP_OFF_S14                 0x58
#define _APP_SP_OFF_S15                 0x5C
#define _APP_SP_OFF_FPSCR               0x60

#define _NUM_BYTES_BASIC_STACKFRAME     32
#define _NUM_BYTES_EXTENDED_STACKFRAME  72

#define _SYSTEM_DCRDR_OFF               0x00
#define _SYSTEM_DEMCR_OFF               0x04

#define _SYSTEM_DHCSR                   0xE000EDF0 // Debug Halting Control and Status Register (DHCSR)
#define _SYSTEM_DCRSR                   0xE000EDF4 // Debug Core Register Selector Register (DCRSR)
#define _SYSTEM_DCRDR                   0xE000EDF8 // Debug Core Register Data Register (DCRDR)
#define _SYSTEM_DEMCR                   0xE000EDFC // Debug Exception and Monitor Control Register (DEMCR)

#define _SYSTEM_FPCCR                   0xE000EF34 // Floating-Point Context Control Register (FPCCR)
#define _SYSTEM_FPCAR                   0xE000EF38 // Floating-Point Context Address Register (FPCAR)
#define _SYSTEM_FPDSCR                  0xE000EF3C // Floating-Point Default Status Control Register (FPDSCR)
#define _SYSTEM_MVFR0                   0xE000EF40 // Media and FP Feature Register 0 (MVFR0)
#define _SYSTEM_MVFR1                   0xE000EF44 // Media and FP Feature Register 1 (MVFR1)

/*
* Defines for determining if the current debug config supports FPU registers
* For some compilers like IAR EWARM when disabling the FPU in the compiler settings an error is thrown when 
*/
#ifdef __FPU_PRESENT
  #if __FPU_PRESENT
   #define _HAS_FPU_REGS  1
  #else
   #define _HAS_FPU_REGS  0
  #endif
#else
  #define _HAS_FPU_REGS  0
#endif

/********************************************************************* 
* 
*       Signature of monitor
* 
*  Function description 
*    Needed for targets where also a boot ROM is present that possibly specifies a vector table with a valid debug monitor exception entry
*/ 
        .section .text, "ax"

        //
        // JLINKMONHANDLER
        //
        .byte 0x4A
        .byte 0x4C
        .byte 0x49
        .byte 0x4E
        .byte 0x4B
        .byte 0x4D
        .byte 0x4F
        .byte 0x4E
        .byte 0x48
        .byte 0x41
        .byte 0x4E
        .byte 0x44
        .byte 0x4C
        .byte 0x45
        .byte 0x52
        .byte 0x00      // Align to 8-bytes

/********************************************************************* 
* 
*       DebugMon_Handler()
* 
*  Function description 
*    Debug monitor handler. CPU enters this handler in case a "halt" request is made from the debugger.
*    This handler is also responsible for handling commands that are sent by the debugger.
*
*  Notes 
*    This is actually the ISR for the debug interrupt (exception no. 12)
*/ 
        .thumb_func

DebugMon_Handler:
        /*
        General procedure:
        DCRDR is used as communication register
        DEMCR[19] is used as ready flag
        For the command J-Link sends to the monitor: DCRDR[7:0] == Cmd, DCRDR[31:8] == ParamData

        1) Monitor sets DEMCR[19] whenever it is ready to receive new commands/data
           DEMCR[19] is initially set on debug monitor entry
        2) J-Link will clear once it has placed conmmand/data in DCRDR for J-Link
        3) Monitor will wait for DEMCR[19] to be cleared
        4) Monitor will process command (May cause additional data transfers etc., depends on command
        5) No restart-CPU command? => Back to 2), Otherwise => 6)
        6) Monitor will clear DEMCR[19] 19 to indicate that it is no longer ready
        */
        PUSH     {LR}
        BL       JLINK_MONITOR_OnEnter
        POP      {LR}
        LDR.N    R3,_AddrDCRDR                             // 0xe000edf8 == _SYSTEM_DCRDR
        B.N      _IndicateMonReady
_WaitProbeReadIndicateMonRdy:                              // while(_SYSTEM_DEMCR & (1uL << 19));  => Wait until J-Link has read item
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]              // _SYSTEM_DEMCR
        LSLS     R0,R0,#+12
        BMI.N    _WaitProbeReadIndicateMonRdy
_IndicateMonReady:
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]              // _SYSTEM_DEMCR |= (1uL << 19);  => Set MON_REQ bit, so J-Link knows monitor is ready to receive commands
        ORR      R0,R0,#0x80000
        STR      R0,[R3, #+_SYSTEM_DEMCR_OFF]
        /*
        During command loop:
        R0  = Tmp
        R1  = Tmp
        R2  = Tmp
        R3  = &_SYSTEM_DCRDR  (allows also access to DEMCR with offset)
        R12 = Tmp

        Outside command loop R0-R3 and R12 may be overwritten by MONITOR_OnPoll()
        */
_WaitForJLinkCmd:                                          // do {
        PUSH     {LR}
        BL       JLINK_MONITOR_OnPoll
        POP      {LR}
        LDR.N    R3,_AddrDCRDR                             // 0xe000edf8 == _SYSTEM_DCRDR
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]
        LSRS     R0,R0,#+20                                // DEMCR[19] -> Carry Clear? => J-Link has placed command for us
        BCS     _WaitForJLinkCmd
        /*
        Perform command
        Command is placed by J-Link in DCRDR[7:0] and additional parameter data is stored in DCRDR[31:8]
        J-Link clears DEMCR[19] to indicate that it placed a command/data or read data
        Monitor sets DEMCR[19] to indicate that it placed data or read data / is ready for a new command
        Setting DEMCR[19] indicates "monitor ready for new command / data" and also indicates: "data has been placed in DCRDR by monitor, for J-Link"
        Therefore it is responsibility of the commands to respond to the commands accordingly

        Commands for debug monitor
        Commands must not exceed 0xFF (255) as we only defined 8-bits for command-part. Higher 24-bits are parameter info for current command

        Protocol for different commands:
          J-Link: Cmd -> DCRDR,  DEMCR[19] -> 0 => Cmd placed by probe
        */
        LDR      R0,[R3, #+_SYSTEM_DCRDR_OFF]              // ParamInfo = _SYSTEM_DCRDR
        LSRS     R1,R0,#+8                                 // ParamInfo >>= 8
        LSLS     R0,R0,#+24
        LSRS     R0,R0,#+24                                // Cmd = ParamInfo & 0xFF
        //
        // switch (Cmd)
        //
        CMP      R0,#+0
        BEQ.N    _HandleGetMonVersion                      // case _MON_CMD_GET_MONITOR_VERSION
        CMP      R0,#+2
        BEQ.N    _HandleReadReg                            // case _MON_CMD_READ_REG
        BCC.N    _HandleRestartCPU                         // case _MON_CMD_RESTART_CPU
        CMP      R0,#+3
        BEQ.N    _HandleWriteReg_Veneer                    // case _MON_CMD_WRITE_REG
        B.N      _IndicateMonReady                         // default : while (1);
        /*
        Return
        _MON_CMD_RESTART_CPU
          CPU:                   DEMCR[19] -> 0 => Monitor no longer ready
        */
_HandleRestartCPU:
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]              // _SYSTEM_DEMCR &= ~(1uL << 19);  => Clear MON_REQ to indicate that monitor is no longer active
        BIC      R0,R0,#0x80000
        STR      R0,[R3, #+_SYSTEM_DEMCR_OFF]
        PUSH     {LR}
        BL       JLINK_MONITOR_OnExit
        POP      {PC}
        //
        // Place data section here to not get in trouble with load-offsets
        //
        .section .text, "ax", %progbits
        .align 2
_AddrDCRDR:
        .long     0xE000EDF8
_AddrCPACR:
        .long     0xE000ED88

        .section .text, "ax"
        .thumb_func

;/********************************************************************* 
;* 
;*       _HandleGetMonVersion
;*
;*/
_HandleGetMonVersion:
        /*
        _MON_CMD_GET_MONITOR_VERSION
          CPU:    Data -> DCRDR, DEMCR[19] -> 1 => Data ready
          J-Link: DCRDR -> Read, DEMCR[19] -> 0 => Data read
          CPU:                   DEMCR[19] -> 1 => Mon ready
        */
        MOVS     R0,#+_MON_VERSION
        STR      R0,[R3, #+_SYSTEM_DCRDR_OFF]              // _SYSTEM_DCRDR = x
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]              // _SYSTEM_DEMCR |= (1uL << 19);  => Set MON_REQ bit, so J-Link knows monitor is ready to receive commands
        ORR      R0,R0,#0x80000
        STR      R0,[R3, #+_SYSTEM_DEMCR_OFF]              // Indicate data ready
        B        _WaitProbeReadIndicateMonRdy

/********************************************************************* 
* 
*       _HandleReadReg
*
*/
_HandleWriteReg_Veneer:
        B.N      _HandleWriteReg
_HandleReadReg:
        /*
        _MON_CMD_READ_REG
          CPU:    Data -> DCRDR, DEMCR[19] -> 1 => Data ready
          J-Link: DCRDR -> Read, DEMCR[19] -> 0 => Data read
          CPU:                   DEMCR[19] -> 1 => Mon ready


        Register indexes
        0-15: R0-R15       (13 == R13 reserved => is banked ... Has to be read as PSP / MSP. Decision has to be done by J-Link DLL side!)
        16: XPSR
        17: MSP
        18: PSP
        19: CFBP           CONTROL/FAULTMASK/BASEPRI/PRIMASK (packed into 4 bytes of word. CONTROL = CFBP[31:24], FAULTMASK = CFBP[16:23], BASEPRI = CFBP[15:8], PRIMASK = CFBP[7:0]
        20: FPSCR
        21-52: FPS0-FPS31


        Register usage when entering this "subroutine":
        R0 Cmd
        R1 ParamInfo
        R2 ---
        R3  = &_SYSTEM_DCRDR  (allows also access to DEMCR with offset)
        R12 ---

        Table B1-9 EXC_RETURN definition of exception return behavior, with FP extension
        LR           Return to     Return SP   Frame type
        ---------------------------------------------------------
        0xFFFFFFE1   Handler mode. MSP         Extended
        0xFFFFFFE9   Thread mode   MSP         Extended
        0xFFFFFFED   Thread mode   PSP         Extended
        0xFFFFFFF1   Handler mode. MSP         Basic
        0xFFFFFFF9   Thread mode   MSP         Basic
        0xFFFFFFFD   Thread mode   PSP         Basic

        So LR[2] == 1 => Return stack == PSP else MSP

        R0-R3, R12, PC, xPSR can be read from application stackpointer
        Other regs can be read directly
        */
        LSRS     R2,LR,#+3                         // Shift LR[2] into carry => Carry clear means that CPU was running on MSP
        ITE      CS
        MRSCS    R2,PSP
        MRSCC    R2,MSP
        CMP      R1,#+4                            // if (RegIndex < 4) { (R0-R3)
        BCS      _HandleReadRegR4
        LDR      R0,[R2, R1, LSL #+2]              // v = [SP + Rx * 4] (R0-R3)
        B.N      _HandleReadRegDone
_HandleReadRegR4:
        CMP      R1,#+5                          // if (RegIndex < 5) { (R4)
        BCS      _HandleReadRegR5
        MOV      R0,R4
        B.N      _HandleReadRegDone
_HandleReadRegR5:
        CMP      R1,#+6                          // if (RegIndex < 6) { (R5)
        BCS      _HandleReadRegR6
        MOV      R0,R5
        B.N      _HandleReadRegDone
_HandleReadRegR6:
        CMP      R1,#+7                          // if (RegIndex < 7) { (R6)
        BCS      _HandleReadRegR7
        MOV      R0,R6
        B.N      _HandleReadRegDone
_HandleReadRegR7:
        CMP      R1,#+8                          // if (RegIndex < 8) { (R7)
        BCS      _HandleReadRegR8
        MOV      R0,R7
        B.N      _HandleReadRegDone
_HandleReadRegR8:
        CMP      R1,#+9                          // if (RegIndex < 9) { (R8)
        BCS      _HandleReadRegR9
        MOV      R0,R8
        B.N      _HandleReadRegDone
_HandleReadRegR9:
        CMP      R1,#+10                         // if (RegIndex < 10) { (R9)
        BCS      _HandleReadRegR10
        MOV      R0,R9
        B.N      _HandleReadRegDone
_HandleReadRegR10:
        CMP      R1,#+11                         // if (RegIndex < 11) { (R10)
        BCS      _HandleReadRegR11
        MOV      R0,R10
        B.N      _HandleReadRegDone
_HandleReadRegR11:
        CMP      R1,#+12                         // if (RegIndex < 12) { (R11)
        BCS      _HandleReadRegR12
        MOV      R0,R11
        B.N      _HandleReadRegDone
_HandleReadRegR12:
        CMP      R1,#+14                         // if (RegIndex < 14) { (R12)
        BCS      _HandleReadRegR14
        LDR      R0,[R2, #+_APP_SP_OFF_R12]
        B.N      _HandleReadRegDone
_HandleReadRegR14:
        CMP      R1,#+15                         // if (RegIndex < 15) { (R14 / LR)
        BCS      _HandleReadRegR15
        LDR      R0,[R2, #+_APP_SP_OFF_R14_LR]
        B.N      _HandleReadRegDone
_HandleReadRegR15:
        CMP      R1,#+16                         // if (RegIndex < 16) { (R15 / PC)
        BCS      _HandleReadRegXPSR
        LDR      R0,[R2, #+_APP_SP_OFF_PC]
        B.N      _HandleReadRegDone
_HandleReadRegXPSR:
        CMP      R1,#+17                         // if (RegIndex < 17) { (xPSR)
        BCS      _HandleReadRegMSP
        LDR      R0,[R2, #+_APP_SP_OFF_XPSR]
        B.N      _HandleReadRegDone
_HandleReadRegMSP:
        /*
        Stackpointer is tricky because we need to get some info about the SP used in the user app, first

        Handle reading R0-R3 which can be read right from application stackpointer

        Table B1-9 EXC_RETURN definition of exception return behavior, with FP extension
        LR           Return to     Return SP   Frame type
        ---------------------------------------------------------
        0xFFFFFFE1   Handler mode. MSP         Extended
        0xFFFFFFE9   Thread mode   MSP         Extended
        0xFFFFFFED   Thread mode   PSP         Extended
        0xFFFFFFF1   Handler mode. MSP         Basic
        0xFFFFFFF9   Thread mode   MSP         Basic
        0xFFFFFFFD   Thread mode   PSP         Basic

        So LR[2] == 1 => Return stack == PSP else MSP
        Per architecture definition: Inside monitor (exception) SP = MSP

        Stack pointer handling is complicated because it is different what is pushed on the stack before entering the monitor ISR...
        Cortex-M: 8 regs
        Cortex-M + forced-stack-alignment: 8 regs + 1 dummy-word if stack was not 8-byte aligned
        Cortex-M + FPU: 8 regs + 17 FPU regs + 1 dummy-word + 1-dummy word if stack was not 8-byte aligned
        Cortex-M + FPU + lazy mode: 8 regs + 17 dummy-words + 1 dummy-word + 1-dummy word if stack was not 8-byte aligned
        */
        CMP      R1,#+18                           // if (RegIndex < 18) { (MSP)
        BCS      _HandleReadRegPSP
        MRS      R0,MSP
        LSRS     R1,LR,#+3                         // LR[2] -> Carry == 0 => CPU was running on MSP => Needs correction
        BCS      _HandleReadRegDone_Veneer         // CPU was running on PSP? => No correction necessary
_HandleSPCorrection:
        LSRS     R1,LR,#+5                         // LR[4] -> Carry == 0 => extended stack frame has been allocated. See ARM DDI0403D, B1.5.7 Stack alignment on exception entry
        ITE      CS
        ADDCS    R0,R0,#+_NUM_BYTES_BASIC_STACKFRAME
        ADDCC    R0,R0,#+_NUM_BYTES_EXTENDED_STACKFRAME
        LDR      R1,[R2, #+_APP_SP_OFF_XPSR]       // Get xPSR from application stack (R2 has been set to app stack on beginning of _HandleReadReg)
        LSRS     R1,R1,#+5                         // xPSR[9] -> Carry == 1 => Stack has been force-aligned before pushing regs. See ARM DDI0403D, B1.5.7 Stack alignment on exception entry
        IT       CS
        ADDCS    R0,R0,#+4
        B        _HandleReadRegDone
_HandleReadRegPSP:                                 // RegIndex == 18
        CMP      R1,#+19                           // if (RegIndex < 19) {
        BCS      _HandleReadRegCFBP
        MRS      R0,PSP                            // PSP is not touched by monitor
        LSRS     R1,LR,#+3                         // LR[2] -> Carry == 1 => CPU was running on PSP => Needs correction
        BCC      _HandleReadRegDone_Veneer         // CPU was running on MSP? => No correction of PSP necessary
        B        _HandleSPCorrection
_HandleReadRegCFBP:
        /*
        CFBP is a register that can only be read via debug probe and is a merger of the following regs:
        CONTROL/FAULTMASK/BASEPRI/PRIMASK (packed into 4 bytes of word. CONTROL = CFBP[31:24], FAULTMASK = CFBP[16:23], BASEPRI = CFBP[15:8], PRIMASK = CFBP[7:0]
        To keep J-Link side the same for monitor and halt mode, we also return CFBP in monitor mode
        */
        CMP      R1,#+20                           // if (RegIndex < 20) { (CFBP)
        BCS      _HandleReadRegFPU
        MOVS     R0,#+0
        MRS      R2,PRIMASK
        ORRS     R0,R2                             // Merge PRIMASK into CFBP[7:0]
        MRS      R2,BASEPRI
        LSLS     R2,R2,#+8                         // Merge BASEPRI into CFBP[15:8]
        ORRS     R0,R2
        MRS      R2,FAULTMASK
        LSLS     R2,R2,#+16                        // Merge FAULTMASK into CFBP[23:16]
        ORRS     R0,R2
        MRS      R2,CONTROL
        LSRS     R1,LR,#3                               // LR[2] -> Carry. CONTROL.SPSEL is saved to LR[2] on exception entry => ARM DDI0403D, B1.5.6 Exception entry behavior
        IT       CS                                     // As J-Link sees value of CONTROL at application time, we need reconstruct original value of CONTROL
        ORRCS    R2,R2,#+2                              // CONTROL.SPSEL (CONTROL[1]) == 0 inside monitor
        LSRS     R1,LR,#+5                              // LR[4] == NOT(CONTROL.FPCA)  -> Carry
        ITE      CS                                     // Merge original value of FPCA (CONTROL[2]) into read data
        BICCS    R2,R2,#+0x04                           // Remember LR contains NOT(CONTROL)
        ORRCC    R2,R2,#+0x04
        LSLS     R2,R2,#+24
        ORRS     R0,R2
        B.N      _HandleReadRegDone
_HandleReadRegFPU:
#if _HAS_FPU_REGS
        CMP      R1,#+53                               // if (RegIndex < 53) { (20 (FPSCR), 21-52 FPS0-FPS31)
        BCS      _HandleReadRegDone_Veneer
        /*
        Read Coprocessor Access Control Register (CPACR) to check if CP10 and CP11 are enabled
        If not, access to floating point is not possible
        CPACR[21:20] == CP10 enable. 0b01 = Privileged access only. 0b11 = Full access. Other = reserved
        CPACR[23:22] == CP11 enable. 0b01 = Privileged access only. 0b11 = Full access. Other = reserved 
        */
        LDR      R0,_AddrCPACR
        LDR      R0,[R0]
        LSLS     R0,R0,#+8
        LSRS     R0,R0,#+28
        CMP      R0,#+0xF
        BEQ      _HandleReadRegFPU_Allowed
        CMP      R0,#+0x5
        BNE      _HandleReadRegDone_Veneer
_HandleReadRegFPU_Allowed:
        CMP      R1,#+21                                  // if (RegIndex < 21) (20 == FPSCR)
        BCS      _HandleReadRegFPS0_FPS31
        LSRS     R0,LR,#+5                                // CONTROL[2] == FPCA => NOT(FPCA) saved to LR[4]. LR[4] == 0 => Extended stack frame, so FPU regs possibly on stack
        BCS      _HandleReadFPSCRLazyMode                 // Remember: NOT(FPCA) is stored to LR. == 0 means: Extended stack frame
        LDR      R0,=_SYSTEM_FPCCR
        LDR      R0,[R0]
        LSLS     R0,R0,#+2                                // FPCCR[30] -> Carry == 1 indicates if lazy mode is active, so space on stack is reserved but FPU registers are not saved on stack
        BCS      _HandleReadFPSCRLazyMode
        LDR      R0,[R2, #+_APP_SP_OFF_FPSCR]
        B        _HandleReadRegDone
_HandleReadFPSCRLazyMode:
        VMRS     R0,FPSCR
        B        _HandleReadRegDone
_HandleReadRegFPS0_FPS31:                                 // RegIndex == 21-52
        LSRS     R0,LR,#+5                                // CONTROL[2] == FPCA => NOT(FPCA) saved to LR[4]. LR[4] == 0 => Extended stack frame, so FPU regs possibly on stack
        BCS      _HandleReadFPS0_FPS31LazyMode            // Remember: NOT(FPCA) is stored to LR. == 0 means: Extended stack frame
        LDR      R0,=_SYSTEM_FPCCR
        LDR      R0,[R0]
        LSLS     R0,R0,#+2                                // FPCCR[30] -> Carry == 1 indicates if lazy mode is active, so space on stack is reserved but FPU registers are not saved on stack
        BCS      _HandleReadFPS0_FPS31LazyMode
        SUBS     R1,#+21                                  // Convert absolute reg index into rel. one
        LSLS     R1,R1,#+2                                // RegIndex to position on stack
        ADDS     R1,#+_APP_SP_OFF_S0
        LDR      R0,[R2, R1]
_HandleReadRegDone_Veneer:
        B        _HandleReadRegDone
_HandleReadFPS0_FPS31LazyMode:
        SUBS R1,#+20                                   // convert abs. RegIndex into rel. one
        MOVS R0,#+6
        MULS R1,R0,R1
        LDR R0,=_HandleReadRegUnknown
        SUB      R0,R0,R1                              // _HandleReadRegUnknown - 6 * ((RegIndex - 21) + 1)
        ORR      R0,R0,#1                              // Thumb bit needs to be set in DestAddr
        BX       R0
        //
        // Table for reading FPS0-FPS31
        //
        VMOV     R0,S31                                // v = FPSx
        B        _HandleReadRegDone
        VMOV     R0,S30  
        B        _HandleReadRegDone
        VMOV     R0,S29  
        B        _HandleReadRegDone
        VMOV     R0,S28  
        B        _HandleReadRegDone
        VMOV     R0,S27  
        B        _HandleReadRegDone
        VMOV     R0,S26  
        B        _HandleReadRegDone
        VMOV     R0,S25  
        B        _HandleReadRegDone
        VMOV     R0,S24  
        B        _HandleReadRegDone
        VMOV     R0,S23  
        B        _HandleReadRegDone
        VMOV     R0,S22  
        B        _HandleReadRegDone
        VMOV     R0,S21
        B        _HandleReadRegDone
        VMOV     R0,S20  
        B        _HandleReadRegDone
        VMOV     R0,S19  
        B        _HandleReadRegDone
        VMOV     R0,S18  
        B        _HandleReadRegDone
        VMOV     R0,S17  
        B        _HandleReadRegDone
        VMOV     R0,S16  
        B        _HandleReadRegDone
        VMOV     R0,S15  
        B        _HandleReadRegDone
        VMOV     R0,S14  
        B        _HandleReadRegDone
        VMOV     R0,S13  
        B        _HandleReadRegDone
        VMOV     R0,S12  
        B        _HandleReadRegDone
        VMOV     R0,S11
        B        _HandleReadRegDone
        VMOV     R0,S10  
        B        _HandleReadRegDone
        VMOV     R0,S9  
        B        _HandleReadRegDone
        VMOV     R0,S8  
        B        _HandleReadRegDone
        VMOV     R0,S7  
        B        _HandleReadRegDone
        VMOV     R0,S6  
        B        _HandleReadRegDone
        VMOV     R0,S5  
        B        _HandleReadRegDone
        VMOV     R0,S4  
        B        _HandleReadRegDone
        VMOV     R0,S3  
        B        _HandleReadRegDone
        VMOV     R0,S2  
        B        _HandleReadRegDone
        VMOV     R0,S1
        B        _HandleReadRegDone
        VMOV     R0,S0  
        B        _HandleReadRegDone
#else
        B        _HandleReadRegUnknown
_HandleReadRegDone_Veneer:
        B        _HandleReadRegDone
#endif
_HandleReadRegUnknown:
        MOVS     R0,#+0                                // v = 0
        B.N      _HandleReadRegDone
_HandleReadRegDone:

        // Send register content to J-Link and wait until J-Link has read the data

        STR      R0,[R3, #+_SYSTEM_DCRDR_OFF]          // DCRDR = v;
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]          // _SYSTEM_DEMCR |= (1uL << 19);  => Set MON_REQ bit, so J-Link knows monitor is ready to receive commands
        ORR      R0,R0,#0x80000
        STR      R0,[R3, #+_SYSTEM_DEMCR_OFF]          // Indicate data ready
        B        _WaitProbeReadIndicateMonRdy

        // Data section for register addresses

_HandleWriteReg:
        /*
        _MON_CMD_WRITE_REG
          CPU:                   DEMCR[19] -> 1 => Mon ready
          J-Link: Data -> DCRDR, DEMCR[19] -> 0 => Data placed by probe
          CPU:    DCRDR -> Read, Process command, DEMCR[19] -> 1 => Data read & mon ready

        Register indexes
        0-15: R0-R15       (13 == R13 reserved => is banked ... Has to be read as PSP / MSP. Decision has to be done by J-Link DLL side!)
        16: XPSR
        17: MSP
        18: PSP
        19: CFBP           CONTROL/FAULTMASK/BASEPRI/PRIMASK (packed into 4 bytes of word. CONTROL = CFBP[31:24], FAULTMASK = CFBP[16:23], BASEPRI = CFBP[15:8], PRIMASK = CFBP[7:0]
        20: FPSCR
        21-52: FPS0-FPS31


        Register usage when entering this "subroutine":
        R0 Cmd
        R1 ParamInfo
        R2 ---
        R3  = &_SYSTEM_DCRDR  (allows also access to DEMCR with offset)
        R12 ---

        Table B1-9 EXC_RETURN definition of exception return behavior, with FP extension
        LR           Return to     Return SP   Frame type
        ---------------------------------------------------------
        0xFFFFFFE1   Handler mode. MSP         Extended
        0xFFFFFFE9   Thread mode   MSP         Extended
        0xFFFFFFED   Thread mode   PSP         Extended
        0xFFFFFFF1   Handler mode. MSP         Basic
        0xFFFFFFF9   Thread mode   MSP         Basic
        0xFFFFFFFD   Thread mode   PSP         Basic

        So LR[2] == 1 => Return stack == PSP else MSP

        R0-R3, R12, PC, xPSR can be written via application stackpointer
        Other regs can be written directly


        Read register data from J-Link into R0
        */
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]              // _SYSTEM_DEMCR |= (1uL << 19);  => Monitor is ready to receive register data
        ORR      R0,R0,#0x80000
        STR      R0,[R3, #+_SYSTEM_DEMCR_OFF]
_HandleWRegWaitUntilDataRecv:
        LDR      R0,[R3, #+_SYSTEM_DEMCR_OFF]
        LSLS     R0,R0,#+12
        BMI.N    _HandleWRegWaitUntilDataRecv              // DEMCR[19] == 0 => J-Link has placed new data for us
        LDR      R0,[R3, #+_SYSTEM_DCRDR_OFF]              // Get register data
        //
        // Determine application SP
        //
        LSRS     R2,LR,#+3                         // Shift LR[2] into carry => Carry clear means that CPU was running on MSP
        ITE      CS
        MRSCS    R2,PSP
        MRSCC    R2,MSP
        CMP      R1,#+4                            // if (RegIndex < 4) { (R0-R3)
        BCS      _HandleWriteRegR4
        STR      R0,[R2, R1, LSL #+2]              // v = [SP + Rx * 4] (R0-R3)
        B.N      _HandleWriteRegDone
_HandleWriteRegR4:
        CMP      R1,#+5                          // if (RegIndex < 5) { (R4)
        BCS      _HandleWriteRegR5
        MOV      R4,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR5:
        CMP      R1,#+6                          // if (RegIndex < 6) { (R5)
        BCS      _HandleWriteRegR6
        MOV      R5,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR6:
        CMP      R1,#+7                          // if (RegIndex < 7) { (R6)
        BCS      _HandleWriteRegR7
        MOV      R6,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR7:
        CMP      R1,#+8                          // if (RegIndex < 8) { (R7)
        BCS      _HandleWriteRegR8
        MOV      R7,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR8:
        CMP      R1,#+9                          // if (RegIndex < 9) { (R8)
        BCS      _HandleWriteRegR9
        MOV      R8,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR9:
        CMP      R1,#+10                         // if (RegIndex < 10) { (R9)
        BCS      _HandleWriteRegR10
        MOV      R9,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR10:
        CMP      R1,#+11                         // if (RegIndex < 11) { (R10)
        BCS      _HandleWriteRegR11
        MOV      R10,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR11:
        CMP      R1,#+12                         // if (RegIndex < 12) { (R11)
        BCS      _HandleWriteRegR12
        MOV      R11,R0
        B.N      _HandleWriteRegDone
_HandleWriteRegR12:
        CMP      R1,#+14                         // if (RegIndex < 14) { (R12)
        BCS      _HandleWriteRegR14
        STR      R0,[R2, #+_APP_SP_OFF_R12]
        B.N      _HandleWriteRegDone
_HandleWriteRegR14:
        CMP      R1,#+15                         // if (RegIndex < 15) { (R14 / LR)
        BCS      _HandleWriteRegR15
        STR      R0,[R2, #+_APP_SP_OFF_R14_LR]
        B.N      _HandleWriteRegDone
_HandleWriteRegR15:
        CMP      R1,#+16                         // if (RegIndex < 16) { (R15 / PC)
        BCS      _HandleWriteRegXPSR
        STR      R0,[R2, #+_APP_SP_OFF_PC]
        B.N      _HandleWriteRegDone
_HandleWriteRegXPSR:
        CMP      R1,#+17                         // if (RegIndex < 17) { (xPSR)
        BCS      _HandleWriteRegMSP
        STR      R0,[R2, #+_APP_SP_OFF_XPSR]
        B.N      _HandleWriteRegDone
_HandleWriteRegMSP:
        //
        // For now, SP cannot be modified because it is needed to jump back from monitor mode
        //
        CMP      R1,#+18                            // if (RegIndex < 18) { (MSP)
        BCS      _HandleWriteRegPSP
        B.N      _HandleWriteRegDone
_HandleWriteRegPSP:                                 // RegIndex == 18
        CMP      R1,#+19                            // if (RegIndex < 19) {
        BCS      _HandleWriteRegCFBP
        B.N      _HandleWriteRegDone
_HandleWriteRegCFBP:
        /*
        CFBP is a register that can only be read via debug probe and is a merger of the following regs:
        CONTROL/FAULTMASK/BASEPRI/PRIMASK (packed into 4 bytes of word. CONTROL = CFBP[31:24], FAULTMASK = CFBP[16:23], BASEPRI = CFBP[15:8], PRIMASK = CFBP[7:0]
        To keep J-Link side the same for monitor and halt mode, we also return CFBP in monitor mode
        */
        CMP      R1,#+20                                // if (RegIndex < 20) { (CFBP)
        BCS      _HandleWriteRegFPU
        LSLS     R1,R0,#+24
        LSRS     R1,R1,#+24                             // Extract CFBP[7:0] => PRIMASK
        MSR      PRIMASK,R1
        LSLS     R1,R0,#+16
        LSRS     R1,R1,#+24                             // Extract CFBP[15:8] => BASEPRI
        MSR      BASEPRI,R1
        LSLS     R1,R0,#+8                              // Extract CFBP[23:16] => FAULTMASK
        LSRS     R1,R1,#+24
        MSR      FAULTMASK,R1
        LSRS     R1,R0,#+24                             // Extract CFBP[31:24] => CONTROL
        LSRS     R0,R1,#2                               // Current CONTROL[1] -> Carry
        ITE      CS                                     // Update saved CONTROL.SPSEL (CONTROL[1]). CONTROL.SPSEL is saved to LR[2] on exception entry => ARM DDI0403D, B1.5.6 Exception entry behavior
        ORRCS    LR,LR,#+4
        BICCC    LR,LR,#+4
        BIC      R1,R1,#+2                              // CONTROL.SPSEL (CONTROL[1]) == 0 inside monitor. Otherwise behavior is UNPREDICTABLE
        LSRS     R0,R1,#+3                              // New CONTROL.FPCA (CONTROL[2]) -> Carry
        ITE      CS                                     // CONTROL[2] == FPCA => NOT(FPCA) saved to LR[4]. LR[4] == 0 => Extended stack frame, so FPU regs possibly on stack
        BICCS    LR,LR,#+0x10                           // Remember: NOT(FPCA) is stored to LR. == 0 means: Extended stack frame
        ORRCC    LR,LR,#+0x10
        MRS      R0,CONTROL
        LSRS     R0,R0,#+3                              // CONTROL[2] -> Carry
        ITE      CS                                     // Preserve original value of current CONTROL[2]
        ORRCS    R1,R1,#+0x04
        BICCC    R1,R1,#+0x04
        MSR      CONTROL,R1
        ISB                                             // Necessary after writing to CONTROL, see ARM DDI0403D, B1.4.4 The special-purpose CONTROL register
        B.N      _HandleWriteRegDone
_HandleWriteRegFPU:
#if _HAS_FPU_REGS
        CMP      R1,#+53                                // if (RegIndex < 53) { (20 (FPSCR), 21-52 FPS0-FPS31)
        BCS      _HandleWriteRegDone_Veneer
        /*
        Read Coprocessor Access Control Register (CPACR) to check if CP10 and CP11 are enabled
        If not, access to floating point is not possible
        CPACR[21:20] == CP10 enable. 0b01 = Privileged access only. 0b11 = Full access. Other = reserved
        CPACR[23:22] == CP11 enable. 0b01 = Privileged access only. 0b11 = Full access. Other = reserved 
        */
        MOV      R12,R0                                  // Save register data
        LDR      R0,_AddrCPACR
        LDR      R0,[R0]
        LSLS     R0,R0,#+8
        LSRS     R0,R0,#+28
        CMP      R0,#+0xF
        BEQ      _HandleWriteRegFPU_Allowed
        CMP      R0,#+0x5
        BNE      _HandleWriteRegDone_Veneer
_HandleWriteRegFPU_Allowed:
        CMP      R1,#+21                                  // if (RegIndex < 21) (20 == FPSCR)
        BCS      _HandleWriteRegFPS0_FPS31
        LSRS     R0,LR,#+5                                // CONTROL[2] == FPCA => NOT(FPCA) saved to LR[4]. LR[4] == 0 => Extended stack frame, so FPU regs possibly on stack
        BCS      _HandleWriteFPSCRLazyMode                // Remember: NOT(FPCA) is stored to LR. == 0 means: Extended stack frame
        LDR      R0,=_SYSTEM_FPCCR
        LDR      R0,[R0]
        LSLS     R0,R0,#+2                                // FPCCR[30] -> Carry == 1 indicates if lazy mode is active, so space on stack is reserved but FPU registers are not saved on stack
        BCS      _HandleWriteFPSCRLazyMode
        STR      R12,[R2, #+_APP_SP_OFF_FPSCR]
        B        _HandleWriteRegDone
_HandleWriteFPSCRLazyMode:
        VMSR     FPSCR,R12
        B        _HandleWriteRegDone
_HandleWriteRegFPS0_FPS31:                                // RegIndex == 21-52
        LDR      R0,=_SYSTEM_FPCCR
        LDR      R0,[R0]
        LSLS     R0,R0,#+2                                // FPCCR[30] -> Carry == 1 indicates if lazy mode is active, so space on stack is reserved but FPU registers are not saved on stack
        BCS      _HandleWriteFPS0_FPS31LazyMode
        LSRS     R0,LR,#+5                                // CONTROL[2] == FPCA => NOT(FPCA) saved to LR[4]. LR[4] == 0 => Extended stack frame, so FPU regs possibly on stack
        BCS     _HandleWriteFPS0_FPS31LazyMode            // Remember: NOT(FPCA) is stored to LR. == 0 means: Extended stack frame
        SUBS     R1,#+21                                  // Convert absolute reg index into rel. one
        LSLS     R1,R1,#+2                                // RegIndex to position on stack
        ADDS     R1,#+_APP_SP_OFF_S0
        STR      R12,[R2, R1]
_HandleWriteRegDone_Veneer:
        B        _HandleWriteRegDone
_HandleWriteFPS0_FPS31LazyMode:
        SUBS     R1,#+20                                  // Convert abs. RegIndex into rel. one
        MOVS     R0,#+6
        MULS     R1,R0,R1
        LDR      R0,=_HandleReadRegUnknown
        SUB      R0,R0,R1                                 // _HandleReadRegUnknown - 6 * ((RegIndex - 21) + 1)
        ORR      R0,R0,#1                                 // Thumb bit needs to be set in DestAddr
        BX       R0
        //
        // Table for reading FPS0-FPS31
        //
        VMOV     S31,R12                                  // v = FPSx
        B        _HandleWriteRegDone
        VMOV     S30,R12  
        B        _HandleWriteRegDone
        VMOV     S29,R12
        B        _HandleWriteRegDone
        VMOV     S28,R12
        B        _HandleWriteRegDone
        VMOV     S27,R12  
        B        _HandleWriteRegDone
        VMOV     S26,R12  
        B        _HandleWriteRegDone
        VMOV     S25,R12  
        B        _HandleWriteRegDone
        VMOV     S24,R12  
        B        _HandleWriteRegDone
        VMOV     S23,R12  
        B        _HandleWriteRegDone
        VMOV     S22,R12  
        B        _HandleWriteRegDone
        VMOV     S21,R12
        B        _HandleWriteRegDone
        VMOV     S20,R12  
        B        _HandleWriteRegDone
        VMOV     S19,R12  
        B        _HandleWriteRegDone
        VMOV     S18,R12  
        B        _HandleWriteRegDone
        VMOV     S17,R12  
        B        _HandleWriteRegDone
        VMOV     S16,R12  
        B        _HandleWriteRegDone
        VMOV     S15,R12  
        B        _HandleWriteRegDone
        VMOV     S14,R12  
        B        _HandleWriteRegDone
        VMOV     S13,R12  
        B        _HandleWriteRegDone
        VMOV     S12,R12  
        B        _HandleWriteRegDone
        VMOV     S11,R12
        B        _HandleWriteRegDone
        VMOV     S10,R12  
        B        _HandleWriteRegDone
        VMOV     S9,R12  
        B        _HandleWriteRegDone
        VMOV     S8,R12  
        B        _HandleWriteRegDone
        VMOV     S7,R12  
        B        _HandleWriteRegDone
        VMOV     S6,R12  
        B        _HandleWriteRegDone
        VMOV     S5,R12  
        B        _HandleWriteRegDone
        VMOV     S4,R12  
        B        _HandleWriteRegDone
        VMOV     S3,R12  
        B        _HandleWriteRegDone
        VMOV     S2,R12  
        B        _HandleWriteRegDone
        VMOV     S1,R12
        B        _HandleWriteRegDone
        VMOV     S0,R12  
        B        _HandleWriteRegDone
#else
        B        _HandleWriteRegUnknown
#endif
_HandleWriteRegUnknown:
        B.N      _HandleWriteRegDone
_HandleWriteRegDone:
        B        _IndicateMonReady                     // Indicate that monitor has read data, processed command and is ready for a new one
        .end
/****** End Of File *************************************************/
