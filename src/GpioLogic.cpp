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

void GpioHwPin::set(bool value)
{
    pinMode(num, OUTPUT);
    digitalWrite(num, value);
}

GpioTransformer::GpioTransformer(GpioPin *outPin) : outPin(outPin) {}

void GpioTransformer::set(bool value)
{
    outPin->set(value);
}

GpioUnaryTransformer::GpioUnaryTransformer(GpioVirtPin *inPin, GpioPin *outPin) : GpioTransformer(outPin), inPin(inPin)
{
    assert(!inPin->dependentPin); // We only allow one dependent pin
    inPin->dependentPin = this;

    // Don't update at construction time, because various GpioPins might be global constructor based not yet initied because
    // order of operations for global constructors is not defined.
    // update();
}

/**
 * Update the output pin based on the current state of the input pin.
 */
void GpioUnaryTransformer::update()
{
    auto p = inPin->get();
    if (p == GpioVirtPin::PinState::Unset)
        return; // Not yet fully initialized

    set(p);
}

/**
 * Update the output pin based on the current state of the input pin.
 */
void GpioNotTransformer::update()
{
    auto p = inPin->get();
    if (p == GpioVirtPin::PinState::Unset)
        return; // Not yet fully initialized

    set(!p);
}

GpioBinaryTransformer::GpioBinaryTransformer(GpioVirtPin *inPin1, GpioVirtPin *inPin2, GpioPin *outPin, Operation operation)
    : GpioTransformer(outPin), inPin1(inPin1), inPin2(inPin2), operation(operation)
{
    assert(!inPin1->dependentPin); // We only allow one dependent pin
    inPin1->dependentPin = this;
    assert(!inPin2->dependentPin); // We only allow one dependent pin
    inPin2->dependentPin = this;

    // Don't update at construction time, because various GpioPins might be global constructor based not yet initiated because
    // order of operations for global constructors is not defined.
    // update();
}

void GpioBinaryTransformer::update()
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

GpioSplitter::GpioSplitter(GpioPin *outPin1, GpioPin *outPin2) : outPin1(outPin1), outPin2(outPin2) {}
