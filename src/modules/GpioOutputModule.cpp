#include "GpioOutputModule.h"
#include "NodeDB.h"
#include <Arduino.h>

GpioOutputModule *gpioOutputModule;

GpioOutputModule::GpioOutputModule() : concurrency::OSThread("GpioOutputModule")
{
    apply();
}

void GpioOutputModule::apply()
{
#ifdef OUTPUT_GPIO_PIN
    pinMode(OUTPUT_GPIO_PIN, OUTPUT);
    digitalWrite(OUTPUT_GPIO_PIN, config.device.output_gpio_enabled ? HIGH : LOW);
    LOG_DEBUG("GpioOutputModule: OUTPUT_GPIO_PIN=%d state=%s", OUTPUT_GPIO_PIN,
              config.device.output_gpio_enabled ? "HIGH" : "LOW");
#endif
}

int32_t GpioOutputModule::runOnce()
{
    return INT32_MAX; // No periodic work needed — state is applied on init and on config change
}
