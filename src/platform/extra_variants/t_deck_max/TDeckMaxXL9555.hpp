#pragma once

#if defined(T_DECK_MAX)

#include <Arduino.h>
#include <Wire.h>

#include "GpioLogic.h"
#include "concurrency/Lock.h"

#ifndef XL9555_SLAVE_ADDRESS0
#define XL9555_SLAVE_ADDRESS0 0x20
#endif

class ExtensionIOXL9555;

class XL9555GpioPin : public GpioPin
{
  public:
    XL9555GpioPin(ExtensionIOXL9555 *owner, uint8_t pin) : owner(owner), pin(pin) {}

    void set(bool value) override;

  private:
    ExtensionIOXL9555 *owner;
    uint8_t pin;
};

class ExtensionIOXL9555
{
  public:
    bool begin(TwoWire &wire, uint8_t address = XL9555_SLAVE_ADDRESS0, int sda = -1, int scl = -1);
    void pinMode(uint8_t pin, uint8_t mode);
    void digitalWrite(uint8_t pin, uint8_t value);
    int digitalRead(uint8_t pin);
    bool isReady() const { return ready; }
    uint16_t outputState() const { return outputLatch; }
    void setSafeState();
    GpioPin *makeGpioPin(uint8_t pin);

  private:
    static constexpr uint8_t INPUT_PORT_0 = 0x00;
    static constexpr uint8_t OUTPUT_PORT_0 = 0x02;
    static constexpr uint8_t CONFIG_PORT_0 = 0x06;

    TwoWire *bus = nullptr;
    uint8_t address = XL9555_SLAVE_ADDRESS0;
    uint16_t outputLatch = 0;
    uint16_t direction = 0xFFFF;
    bool ready = false;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool writePortPair(uint8_t reg, uint16_t value);
    bool readPortPair(uint8_t reg, uint16_t &value);
    concurrency::Lock lock;
};

extern ExtensionIOXL9555 io;

#endif // T_DECK_MAX
