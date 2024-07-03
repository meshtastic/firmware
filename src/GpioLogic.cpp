#include "GpioLogic.h"
#include <assert.h>

void GpioVirtPin::set(bool value)
{
    if (value != this->value) {
        this->value = value ? PinState::On : PinState::Off;
        if (dependentPin)
            dependentPin->update();
    }
}

GpioLogicPin::GpioLogicPin(GpioPin *outPin) : outPin(outPin) {}

void GpioLogicPin::set(bool value)
{
    outPin->set(value);
}

GpioNotPin::GpioNotPin(GpioVirtPin *inPin, GpioPin *outPin) : GpioLogicPin(outPin), inPin(inPin)
{
    assert(!inPin->dependentPin); // We only allow one dependent pin
    inPin->dependentPin = this;
    update();
}
void GpioNotPin::set(bool value)
{
    outPin->set(value);
}

/**
 * Update the output pin based on the current state of the input pin.
 */
void GpioNotPin::update()
{
    auto p = inPin->get();
    if (p == GpioVirtPin::PinState::Unset)
        return; // Not yet fully initialized

    set(!p);
}

GpioBinaryLogicPin::GpioBinaryLogicPin(GpioVirtPin *inPin1, GpioVirtPin *inPin2, GpioPin *outPin, Operation operation)
    : GpioLogicPin(outPin), inPin1(inPin1), inPin2(inPin2), operation(operation)
{
    assert(!inPin1->dependentPin); // We only allow one dependent pin
    inPin1->dependentPin = this;
    assert(!inPin2->dependentPin); // We only allow one dependent pin
    inPin2->dependentPin = this;
    update();
}

void GpioBinaryLogicPin::update()
{
    auto p1 = inPin1->get(), p2 = inPin2->get();
    GpioVirtPin::PinState newValue = GpioVirtPin::PinState::Unset;

    if (p1 == GpioVirtPin::PinState::Unset)
        newValue = p2; // Not yet fully initialized
    else if (p2 == GpioVirtPin::PinState::Unset)
        newValue = p1; // Not yet fully initialized

    // If we've already found our value just use it, otherwise need to do the operation
    if (newValue == GpioVirtPin::PinState::Unset) {
        switch (operation) {
        case And:
            newValue = (GpioVirtPin::PinState)(p1 && p2);
            break;
        case Or:
            newValue = (GpioVirtPin::PinState)(p1 || p2);
            break;
        case Xor:
            newValue = (GpioVirtPin::PinState)(p1 != p2);
            break;
        default:
            assert(false);
        }
    }
    set(newValue);
}