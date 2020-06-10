#pragma once

#include <Fsm.h>

// See sw-design.md for documentation

#define EVENT_PRESS 1
#define EVENT_WAKE_TIMER 2
#define EVENT_RECEIVED_PACKET 3
#define EVENT_PACKET_FOR_PHONE 4
#define EVENT_RECEIVED_TEXT_MSG 5
// #define EVENT_BOOT 6 // now done with a timed transition
#define EVENT_BLUETOOTH_PAIR 7
#define EVENT_NODEDB_UPDATED 8     // NodeDB has a big enough change that we think you should turn on the screen
#define EVENT_CONTACT_FROM_PHONE 9 // the phone just talked to us over bluetooth
#define EVENT_LOW_BATTERY 10 // Battery is critically low, go to sleep
#define EVENT_SERIAL_CONNECTED 11
#define EVENT_SERIAL_DISCONNECTED 12

extern Fsm powerFSM;

void PowerFSM_setup();
