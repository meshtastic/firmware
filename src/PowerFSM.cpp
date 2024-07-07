/**
 * @file PowerFSM.cpp
 * @brief Implements the finite state machine for power management.
 *
 * This file contains the implementation of the finite state machine (FSM) for power management.
 * The FSM controls the power states of the device, including SDS (shallow deep sleep), LS (light sleep),
 * NB (normal mode), and POWER (powered mode). The FSM also handles transitions between states and
 * actions to be taken upon entering or exiting each state.
 */
#include "PowerFSM.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "sleep.h"
#include "target_specific.h"

#ifndef SLEEP_TIME
#define SLEEP_TIME 30
#endif

/// Should we behave as if we have AC power now?
static bool isPowered()
{
// Circumvent the battery sensing logic and assumes constant power if no battery pin or power mgmt IC
#if !defined(BATTERY_PIN) && !defined(HAS_AXP192) && !defined(HAS_AXP2101)
    return true;
#endif

    bool isRouter = (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ? 1 : 0);

    // If we are not a router and we already have AC power go to POWER state after init, otherwise go to ON
    // We assume routers might be powered all the time, but from a low current (solar) source
    bool isPowerSavingMode = config.power.is_power_saving || isRouter;

    /* To determine if we're externally powered, assumptions
        1) If we're powered up and there's no battery, we must be getting power externally. (because we'd be dead otherwise)

        2) If we detect USB power from the power management chip, we must be getting power externally.

        3) On some boards we don't have the power management chip (like AXPxxxx) so we use EXT_PWR_DETECT GPIO pin to detect
       external power source (see `isVbusIn()` in `Power.cpp`)
    */
    return !isPowerSavingMode && powerStatus && (!powerStatus->getHasBattery() || powerStatus->getHasUSB());
}

static void sdsEnter()
{
    LOG_DEBUG("Enter state: SDS\n");
    powerMon->setState(meshtastic_PowerMon_State_CPU_DeepSleep);
    // FIXME - make sure GPS and LORA radio are off first - because we want close to zero current draw
    doDeepSleep(Default::getConfiguredOrDefaultMs(config.power.sds_secs), false);
}

extern Power *power;

static void shutdownEnter()
{
    LOG_DEBUG("Enter state: SHUTDOWN\n");
    power->shutdown();
}

#include "error.h"

static uint32_t secsSlept;

static void lsEnter()
{
    LOG_INFO("lsEnter begin, ls_secs=%u\n", config.power.ls_secs);
    powerMon->clearState(meshtastic_PowerMon_State_Screen_On);
    screen->setOn(false);
    secsSlept = 0; // How long have we been sleeping this time

    // LOG_INFO("lsEnter end\n");
}

static void lsIdle()
{
    // LOG_INFO("lsIdle begin ls_secs=%u\n", getPref_ls_secs());

#ifdef ARCH_ESP32

    // Do we have more sleeping to do?
    if (secsSlept < config.power.ls_secs) {
        // If some other service would stall sleep, don't let sleep happen yet
        if (doPreflightSleep()) {
            // Briefly come out of sleep long enough to blink the led once every few seconds
            uint32_t sleepTime = SLEEP_TIME;

            powerMon->setState(meshtastic_PowerMon_State_CPU_LightSleep);
            setLed(false); // Never leave led on while in light sleep
            esp_sleep_source_t wakeCause2 = doLightSleep(sleepTime * 1000LL);
            powerMon->clearState(meshtastic_PowerMon_State_CPU_LightSleep);

            switch (wakeCause2) {
            case ESP_SLEEP_WAKEUP_TIMER:
                // Normal case: timer expired, we should just go back to sleep ASAP

                setLed(true);                   // briefly turn on led
                wakeCause2 = doLightSleep(100); // leave led on for 1ms

                secsSlept += sleepTime;
                // LOG_INFO("sleeping, flash led!\n");
                break;

            case ESP_SLEEP_WAKEUP_UART:
                // Not currently used (because uart triggers in hw have problems)
                powerFSM.trigger(EVENT_SERIAL_CONNECTED);
                break;

            default:
                // We woke for some other reason (button press, device IRQ interrupt)

#ifdef BUTTON_PIN
                bool pressed = !digitalRead(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN);
#else
                bool pressed = false;
#endif
                if (pressed) { // If we woke because of press, instead generate a PRESS event.
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
        LOG_INFO("Reached ls_secs, servicing loop()\n");
        powerFSM.trigger(EVENT_WAKE_TIMER);
    }
#endif
}

static void lsExit()
{
    LOG_INFO("Exit state: LS\n");
}

static void nbEnter()
{
    LOG_DEBUG("Enter state: NB\n");
    powerMon->clearState(meshtastic_PowerMon_State_BT_On);
    screen->setOn(false);
#ifdef ARCH_ESP32
    // Only ESP32 should turn off bluetooth
    setBluetoothEnable(false);
#endif

    // FIXME - check if we already have packets for phone and immediately trigger EVENT_PACKETS_FOR_PHONE
}

static void darkEnter()
{
    powerMon->clearState(meshtastic_PowerMon_State_BT_On);
    powerMon->clearState(meshtastic_PowerMon_State_Screen_On);
    setBluetoothEnable(true);
    screen->setOn(false);
}

static void serialEnter()
{
    LOG_DEBUG("Enter state: SERIAL\n");
    powerMon->clearState(meshtastic_PowerMon_State_BT_On);
    powerMon->setState(meshtastic_PowerMon_State_Screen_On);
    setBluetoothEnable(false);
    screen->setOn(true);
    screen->print("Serial connected\n");
}

static void serialExit()
{
    // Turn bluetooth back on when we leave serial stream API
    powerMon->setState(meshtastic_PowerMon_State_BT_On);
    setBluetoothEnable(true);
    screen->print("Serial disconnected\n");
}

static void powerEnter()
{
    // LOG_DEBUG("Enter state: POWER\n");
    if (!isPowered()) {
        // If we got here, we are in the wrong state - we should be in powered, let that state ahndle things
        LOG_INFO("Loss of power in Powered\n");
        powerFSM.trigger(EVENT_POWER_DISCONNECTED);
    } else {
        powerMon->setState(meshtastic_PowerMon_State_BT_On);
        powerMon->setState(meshtastic_PowerMon_State_Screen_On);
        screen->setOn(true);
        setBluetoothEnable(true);
        // within enter() the function getState() returns the state we came from

        // Mothballed: print change of power-state to device screen
        /* if (strcmp(powerFSM.getState()->name, "BOOT") != 0 && strcmp(powerFSM.getState()->name, "POWER") != 0 &&
            strcmp(powerFSM.getState()->name, "DARK") != 0) {
            screen->print("Powered...\n");
        }*/
    }
}

static void powerIdle()
{
    if (!isPowered()) {
        // If we got here, we are in the wrong state
        LOG_INFO("Loss of power in Powered\n");
        powerFSM.trigger(EVENT_POWER_DISCONNECTED);
    }
}

static void powerExit()
{
    powerMon->setState(meshtastic_PowerMon_State_BT_On);
    powerMon->setState(meshtastic_PowerMon_State_Screen_On);
    screen->setOn(true);
    setBluetoothEnable(true);

    // Mothballed: print change of power-state to device screen
    /*if (!isPowered())
        screen->print("Unpowered...\n");*/
}

static void onEnter()
{
    LOG_DEBUG("Enter state: ON\n");
    powerMon->setState(meshtastic_PowerMon_State_BT_On);
    powerMon->setState(meshtastic_PowerMon_State_Screen_On);
    screen->setOn(true);
    setBluetoothEnable(true);
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
    LOG_DEBUG("Enter state: BOOT\n");
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
    bool isRouter = (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ? 1 : 0);
    bool isTrackerOrSensor = config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
                             config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER ||
                             config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR;
    bool hasPower = isPowered();

    LOG_INFO("PowerFSM init, USB power=%d\n", hasPower ? 1 : 0);
    powerFSM.add_timed_transition(&stateBOOT, hasPower ? &statePOWER : &stateON, 3 * 1000, NULL, "boot timeout");

    // wake timer expired or a packet arrived
    // if we are a router node, we go to NB (no need for bluetooth) otherwise we go to DARK (so we can send message to phone)
#ifdef ARCH_ESP32
    powerFSM.add_transition(&stateLS, isRouter ? &stateNB : &stateDARK, EVENT_WAKE_TIMER, NULL, "Wake timer");
#else // Don't go into a no-bluetooth state on low power platforms
    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_WAKE_TIMER, NULL, "Wake timer");
#endif

    // We need this transition, because we might not transition if we were waiting to enter light-sleep, because when we wake from
    // light sleep we _always_ transition to NB or dark and
    powerFSM.add_transition(&stateLS, isRouter ? &stateNB : &stateDARK, EVENT_PACKET_FOR_PHONE, NULL,
                            "Received packet, exiting light sleep");
    powerFSM.add_transition(&stateNB, &stateNB, EVENT_PACKET_FOR_PHONE, NULL, "Received packet, resetting win wake");

    // Handle press events - note: we ignore button presses when in API mode
    powerFSM.add_transition(&stateLS, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateDARK, isPowered() ? &statePOWER : &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&statePOWER, &statePOWER, EVENT_PRESS, screenPress, "Press");
    powerFSM.add_transition(&stateON, &stateON, EVENT_PRESS, screenPress, "Press"); // reenter On to restart our timers
    powerFSM.add_transition(&stateSERIAL, &stateSERIAL, EVENT_PRESS, screenPress,
                            "Press"); // Allow button to work while in serial API

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
    powerFSM.add_transition(&stateON, &stateON, EVENT_INPUT, NULL, "Input Device");       // restarts the sleep timer
    powerFSM.add_transition(&statePOWER, &statePOWER, EVENT_INPUT, NULL, "Input Device"); // restarts the sleep timer

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
        powerFSM.add_transition(&stateLS, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateNB, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateDARK, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateON, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text"); // restarts the sleep timer
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

#ifdef USE_EINK
    // Allow E-Ink devices to suppress the screensaver, if screen timeout set to 0
    if (config.display.screen_on_secs > 0)
#endif
    {
        powerFSM.add_timed_transition(&stateON, &stateDARK,
                                      Default::getConfiguredOrDefaultMs(config.display.screen_on_secs, default_screen_on_secs),
                                      NULL, "Screen-on timeout");
        powerFSM.add_timed_transition(&statePOWER, &stateDARK,
                                      Default::getConfiguredOrDefaultMs(config.display.screen_on_secs, default_screen_on_secs),
                                      NULL, "Screen-on timeout");
    }

// We never enter light-sleep or NB states on NRF52 (because the CPU uses so little power normally)
#ifdef ARCH_ESP32
    // See: https://github.com/meshtastic/firmware/issues/1071
    // Don't add power saving transitions if we are a power saving tracker or sensor. Sleep will be initiatiated through the
    // modules
    if ((isRouter || config.power.is_power_saving) && !isTrackerOrSensor) {
        powerFSM.add_timed_transition(&stateNB, &stateLS,
                                      Default::getConfiguredOrDefaultMs(config.power.min_wake_secs, default_min_wake_secs), NULL,
                                      "Min wake timeout");

        // If ESP32 and using power-saving, timer mover from DARK to light-sleep
        // Also serves purpose of the old DARK to DARK transition(?) See https://github.com/meshtastic/firmware/issues/3517
        powerFSM.add_timed_transition(
            &stateDARK, &stateLS,
            Default::getConfiguredOrDefaultMs(config.power.wait_bluetooth_secs, default_wait_bluetooth_secs), NULL,
            "Bluetooth timeout");
    } else {
        // If ESP32, but not using power-saving, check periodically if config has drifted out of stateDark
        powerFSM.add_timed_transition(&stateDARK, &stateDARK,
                                      Default::getConfiguredOrDefaultMs(config.display.screen_on_secs, default_screen_on_secs),
                                      NULL, "Screen-on timeout");
    }
#else
    // If not ESP32, light-sleep not used. Check periodically if config has drifted out of stateDark
    powerFSM.add_timed_transition(&stateDARK, &stateDARK,
                                  Default::getConfiguredOrDefaultMs(config.display.screen_on_secs, default_screen_on_secs), NULL,
                                  "Screen-on timeout");
#endif

    powerFSM.run_machine(); // run one iteration of the state machine, so we run our on enter tasks for the initial DARK state
}