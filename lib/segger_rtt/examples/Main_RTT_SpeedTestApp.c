/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*       Solutions for real time microcontroller applications         *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2018 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************

--------- END-OF-HEADER --------------------------------------------
File    : Main_RTT_SpeedTestApp.c
Purpose : Sample program for measuring RTT performance.
*/

#include "RTOS.h"
#include "BSP.h"

#include "SEGGER_RTT.h"
#include <stdio.h>

OS_STACKPTR int StackHP[128], StackLP[128];          /* Task stacks */
OS_TASK TCBHP, TCBLP;                        /* Task-control-blocks */

static void HPTask(void) {
  while (1) {
    //
    // Measure time needed for RTT output
    // Perform dummy write with 0 characters, so we know the overhead of toggling LEDs and RTT in general
    //
// Set BP here. Then start sampling on scope
    BSP_ClrLED(0);
    SEGGER_RTT_Write(0, 0, 0);
    BSP_SetLED(0);
    BSP_ClrLED(0);
    SEGGER_RTT_Write(0, "01234567890123456789012345678901234567890123456789012345678901234567890123456789\r\n", 82);
    BSP_SetLED(0);
// Set BP here. Then stop sampling on scope
    OS_Delay(200);
  }
}

static void LPTask(void) {
  while (1) {
    BSP_ToggleLED(1);
    OS_Delay (500);
  }
}

/*********************************************************************
*
*       main
*
*********************************************************************/

int main(void) {
  OS_IncDI();                      /* Initially disable interrupts  */
  OS_InitKern();                   /* Initialize OS                 */
  OS_InitHW();                     /* Initialize Hardware for OS    */
  BSP_Init();                      /* Initialize LED ports          */
  BSP_SetLED(0);
  /* You need to create at least one task before calling OS_Start() */
  OS_CREATETASK(&TCBHP, "HP Task", HPTask, 100, StackHP);
  OS_CREATETASK(&TCBLP, "LP Task", LPTask,  50, StackLP);
  OS_Start();                      /* Start multitasking            */
  return 0;
}

