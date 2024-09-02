#pragma once

#include "configuration.h"

/**This is a set of classes to mediate access to GPIOs in a structured way.  Most usage of GPIOs do not
    require these classes!  But if your hardware has a GPIO that is 'shared' between multiple devices (i.e. a shared power enable)
    then using these classes might be able to let you cleanly turn on that enable when either dependent device is needed.

    Note: these classes are intended to be 99% inline for the common case so should have minimal impact on flash or RAM
   requirements.
*/

/**
 * A logical GPIO pin (not necessary raw hardware).
 */
class GpioPin
{
  public:
    virtual void set(bool value) = 0;
};

/**
 * A physical GPIO hw pin.
 */
class GpioHwPin : public GpioPin
{
    uint32_t num;

  public:
    explicit GpioHwPin(uint32_t num) : num(num) {}

    void set(bool value);
};

class GpioTransformer;
class GpioNotTransformer;
class GpioBinaryTransformer;

/**
 * A virtual GPIO pin.
 */
class GpioVirtPin : public GpioPin
{
    friend class GpioBinaryTransformer;
    friend class GpioUnaryTransformer;

  public:
    enum PinState { On = true, Off = false, Unset = 2 };

    void set(bool value);
    PinState get() const { return value; }

  private:
    PinState value = PinState::Unset;
    GpioTransformer *dependentPin = NULL;
};

#include <assert.h>

/**
 * A 'smart' trigger that can depend in a fake GPIO and if that GPIO changes, drive some other downstream GPIO to change.
 * notably: the set method is not public (because it always is calculated by a subclass)
 */
class GpioTransformer
{
  public:
    /**
     * Update the output pin based on the current state of the input pin.
     */
    virtual void update() = 0;

  protected:
    GpioTransformer(GpioPin *outPin);

    void set(bool value);

  private:
    GpioPin *outPin;
};

/**
 * A transformer that just drives a hw pin based on a virtual pin.
 */
class GpioUnaryTransformer : public GpioTransformer
{
  public:
    GpioUnaryTransformer(GpioVirtPin *inPin, GpioPin *outPin);

  protected:
    friend class GpioVirtPin;

    /**
     * Update the output pin based on the current state of the input pin.
     */
    virtual void update();

    GpioVirtPin *inPin;
};

/**
 * A transformer that performs a unary NOT operation from an input.
 */
class GpioNotTransformer : public GpioUnaryTransformer
{
  public:
    GpioNotTransformer(GpioVirtPin *inPin, GpioPin *outPin) : GpioUnaryTransformer(inPin, outPin) {}

  protected:
    friend class GpioVirtPin;

    /**
     * Update the output pin based on the current state of the input pin.
     */
    void update();
};

/**
 * A transformer that combines multiple virtual pins to drive an output pin
 */
class GpioBinaryTransformer : public GpioTransformer
{

  public:
    enum Operation { And, Or, Xor };

    GpioBinaryTransformer(GpioVirtPin *inPin1, GpioVirtPin *inPin2, GpioPin *outPin, Operation operation);

  protected:
    friend class GpioVirtPin;

    /**
     * Update the output pin based on the current state of the input pins.
     */
    void update();

  private:
    GpioVirtPin *inPin1;
    GpioVirtPin *inPin2;
    Operation operation;
};

/**
 * Sometimes a single output GPIO single needs to drive multiple physical GPIOs.  This class provides that.
 */
class GpioSplitter : public GpioPin
{

  public:
    GpioSplitter(GpioPin *outPin1, GpioPin *outPin2);

    void set(bool value)
    {
        outPin1->set(value);
        outPin2->set(value);
    }

  private:
    GpioPin *outPin1;
    GpioPin *outPin2;
};