#include "GpioLogic.h"

class GpioExtPin : public GpioPin
{
    uint32_t num;

  public:
    GpioExtPin(uint16_t _pin) : pin(_pin & 0x3F), address(_pin & 0x100 ? 0x44 : 0x43){};
    uint8_t pin;
    uint8_t address;

    void set(bool value);
    uint8_t get();
};
