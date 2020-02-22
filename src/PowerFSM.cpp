
#include "sleep.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "screen.h"
#include "PowerFSM.h"
#include "GPS.h"

static void sdsEnter()
{
    /*

  // Don't deepsleep if we have USB power or if the user as pressed a button recently
  // !isUSBPowered <- doesn't work yet because the axp192 isn't letting the battery fully charge when we are awake - FIXME
  if (millis() - lastPressMs > radioConfig.preferences.mesh_sds_timeout_secs)
  {
    doDeepSleep(radioConfig.preferences.sds_secs);
  }
*/

    doDeepSleep(radioConfig.preferences.sds_secs * 1000LL);
}

static void lsEnter()
{
    /*
      // while we have bluetooth on, we can't do light sleep, but once off stay in light_sleep all the time
  // we will wake from light sleep on button press or interrupt from the RF95 radio
  if (!bluetoothOn && !is_screen_on() && service.radio.rf95.canSleep() && gps.canSleep())
    doLightSleep(radioConfig.preferences.ls_secs); 
  else
  {
    delay(msecstosleep);
  } */

    gps.prepareSleep(); // abandon in-process parsing
    setGPSPower(false); // kill GPS power
    doLightSleep(radioConfig.preferences.ls_secs * 1000LL);
}

static void lsIdle()
{
    // FIXME - blink led when we occasionally wake from timer, then go back to light sleep
}

static void lsExit()
{
    setGPSPower(true); // restore GPS power
    gps.startLock();
}

static void nbEnter()
{
    setBluetoothEnable(false);
}

static void darkEnter()
{
    screen.setOn(false);
}

static void onEnter()
{
    screen.setOn(true);
    setBluetoothEnable(true);
}

static void onExit()
{
    screen.setOn(false);
}
static void wakeForPing()
{
}

static void screenPress()
{
    screen.onPress();
}

State stateSDS(sdsEnter, NULL, NULL, "SDS");
State stateLS(lsEnter, lsIdle, lsExit, "LS");
State stateNB(nbEnter, NULL, NULL, "NB");
State stateDARK(darkEnter, NULL, NULL, "DARK");
State stateON(onEnter, NULL, onExit, "ON");
Fsm powerFSM(&stateDARK);

void PowerFSM_setup()
{
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_BOOT, NULL, "Boot");
    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_WAKE_TIMER, wakeForPing, "Wake timer");
    powerFSM.add_transition(&stateLS, &stateNB, EVENT_RECEIVED_PACKET, NULL, "Received packet");
    powerFSM.add_transition(&stateNB, &stateNB, EVENT_RECEIVED_PACKET, NULL, "Received packet, resetting win wake");

    powerFSM.add_transition(&stateLS, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateON, &stateON, EVENT_PRESS, screenPress, "Press"); // reenter On to restart our timers

    powerFSM.add_transition(&stateLS, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");

    powerFSM.add_transition(&stateNB, &stateDARK, EVENT_PACKET_FOR_PHONE, NULL, "Packet for phone");

    powerFSM.add_timed_transition(&stateON, &stateDARK, radioConfig.preferences.screen_on_secs * 1000, NULL, "Screen-on timeout");

    powerFSM.add_timed_transition(&stateDARK, &stateNB, radioConfig.preferences.phone_timeout_secs * 1000, NULL, "Phone timeout");

    powerFSM.add_timed_transition(&stateNB, &stateLS, radioConfig.preferences.min_wake_secs * 1000, NULL, "Min wake timeout");

    powerFSM.add_timed_transition(&stateDARK, &stateLS, radioConfig.preferences.wait_bluetooth_secs * 1000, NULL, "Bluetooth timeout");

    powerFSM.run_machine(); // run one interation of the state machine, so we run our on enter tasks for the initial DARK state
}