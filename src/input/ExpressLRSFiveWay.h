/*
    Input source for Radio Master Bandit Nano, and similar hardware.
    Devices have a 5-button "resistor ladder" style joystick, read by ADC.
    These devices do not use the ADC to monitor input voltage.

    Much of this code taken directly from ExpressLRS FiveWayButton class:
    https://github.com/ExpressLRS/ExpressLRS/tree/d9f56f8bd6f9f7144d5f01caaca766383e1e0950/src/lib/SCREEN/FiveWayButton
*/

#pragma once

#include "configuration.h"

#ifdef INPUTBROKER_EXPRESSLRSFIVEWAY_TYPE

#include <esp_adc_cal.h>
#include <soc/adc_channel.h>

#include "InputBroker.h"
#include "MeshService.h" // For adhoc ping action
#include "buzz.h"
#include "concurrency/OSThread.h"
#include "graphics/Screen.h" // Feedback for adhoc ping / toggle GPS
#include "main.h"
#include "modules/CannedMessageModule.h"

#if HAS_GPS && !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h" // For toggle GPS action
#endif

class ExpressLRSFiveWay : public Observable<const InputEvent *>, public concurrency::OSThread
{
  private:
    // Number of values in JOY_ADC_VALUES, if defined
    // These must be ADC readings for {UP, DOWN, LEFT, RIGHT, ENTER, IDLE}
    static constexpr size_t N_JOY_ADC_VALUES = 6;
    static constexpr uint32_t KEY_DEBOUNCE_MS = 25;
    static constexpr uint32_t KEY_LONG_PRESS_MS = 3000; // How many milliseconds to hold key for a long press

    // This merged an enum used by the ExpressLRS code, with meshtastic canned message values
    // Key names are kept simple, to allow user customizaton
    typedef enum {
        UP = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP,
        DOWN = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN,
        LEFT = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT,
        RIGHT = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT,
        OK = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT,
        CANCEL = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL,
        NO_PRESS = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE
    } KeyType;

    typedef enum { SHORT, LONG } PressLength;

    // From ExpressLRS
    int keyInProcess;
    uint32_t keyDownStart;
    bool isLongPressed;
    const uint16_t joyAdcValues[N_JOY_ADC_VALUES] = {JOYSTICK_ADC_VALS};
    uint16_t fuzzValues[N_JOY_ADC_VALUES];
    void calcFuzzValues();
    int readKey();
    void update(int *keyValue, bool *keyLongPressed);

    // Meshtastic code
    void determineAction(KeyType key, PressLength length);
    void sendKey(KeyType key);
    inline bool inCannedMessageMenu() { return cannedMessageModule->shouldDraw(); }
    int32_t runOnce() override;

    // Simplified Meshtastic actions, for easier remapping by user
    void toggleGPS();
    void sendAdhocPing();
    void shutdown();
    void click();

    bool alerting = false;        // Is the screen showing an alert frame? Feedback for GPS toggle / adhoc ping actions
    uint32_t alertingSinceMs = 0; // When did screen begin showing an alert frame? Used to auto-dismiss

  public:
    ExpressLRSFiveWay();
};

extern ExpressLRSFiveWay *expressLRSFiveWayInput;

#endif