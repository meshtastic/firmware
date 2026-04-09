#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"

/**
 * GpioOutputModule — drives OUTPUT_GPIO_PIN HIGH or LOW based on config.device.output_gpio_enabled.
 * The pin number is fixed at compile time via #define OUTPUT_GPIO_PIN in the board variant header.
 * The on/off state is persisted in DeviceConfig and can be changed via the Meshtastic admin interface
 * without requiring a device reboot.
 */
class GpioOutputModule : public concurrency::OSThread
{
  public:
    GpioOutputModule();

    /** Apply the current config.device.output_gpio_enabled state to the hardware pin. */
    void apply();

  protected:
    int32_t runOnce() override;
};

extern GpioOutputModule *gpioOutputModule;
