/*
    Input source for Radio Master Bandit Nano, and similar hardware.
    Devices have a 5-button "resistor ladder" style joystick, read by ADC.
    These devices do not use the ADC to monitor input voltage.

    Much of this code takes directly from ExpressLRS FiveWayButton class:
    https://github.com/ExpressLRS/ExpressLRS/tree/d9f56f8bd6f9f7144d5f01caaca766383e1e0950/src/lib/SCREEN/FiveWayButton
*/

#pragma once

#include "configuration.h"

#ifdef INPUTBROKER_EXPRESSLRSFIVEWAY_TYPE

#include <esp_adc_cal.h>
#include <soc/adc_channel.h>

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "main.h"
#include "modules/cannedMessageModule.h"

// Number of values in JOY_ADC_VALUES, if defined
// These must be ADC readings for {UP, DOWN, LEFT, RIGHT, ENTER, IDLE}
constexpr size_t N_JOY_ADC_VALUES = 6;

class ExpressLRSFiveWay : public Observable<const InputEvent *>, public concurrency::OSThread
{
  private:
    // These constants have been remapped to match Meshtastic canned message input events
    // Main reason is to simplify handling with a switch in runOnce
    typedef enum {
        INPUT_KEY_LEFT_PRESS = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT,
        INPUT_KEY_UP_PRESS = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP,
        INPUT_KEY_RIGHT_PRESS = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT,
        INPUT_KEY_DOWN_PRESS = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN,
        INPUT_KEY_OK_PRESS = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT,
        INPUT_KEY_NO_PRESS = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE
    } Input_Key_Value_t;

    int keyInProcess;
    uint32_t keyDownStart;
    bool isLongPressed;
    const uint16_t joyAdcValues[N_JOY_ADC_VALUES] = {JOYSTICK_ADC_VALS};
    uint16_t fuzzValues[N_JOY_ADC_VALUES];
    void calcFuzzValues();

    int readKey();

    // Meshtastic methods
    void raiseEvent(_meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar key);
    int32_t runOnce() override;

  public:
    ExpressLRSFiveWay();
    void update(int *keyValue, bool *keyLongPressed);

    static constexpr uint32_t KEY_DEBOUNCE_MS = 25;
    static constexpr uint32_t KEY_LONG_PRESS_MS = 5000;
};

extern ExpressLRSFiveWay *expressLRSFiveWayInput;

#endif