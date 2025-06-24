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
#include "modules/StatusLEDModule.h"
#include "sleep.h"
#include "target_specific.h"

#ifdef ARCH_ESP32
#include "esp32/pm.h"
#include "esp_pm.h"
#endif

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#endif

#ifndef SLEEP_TIME
#define SLEEP_TIME 30
#endif

#if MESHTASTIC_EXCLUDE_POWER_FSM
FakeFsm powerFSM;
void PowerFSM_setup(){};
#else
/// Should we behave as if we have AC power now?
static bool isPowered()
{
// Circumvent the battery sensing logic and assumes constant power if no battery pin or power mgmt IC
#if !defined(BATTERY_PIN) && !defined(HAS_AXP192) && !defined(HAS_AXP2101) && !defined(NRF_APM)
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
    LOG_POWERFSM("State: sdsEnter");
    // FIXME - make sure GPS and LORA radio are off first - because we want close to zero current draw
    doDeepSleep(Default::getConfiguredOrDefaultMs(config.power.sds_secs), false, false);
}

static void lowBattSDSEnter()
{
    LOG_POWERFSM("State: lowBattSDSEnter");
    doDeepSleep(Default::getConfiguredOrDefaultMs(config.power.sds_secs), false, true);
}
extern Power *power;

static void shutdownEnter()
{
    LOG_POWERFSM("State: shutdownEnter");
    shutdownAtMsec = millis();
}

#include "error.h"

uint32_t sleepStart;
uint32_t sleepTime;

static void lsEnter()
{
    LOG_POWERFSM("State: lsEnter");
    if (screen)
        screen->setOn(false);
    ledBlink.set(false);

    sleepStart = -1;
    sleepTime = 0;

    powerMon->setState(meshtastic_PowerMon_State_CPU_LightSleep);
}

static void lsIdle()
{
    if (!doPreflightSleep()) {
#ifdef HAS_DYNAMIC_LIGHT_SLEEP
        powerFSM.trigger(EVENT_WAKE_TIMER);
#endif
        return;
    }

    if (sleepStart == -1) {
        sleepStart = millis();
    }

    sleepTime = millis() - sleepStart;

#ifdef ARCH_ESP32
    uint32_t sleepLeft;

    sleepLeft = config.power.ls_secs * 1000LL - sleepTime;
    if (sleepLeft > SLEEP_TIME * 1000LL) {
        sleepLeft = SLEEP_TIME * 1000LL;
    }

    doLightSleep(sleepLeft);

    esp_sleep_source_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
    case ESP_SLEEP_WAKEUP_UART:
        LOG_POWERFSM("Wake cause ESP_SLEEP_WAKEUP_UART");
        powerFSM.trigger(EVENT_INPUT);
        return;

    case ESP_SLEEP_WAKEUP_EXT1:
        LOG_POWERFSM("Wake cause ESP_SLEEP_WAKEUP_EXT1");
        powerFSM.trigger(EVENT_PRESS);
        return;

    case ESP_SLEEP_WAKEUP_GPIO:
        LOG_POWERFSM("Wake cause ESP_SLEEP_WAKEUP_GPIO");
        powerFSM.trigger(EVENT_WAKE_TIMER);
        return;

    default:
        if (sleepTime > config.power.ls_secs * 1000LL) {
            powerFSM.trigger(EVENT_WAKE_TIMER);
            return;
        }
        break;
    }
#endif
}

static void lsExit()
{
#ifdef ARCH_ESP32
    doLightSleep(LIGHT_SLEEP_ABORT);
#endif

    if (sleepStart != -1) {
        sleepTime = millis() - sleepStart;
        sleepStart = 0;

        powerMon->clearState(meshtastic_PowerMon_State_CPU_LightSleep);

        LOG_POWERFSM("State: lsExit, stayed %d ms in light-sleep state", sleepTime);
    }
}

static void darkEnter()
{
    LOG_POWERFSM("State: darkEnter");
    if (screen)
        screen->setOn(false);
}

static void serialEnter()
{
    LOG_POWERFSM("State: serialEnter");
    if (screen) {
        screen->setOn(true);
    }
}

static void serialExit()
{
    LOG_POWERFSM("State: serialExit");
}

static void powerEnter()
{
    LOG_POWERFSM("State: powerEnter");
    if (!isPowered()) {
        // If we got here, we are in the wrong state - we should be in powered, let that state handle things
        LOG_INFO("Loss of power in Powered");
        powerFSM.trigger(EVENT_POWER_DISCONNECTED);

    } else {
        if (screen)
            screen->setOn(true);
        // within enter() the function getState() returns the state we came from
    }
}

static void powerIdle()
{
    // LOG_POWERFSM("State: powerIdle"); // very chatty
    if (!isPowered()) {
        // If we got here, we are in the wrong state
        LOG_INFO("Loss of power in Powered");
        powerFSM.trigger(EVENT_POWER_DISCONNECTED);
    }
}

static void powerExit()
{
    LOG_POWERFSM("State: powerExit");
    if (screen)
        screen->setOn(true);
}

static void onEnter()
{
    LOG_POWERFSM("State: onEnter");
    if (screen)
        screen->setOn(true);
}

static void onIdle()
{
    LOG_POWERFSM("State: onIdle");
    if (isPowered()) {
        // If we got here, we are in the wrong state - we should be in powered, let that state handle things
        powerFSM.trigger(EVENT_POWER_CONNECTED);
    }
}

static void bootEnter()
{
    LOG_POWERFSM("State: bootEnter");
}

State stateSHUTDOWN(shutdownEnter, NULL, NULL, "SHUTDOWN");
State stateSDS(sdsEnter, NULL, NULL, "SDS");
State stateLowBattSDS(lowBattSDSEnter, NULL, NULL, "SDS");
State stateLS(lsEnter, lsIdle, lsExit, "LS");
State stateDARK(darkEnter, NULL, NULL, "DARK");
State stateSERIAL(serialEnter, NULL, serialExit, "SERIAL");
State stateBOOT(bootEnter, NULL, NULL, "BOOT");
State stateON(onEnter, onIdle, NULL, "ON");
State statePOWER(powerEnter, powerIdle, powerExit, "POWER");
Fsm powerFSM(&stateBOOT);

void PowerFSM_setup()
{
    bool isRouter = (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ? 1 : 0);
    bool hasPower = isPowered();

    LOG_INFO("PowerFSM init, USB power=%d, is_power_saving=%d, wake_time_ms=%d", hasPower ? 1 : 0,
             config.power.is_power_saving ? 1 : 0, WAKE_TIME_MS);

    powerFSM.add_timed_transition(&stateBOOT, hasPower ? &statePOWER : &stateON, 3 * 1000, NULL, "boot timeout");

    // Handle press events - note: we ignore button presses when in API mode
    powerFSM.add_transition(&stateLS, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateDARK, isPowered() ? &statePOWER : &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&statePOWER, &statePOWER, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateON, &stateON, EVENT_PRESS, NULL, "Press"); // reenter On to restart our timers
    powerFSM.add_transition(&stateSERIAL, &stateSERIAL, EVENT_PRESS, NULL,
                            "Press"); // Allow button to work while in serial API

    // Handle critically low power battery by forcing deep sleep
    powerFSM.add_transition(&stateBOOT, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateLS, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateDARK, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateON, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateSERIAL, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");

    // Handle being told to power off
    powerFSM.add_transition(&stateBOOT, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateLS, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateDARK, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateON, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateSERIAL, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");

    // Inputbroker
    powerFSM.add_transition(&stateLS, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateON, &stateON, EVENT_INPUT, NULL, "Input Device");       // restarts the sleep timer
    powerFSM.add_transition(&statePOWER, &statePOWER, EVENT_INPUT, NULL, "Input Device"); // restarts the sleep timer

    powerFSM.add_transition(&stateDARK, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");
    powerFSM.add_transition(&stateON, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");

    // stay in dark state as long as we continue talking with phone
    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_CONTACT_FROM_PHONE, NULL, "Contact from phone");
    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_PACKET_FOR_PHONE, NULL, "Packet for phone");

    if (!isRouter) {
        powerFSM.add_transition(&stateLS, &stateDARK, EVENT_CONTACT_FROM_PHONE, NULL, "Contact from phone");
        powerFSM.add_transition(&stateLS, &stateDARK, EVENT_PACKET_FOR_PHONE, NULL, "Packet for phone");

        // Show the received text message
        powerFSM.add_transition(&stateLS, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateDARK, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text");
        powerFSM.add_transition(&stateON, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text"); // restarts the sleep timer
    }

    // If we are not in statePOWER but get a serial connection, suppress sleep (and keep the screen on) while connected
    powerFSM.add_transition(&stateLS, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "Serial API");
    powerFSM.add_transition(&stateDARK, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "Serial API");
    powerFSM.add_transition(&stateON, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "Serial API");
    powerFSM.add_transition(&statePOWER, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "Serial API");

    // If we get power connected, go to the power connect state
    powerFSM.add_transition(&stateLS, &statePOWER, EVENT_POWER_CONNECTED, NULL, "Power connect");
    powerFSM.add_transition(&stateDARK, &statePOWER, EVENT_POWER_CONNECTED, NULL, "Power connect");
    powerFSM.add_transition(&stateON, &statePOWER, EVENT_POWER_CONNECTED, NULL, "Power connect");

    powerFSM.add_transition(&statePOWER, &stateON, EVENT_POWER_DISCONNECTED, NULL, "Power disconnected");
    powerFSM.add_transition(&stateLS, &stateON, EVENT_POWER_DISCONNECTED, NULL, "Power disconnected");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_POWER_DISCONNECTED, NULL, "Power disconnected");

    // the only way to leave state serial is for the client to disconnect (or we timeout and force disconnect them)
    // when we leave, go to ON (which might not be the correct state if we have power connected, we will fix that in onEnter)
    powerFSM.add_transition(&stateSERIAL, &stateON, EVENT_SERIAL_DISCONNECTED, NULL, "Serial disconnect");

    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_WAKE_TIMER, NULL, "Wake timer");
    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_WAKE_TIMER, NULL, "Wake timer");

    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_WEB_REQUEST, NULL, "Web request");
    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_WEB_REQUEST, NULL, "Web request");

#ifdef HAS_DYNAMIC_LIGHT_SLEEP
    // it's better to exit dynamic light sleep when packet is received to ensure routing is properly handled
    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_RADIO_INTERRUPT, NULL, "Radio interrupt");
    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_RADIO_INTERRUPT, NULL, "Radio interrupt");
#endif

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

// We never enter light-sleep on NRF52 (because the CPU uses so little power normally)
#ifdef ARCH_ESP32
    if (config.power.is_power_saving) {
        powerFSM.add_timed_transition(&stateDARK, &stateLS, WAKE_TIME_MS, NULL, "Min wake timeout");
    }
#endif

    powerFSM.run_machine(); // run one iteration of the state machine, so we run our on enter tasks for the initial DARK state
}
#endif
