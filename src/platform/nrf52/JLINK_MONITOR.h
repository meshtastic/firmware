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
File    : JLINK_MONITOR.h
Purpose : Header file of debug monitor for J-Link monitor mode debug on Cortex-M devices.
--------  END-OF-HEADER  ---------------------------------------------
*/

#ifndef JLINK_MONITOR_H
#define JLINK_MONITOR_H

void JLINK_MONITOR_OnExit(void);
void JLINK_MONITOR_OnEnter(void);
void JLINK_MONITOR_OnPoll(void);

#endif

/****** End Of File *************************************************/
