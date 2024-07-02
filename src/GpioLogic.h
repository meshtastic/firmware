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

    void set(bool value) { digitalWrite(num, value); }
};

class GpioLogicPin;
class GpioNotPin;
class GpioBinaryLogicPin;

/**
 * A virtual GPIO pin.
 */
class GpioVirtPin : public GpioPin
{
    friend class GpioBinaryLogicPin;
    friend class GpioNotPin;

  public:
    enum PinState { On = true, Off = false, Unset = 2 };

    void set(bool value);
    PinState get() const { return value; }

  private:
    PinState value = PinState::Unset;
    GpioLogicPin *dependentPin = NULL;
};

#include <assert.h>

/**
 * A logical GPIO pin baseclass, notably: the set method is not public (because it always is calculated by a subclass)
 */
class GpioLogicPin
{
  public:
    /**
     * Update the output pin based on the current state of the input pin.
     */
    virtual void update() = 0;

  protected:
    GpioLogicPin(GpioPin *outPin);

    void set(bool value);

  private:
    GpioPin *outPin;
};

/**
 * A logical GPIO pin that performs a unary NOT operation on a virtual pin.
 */
class GpioNotPin : public GpioLogicPin
{
  public:
    GpioNotPin(GpioVirtPin *inPin, GpioPin *outPin);
    void set(bool value);

  protected:
    friend class GpioVirtPin;

    /**
     * Update the output pin based on the current state of the input pin.
     */
    void update();

  private:
    GpioVirtPin *inPin;
    GpioPin *outPin;
};

/**
 * A logical GPIO pin that combines multiple virtual pins to drive a real physical pin
 */
class GpioBinaryLogicPin : public GpioLogicPin
{

  public:
    enum Operation { And, Or, Xor };

    GpioBinaryLogicPin(GpioVirtPin *inPin1, GpioVirtPin *inPin2, GpioPin *outPin, Operation operation);

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