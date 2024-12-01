#pragma once

#include "configuration.h"

// See sw-design.md for documentation

#define EVENT_PRESS 1
#define EVENT_WAKE_TIMER 2
// #define EVENT_RECEIVED_PACKET 3
#define EVENT_PACKET_FOR_PHONE 4
#define EVENT_RECEIVED_MSG 5
// #define EVENT_BOOT 6 // now done with a timed transition
#define EVENT_BLUETOOTH_PAIR 7
#define EVENT_NODEDB_UPDATED 8     // NodeDB has a big enough change that we think you should turn on the screen
#define EVENT_CONTACT_FROM_PHONE 9 // the phone just talked to us over bluetooth
#define EVENT_LOW_BATTERY 10       // Battery is critically low, go to sleep
#define EVENT_SERIAL_CONNECTED 11
#define EVENT_SERIAL_DISCONNECTED 12
#define EVENT_POWER_CONNECTED 13
#define EVENT_POWER_DISCONNECTED 14
#define EVENT_FIRMWARE_UPDATE 15 // We just received a new firmware update packet from the phone
#define EVENT_SHUTDOWN 16        // force a full shutdown now (not just sleep)
#define EVENT_INPUT 17           // input broker wants something, we need to wake up and enable screen

#if EXCLUDE_POWER_FSM
class FakeFsm
{
  public:
    void trigger(int event)
    {
        if (event == EVENT_SERIAL_CONNECTED) {
            serialConnected = true;
        } else if (event == EVENT_SERIAL_DISCONNECTED) {
            serialConnected = false;
        }
    };
    bool getState() { return serialConnected; };

  private:
    bool serialConnected = false;
};
extern FakeFsm powerFSM;
void PowerFSM_setup();

#else
#include <Fsm.h>
extern Fsm powerFSM;
extern State stateON, statePOWER, stateSERIAL, stateDARK;

void PowerFSM_setup();
#endif