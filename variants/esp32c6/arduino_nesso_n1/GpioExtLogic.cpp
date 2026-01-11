#include "GpioExtLogic.h"
#include <assert.h>

void GpioExtPin::set(bool value)
{
    gpio_ext_set(this->address, this->pin, value);
}

uint8_t GpioExtPin::get()
{
    return gpio_ext_get(this->address, this->pin);
}
