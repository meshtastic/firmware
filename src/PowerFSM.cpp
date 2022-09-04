#include "PowerFSM.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "sleep.h"
#include "target_specific.h"

/// Should we behave as if we have AC power now?
static bool isPowered()
{
    // Circumvent the battery sensing logic and assumes constant power if no battery pin or power mgmt IC
    #if !defined(BATTERY_PIN) && !defined(HAS_AXP192)
        return true;
    #endif

    bool isRouter = (config.device.role == Config_DeviceConfig_Role_Router ? 1 : 0);

    // If we are not a router and we already have AC power go to POWER state after init, otherwise go to ON
    // We assume routers might be powered all the time, but from a low current (solar) source
    bool isPowerSavingMode = config.power.is_power_saving || isRouter;

    /* To determine if we're externally powered, assumptions
        1) If we're powered up and there's no battery, we must be getting power externally. (because we'd be dead otherwise)

        2) If we detect USB power from the power management chip, we must be getting power externally.
    */
    return !isPowerSavingMode && powerStatus && (!powerStatus->getHasBattery() || powerStatus->getHasUSB());
}

static void sdsEnter()
{
    DEBUG_MSG("Enter state: SDS\n");
    // FIXME - make sure GPS and LORA radio are off first - because we want close to zero current draw
    doDeepSleep(config.power.sds_secs ? config.power.sds_secs : default_sds_secs * 1000LL);
}

extern Power *power;

static void shutdownEnter()
{
    DEBUG_MSG("Enter state: SHUTDOWN\n");
    power->shutdown();
}

#include "error.h"

static uint32_t secsSlept;

static void lsEnter()
{
    DEBUG_MSG("lsEnter begin, ls_secs=%u\n", config.power.ls_secs > 0 ? config.power.ls_secs : default_ls_secs);
    screen->setOn(false);
    secsSlept = 0; // How long have we been sleeping this time

    // DEBUG_MSG("lsEnter end\n");
}

static void lsIdle()
{
    // DEBUG_MSG("lsIdle begin ls_secs=%u\n", getPref_ls_secs());

#ifdef ARCH_ESP32

    // Do we have more sleeping to do?
    if (secsSlept < config.power.ls_secs ? config.power.ls_secs : default_ls_secs) {
        // Briefly come out of sleep long enough to blink the led once every few seconds
        uint32_t sleepTime = 30;

        // If some other service would stall sleep, don't let sleep happen yet
        if (doPreflightSleep()) {
            setLed(false); // Never leave led on while in light sleep
            esp_sleep_source_t wakeCause2 = doLightSleep(sleepTime * 1000LL);

            switch (wakeCause2) {
            case ESP_SLEEP_WAKEUP_TIMER:
                // Normal case: timer expired, we should just go back to sleep ASAP

                setLed(true);                 // briefly turn on led
                wakeCause2 = doLightSleep(1); // leave led on for 1ms

                secsSlept += sleepTime;
                // DEBUG_MSG("sleeping, flash led!\n");
                break;

            case ESP_SLEEP_WAKEUP_UART:
                // Not currently used (because uart triggers in hw have problems)
                powerFSM.trigger(EVENT_SERIAL_CONNECTED);
                break;

            default:
                // We woke for some other reason (button press, device interrupt)
                // uint64_t status = esp_sleep_get_ext1_wakeup_status();
                DEBUG_MSG("wakeCause2 %d\n", wakeCause2);

#ifdef BUTTON_PIN
                bool pressed = !digitalRead(BUTTON_PIN);
#else
                bool pressed = false;
#endif
                if (pressed) // If we woke because of press, instead generate a PRESS event.
                {
                    powerFSM.trigger(EVENT_PRESS);
                } else {
                    // Otherwise let the NB state handle the IRQ (and that state will handle stuff like IRQs etc)
                    // we lie and say "wake timer" because the interrupt will be handled by the regular IRQ code
                    powerFSM.trigger(EVENT_WAKE_TIMER);
                }
                break;
            }
        } else {
            // Someone says we can't sleep now, so just save some power by sleeping the CPU for 100ms or so
            delay(100);
        }
    } else {
        // Time to stop sleeping!
        setLed(false);
        DEBUG_MSG("reached ls_secs, servicing loop()\n");
        powerFSM.trigger(EVENT_WAKE_TIMER);
    }
#endif
}

static void lsExit()
{
    DEBUG_MSG("Exit state: LS\n");
    // setGPSPower(true); // restore GPS power
    if (gps)
        gps->forceWake(true);
}

static void nbEnter()
{
    DEBUG_MSG("Enter state: NB\n");
    screen->setOn(false);
    setBluetoothEnable(false);

    // FIXME - check if we already have packets for phone and immediately trigger EVENT_PACKETS_FOR_PHONE
}

static void darkEnter()
{
    setBluetoothEnable(true);
    screen->setOn(false);
}

static void serialEnter()
{
    DEBUG_MSG("Enter state: SERIAL\n");
    setBluetoothEnable(false);
    screen->setOn(true);
    screen->print("Serial connected\n");
}

static void serialExit()
{
    screen->print("Serial disconnected\n");
}

static void powerEnter()
{
    DEBUG_MSG("Enter state: POWER\n");
    if (!isPowered()) {
        // If we got here, we are in the wrong state - we should be in powered, let that state ahndle things
        DEBUG_MSG("Loss of power in Powered\n");
        powerFSM.trigger(EVENT_POWER_DISCONNECTED);
    } else {
        screen->setOn(true);
        setBluetoothEnable(true);
        screen->print("Powered...\n");
    }
}

static void powerIdle()
{
    if (!isPowered()) {
        // If we got here, we are in the wrong state
        DEBUG_MSG("Loss of power in Powered\n");
        powerFSM.trigger(EVENT_POWER_DISCONNECTED);
    }
}

static void powerExit()
{
    screen->setOn(true);
    setBluetoothEnable(true);
    screen->print("Unpowered...\n");
}

static void onEnter()
{
    DEBUG_MSG("Enter state: ON\n");
    screen->setOn(true);
    setBluetoothEnable(true);

    static uint32_t lastPingMs;

    uint32_t now = millis();

    if ((now - lastPingMs) > 30 * 1000) { // if more than a minute since our last press, ask node we are looking at to update their state
        if (displayedNodeNum)
            service.sendNetworkPing(displayedNodeNum, true); // Refresh the currently displayed node
        lastPingMs = now;
    }
}

static void onIdle()
{
    if (isPowered()) {
        // If we got here, we are in the wrong state - we should be in powered, let that state ahndle things
        powerFSM.trigger(EVENT_POWER_CONNECTED);
    }
}

static void screenPress()
{
    screen->onPress();
}

static void bootEnter()
{
    DEBUG_MSG("Enter state: BOOT\n");
}

State stateSHUTDOWN(shutdownEnter, NULL, NULL, "SHUTDOWN");
State stateSDS(sdsEnter, NULL, NULL, "SDS");
State stateLS(lsEnter, lsIdle, lsExit, "LS");
State stateNB(nbEnter, NULL, NULL, "NB");
State stateDARK(darkEnter, NULL, NULL, "DARK");
State stateSERIAL(serialEnter, NULL, serialExit, "SERIAL");
State stateBOOT(bootEnter, NULL, NULL, "BOOT");
State stateON(onEnter, onIdle, NULL, "ON");
State statePOWER(powerEnter, powerIdle, powerExit, "POWER");
Fsm powerFSM(&stateBOOT);

void PowerFSM_setup()
{
    bool isRouter = (config.device.role == Config_DeviceConfig_Role_Router ? 1 : 0);
    bool hasPower = isPowered();

    DEBUG_MSG("PowerFSM init, USB power=%d\n", hasPower);
    powerFSM.add_timed_transition(&stateBOOT, hasPower ? &statePOWER : &stateON, 3 * 1000, NULL, "boot timeout");

    // wake timer expired or a packet arrived
    // if we are a router node, we go to NB (no need for bluetooth) otherwise we go to DARK (so we can send message to phone)
    powerFSM.add_transition(&stateLS, isRouter ? &stateNB : &stateDARK, EVENT_WAKE_TIMER, NULL, "Wake timer");

    // We need this transition, because we might not transition if we were waiting to enter light-sleep, because when we wake from
    // light sleep we _always_ transition to NB or dark and
    powerFSM.add_transition(&stateLS, isRouter ? &stateNB : &stateDARK, EVENT_PACKET_FOR_PHONE, NULL, "Received packet, exiting light sleep");
    powerFSM.add_transition(&stateNB, &stateNB, EVENT_PACKET_FOR_PHONE, NULL, "Received packet, resetting win wake");

    // Handle press events - note: we ignore button presses when in API mode
    powerFSM.add_transition(&stateLS, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&statePOWER, &statePOWER, EVENT_PRESS, screenPress, "Press");
    powerFSM.add_transition(&stateON, &stateON, EVENT_PRESS, screenPress, "Press"); // reenter On to restart our timers
    powerFSM.add_transition(&stateSERIAL, &stateSERIAL, EVENT_PRESS, screenPress, "Press"); // Allow button to work while in serial API

    // Handle critically low power battery by forcing deep sleep
    powerFSM.add_transition(&stateBOOT, &stateSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateLS, &stateSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateNB, &stateSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateDARK, &stateSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateON, &stateSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateSERIAL, &stateSDS, EVENT_LOW_BATTERY, NULL, "LowBat");

    // Handle being told to power off
    powerFSM.add_transition(&stateBOOT, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateLS, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateNB, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateDARK, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateON, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateSERIAL, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");

    // Inputbroker
    powerFSM.add_transition(&stateLS, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateON, &stateON, EVENT_INPUT, NULL, "Input Device"); // restarts the sleep timer

    powerFSM.add_transition(&stateDARK, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");
    powerFSM.add_transition(&stateON, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");

    // if we are a router we don't turn the screen on for these things
    if (!isRouter) {
        // if any packet destined for phone arrives, turn on bluetooth at least
        powerFSM.add_transition(&stateNB, &stateDARK, EVENT_PACKET_FOR_PHONE, NULL, "Packet for phone");

        // show the latest node when we get a new node db update
        powerFSM.add_transition(&stateNB, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update");
        powerFSM.add_transition(&stateDARK, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update");
        powerFSM.add_transition(&stateON, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update");

        // Show the received text message
        powerFSM.add_transition(&stateLS, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateNB, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateDARK, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateON, &stateON, EVENT_RECEIVED_TEXT_MSG, NULL, "Received text"); // restarts the sleep timer
    }

    // If we are not in statePOWER but get a serial connection, suppress sleep (and keep the screen on) while connected
    powerFSM.add_transition(&stateLS, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&stateNB, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&stateDARK, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&stateON, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&statePOWER, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");

    // If we get power connected, go to the power connect state
    powerFSM.add_transition(&stateLS, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");
    powerFSM.add_transition(&stateNB, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");
    powerFSM.add_transition(&stateDARK, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");
    powerFSM.add_transition(&stateON, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");

    powerFSM.add_transition(&statePOWER, &stateON, EVENT_POWER_DISCONNECTED, NULL, "power disconnected");
    // powerFSM.add_transition(&stateSERIAL, &stateON, EVENT_POWER_DISCONNECTED, NULL, "power disconnected");

    // the only way to leave state serial is for the client to disconnect (or we timeout and force disconnect them)
    // when we leave, go to ON (which might not be the correct state if we have power connected, we will fix that in onEnter)
    powerFSM.add_transition(&stateSERIAL, &stateON, EVENT_SERIAL_DISCONNECTED, NULL, "serial disconnect");

    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_CONTACT_FROM_PHONE, NULL, "Contact from phone");

    // each time we get a new update packet make sure we are staying in the ON state so the screen stays awake (also we don't
    // shutdown bluetooth if is_router)
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_FIRMWARE_UPDATE, NULL, "Got firmware update");
    powerFSM.add_transition(&stateON, &stateON, EVENT_FIRMWARE_UPDATE, NULL, "Got firmware update");

    powerFSM.add_timed_transition(&stateON, &stateDARK, getConfiguredOrDefaultMs(config.display.screen_on_secs, default_screen_on_secs), NULL, "Screen-on timeout");

    // On most boards we use light-sleep to be our main state, but on NRF52 we just stay in DARK
    State *lowPowerState = &stateLS;

    uint32_t meshSds = 0;

#ifdef ARCH_ESP32
    // We never enter light-sleep or NB states on NRF52 (because the CPU uses so little power normally)

    // See: https://github.com/meshtastic/Meshtastic-device/issues/1071
    if (isRouter || config.power.is_power_saving) {
        powerFSM.add_timed_transition(&stateNB, &stateLS, getConfiguredOrDefaultMs(config.power.min_wake_secs, default_min_wake_secs), NULL, "Min wake timeout");
        powerFSM.add_timed_transition(&stateDARK, &stateLS, getConfiguredOrDefaultMs(config.power.wait_bluetooth_secs, default_wait_bluetooth_secs), NULL, "Bluetooth timeout");
        meshSds = config.power.mesh_sds_timeout_secs ? config.power.mesh_sds_timeout_secs : default_mesh_sds_timeout_secs;

    } else {

        meshSds = UINT32_MAX;
    }

#else
    lowPowerState = &stateDARK;
    meshSds = UINT32_MAX; // Workaround for now: Don't go into deep sleep on the RAK4631
#endif

    if (meshSds != UINT32_MAX)
        powerFSM.add_timed_transition(lowPowerState, &stateSDS, meshSds * 1000, NULL, "mesh timeout");

    powerFSM.run_machine(); // run one interation of the state machine, so we run our on enter tasks for the initial DARK state
}
