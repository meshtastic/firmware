
#include "sleep.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "screen.h"
#include "PowerFSM.h"
#include "GPS.h"
#include "main.h"

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
    DEBUG_MSG("lsEnter begin\n");
    screen.setOn(false);

    while (!service.radio.rf95.canSleep())
        delay(10); // Kinda yucky - wait until radio says say we can shutdown (finished in process sends/receives)

    gps.prepareSleep(); // abandon in-process parsing

    //if (!isUSBPowered)      // FIXME - temp hack until we can put gps in sleep mode, if we have AC when we go to sleep then leave GPS on
    //    setGPSPower(false); // kill GPS power

    DEBUG_MSG("lsEnter end\n");
}

static void lsIdle()
{
    DEBUG_MSG("lsIdle begin ls_secs=%u\n", radioConfig.preferences.ls_secs);

    uint32_t secsSlept = 0;
    esp_sleep_source_t wakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
    bool reached_ls_secs = false;

    while (!reached_ls_secs)
    {
        // Briefly come out of sleep long enough to blink the led once every few seconds
        uint32_t sleepTime = 5;

        setLed(false); // Never leave led on while in light sleep
        wakeCause = doLightSleep(sleepTime * 1000LL);
        if (wakeCause != ESP_SLEEP_WAKEUP_TIMER)
            break;

        setLed(true); // briefly turn on led
        doLightSleep(1);
        if (wakeCause != ESP_SLEEP_WAKEUP_TIMER)
            break;

        secsSlept += sleepTime;
        reached_ls_secs = secsSlept >= radioConfig.preferences.ls_secs;
    }
    setLed(false);

    if (reached_ls_secs)
    {
        // stay in LS mode but let loop check whatever it wants
        DEBUG_MSG("reached ls_secs, servicing loop()\n");
    }
    else
    {
        DEBUG_MSG("wakeCause %d\n", wakeCause);

        // Regardless of why we woke just transition to NB (and that state will handle stuff like IRQs etc)
        powerFSM.trigger(EVENT_WAKE_TIMER);
    }
}

static void lsExit()
{
    // setGPSPower(true); // restore GPS power
    gps.startLock();
}

static void nbEnter()
{
    screen.setOn(false);
    setBluetoothEnable(false);

    // FIXME - check if we already have packets for phone and immediately trigger EVENT_PACKETS_FOR_PHONE
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
State stateON(onEnter, NULL, NULL, "ON");
Fsm powerFSM(&stateDARK);

void PowerFSM_setup()
{
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_BOOT, NULL, "Boot");
    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_WAKE_TIMER, wakeForPing, "Wake timer");

    // Note we don't really use this transition, because when we wake from light sleep we _always_ transition to NB and then it handles things
    // powerFSM.add_transition(&stateLS, &stateNB, EVENT_RECEIVED_PACKET, NULL, "Received packet");

    powerFSM.add_transition(&stateNB, &stateNB, EVENT_RECEIVED_PACKET, NULL, "Received packet, resetting win wake");

    // Note we don't really use this transition, because when we wake from light sleep we _always_ transition to NB and then it handles things
    // powerFSM.add_transition(&stateLS, &stateON, EVENT_PRESS, NULL, "Press");

    powerFSM.add_transition(&stateNB, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateON, &stateON, EVENT_PRESS, screenPress, "Press"); // reenter On to restart our timers

    powerFSM.add_transition(&stateDARK, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");
    powerFSM.add_transition(&stateON, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");

    powerFSM.add_transition(&stateNB, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update");
    powerFSM.add_transition(&stateON, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update");

    powerFSM.add_transition(&stateLS, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
    powerFSM.add_transition(&stateON, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text"); // restarts the sleep timer

    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_CONTACT_FROM_PHONE, NULL, "Contact from phone");

    powerFSM.add_transition(&stateNB, &stateDARK, EVENT_PACKET_FOR_PHONE, NULL, "Packet for phone");

    powerFSM.add_timed_transition(&stateON, &stateDARK, radioConfig.preferences.screen_on_secs * 1000, NULL, "Screen-on timeout");

    powerFSM.add_timed_transition(&stateDARK, &stateNB, radioConfig.preferences.phone_timeout_secs * 1000, NULL, "Phone timeout");

    powerFSM.add_timed_transition(&stateNB, &stateLS, radioConfig.preferences.min_wake_secs * 1000, NULL, "Min wake timeout");

    powerFSM.add_timed_transition(&stateDARK, &stateLS, radioConfig.preferences.wait_bluetooth_secs * 1000, NULL, "Bluetooth timeout");

    powerFSM.add_timed_transition(&stateLS, &stateSDS, radioConfig.preferences.mesh_sds_timeout_secs * 1000, NULL, "mesh timeout");
    // removing for now, because some users don't even have phones
    // powerFSM.add_timed_transition(&stateLS, &stateSDS, radioConfig.preferences.phone_sds_timeout_sec * 1000, NULL, "phone timeout");

    powerFSM.run_machine(); // run one interation of the state machine, so we run our on enter tasks for the initial DARK state
}