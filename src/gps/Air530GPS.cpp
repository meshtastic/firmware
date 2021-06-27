#include "configuration.h"
#include "Air530GPS.h"
#include <assert.h>

/*
Helpful translations from the Air530 GPS datasheet

Sat acquision mode
捕获电流值@3.3v 42.6 mA

sat tracking mode
跟踪电流值@3.3v 36.7 mA

Low power mode
低功耗模式@3.3V 0.85 mA
(发送指令:$PGKC051,0)

Super low power mode
超低功耗模式@3.3V 31 uA
(发送指令:$PGKC105,4)

To exit sleep use WAKE pin

Commands to enter sleep
6、Command: 105
进入周期性低功耗模式
Arguments:

Arg1: “0”,正常运行模式 (normal mode)
“1”,周期超低功耗跟踪模式,需要拉高 WAKE 来唤醒 (periodic low power tracking mode - keeps sat positions, use wake to wake up)
“2”,周期低功耗模式 (periodic low power mode)
“4”,直接进入超低功耗跟踪模式,需要拉高 WAKE 来唤醒 (super low power consumption mode immediately, need WAKE to resume)
“8”,自动低功耗模式,可以通过串口唤醒 (automatic low power mode, wake by sending characters to serial port)
“9”, 自动超低功耗跟踪模式,需要拉高 WAKE 来唤醒 (automatic low power tracking when possible, need wake pin to resume)

(Arg 2 & 3 only valid if Arg1 is "1" or "2")
Arg2:运行时间(毫秒),在 Arg1 为 1、2 的周期模式下,此参数起作用
ON time in msecs

Arg3:睡眠时间(毫秒),在 Arg1 为 1、2 的周期模式下,此参数起作用
Sleep time in msecs

Example:
$PGKC105,8*3F<CR><LF>
This will set automatic low power mode with waking when we send chars to the serial port.  Possibly do this as soon as we get a
new location.  When we wake again in a minute we send a character to wake up.

*/


void Air530GPS::sendCommand(const char *cmd) {
    uint8_t sum = 0;

    // Skip the $
    assert(cmd[0] == '$');
    const char *p = cmd + 1;
    while(*p)
        sum ^= *p++;

    assert(_serial_gps);
   
    _serial_gps->write(cmd);
    _serial_gps->printf("*%02x\r\n", sum);

    // DEBUG_MSG("xsum %02x\n", sum);
}

void Air530GPS::sleep() {
    NMEAGPS::sleep();
#ifdef PIN_GPS_WAKE
    sendCommand("$PGKC105,4");
#endif
}

/// wake the GPS into normal operation mode
void Air530GPS::wake()
{
#if 1
    NMEAGPS::wake();
#else
    // For power testing - keep GPS sleeping forever
    sleep();
#endif
}