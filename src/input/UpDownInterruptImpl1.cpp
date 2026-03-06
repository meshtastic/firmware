#include "UpDownInterruptImpl1.h"
#include "InputBroker.h"
extern bool osk_found;

UpDownInterruptImpl1 *upDownInterruptImpl1;

UpDownInterruptImpl1::UpDownInterruptImpl1() : UpDownInterruptBase("upDown1") {}

bool UpDownInterruptImpl1::init()
{
#if defined(INPUTDRIVER_ENCODER_LEFT) && defined(INPUTDRIVER_ENCODER_RIGHT)
    // Two-way rocker boards can provide direct pin mapping via variant macros.
    // Use that mapping when config-backed inputbroker pins are unset/disabled.
    if (!moduleConfig.canned_message.updown1_enabled || moduleConfig.canned_message.inputbroker_pin_a == 0 ||
        moduleConfig.canned_message.inputbroker_pin_b == 0) {
        moduleConfig.canned_message.updown1_enabled = true;
        moduleConfig.canned_message.inputbroker_pin_a = INPUTDRIVER_ENCODER_LEFT;
        moduleConfig.canned_message.inputbroker_pin_b = INPUTDRIVER_ENCODER_RIGHT;
#if defined(INPUTDRIVER_ENCODER_BTN)
        moduleConfig.canned_message.inputbroker_pin_press = INPUTDRIVER_ENCODER_BTN;
#endif
    }
#endif

    if (!moduleConfig.canned_message.updown1_enabled) {
        // Input device is disabled.
        return false;
    }

    uint8_t pinUp = moduleConfig.canned_message.inputbroker_pin_a;
    uint8_t pinDown = moduleConfig.canned_message.inputbroker_pin_b;
    uint8_t pinPress = moduleConfig.canned_message.inputbroker_pin_press;

    input_broker_event eventDown = INPUT_BROKER_USER_PRESS; // acts like RIGHT/DOWN
    input_broker_event eventUp = INPUT_BROKER_ALT_PRESS;    // acts like LEFT/UP
    input_broker_event eventPressed = INPUT_BROKER_SELECT;
    input_broker_event eventPressedLong = INPUT_BROKER_SELECT_LONG;
    input_broker_event eventUpLong = INPUT_BROKER_UP_LONG;
    input_broker_event eventDownLong = INPUT_BROKER_DOWN_LONG;

    UpDownInterruptBase::init(pinDown, pinUp, pinPress, eventDown, eventUp, eventPressed, eventPressedLong, eventUpLong,
                              eventDownLong, UpDownInterruptImpl1::handleIntDown, UpDownInterruptImpl1::handleIntUp,
                              UpDownInterruptImpl1::handleIntPressed);
    inputBroker->registerSource(this);
#ifndef HAS_PHYSICAL_KEYBOARD
    osk_found = true;
#endif
    return true;
}

void UpDownInterruptImpl1::handleIntDown()
{
    upDownInterruptImpl1->intDownHandler();
}
void UpDownInterruptImpl1::handleIntUp()
{
    upDownInterruptImpl1->intUpHandler();
}
void UpDownInterruptImpl1::handleIntPressed()
{
    upDownInterruptImpl1->intPressHandler();
}
