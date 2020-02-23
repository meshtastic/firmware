#pragma once 

#include "Fsm.h"

// See sw-design.md for documentation

#define EVENT_PRESS 1
#define EVENT_WAKE_TIMER 2
#define EVENT_RECEIVED_PACKET 3
#define EVENT_PACKET_FOR_PHONE 4
#define EVENT_RECEIVED_TEXT_MSG 5
#define EVENT_BOOT 6
#define EVENT_BLUETOOTH_PAIR 7
#define EVENT_NODEDB_UPDATED 8 // NodeDB has a big enough change that we think you should turn on the screen

extern Fsm powerFSM;

void PowerFSM_setup();