#include "configuration.h"

#ifdef ARCH_STM32WL
#include "STM32WLE5JCInterface.h"
#include "error.h"

#ifndef STM32WLx_MAX_POWER
#define STM32WLx_MAX_POWER 22
#endif

STM32WLE5JCInterface::STM32WLE5JCInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq,
                                           RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy)
    : SX126xInterface(hal, cs, irq, rst, busy)
{
}

bool STM32WLE5JCInterface::init()
{
    RadioLibInterface::init();

#ifdef STM32WLx_TCXO_VOLTAGE
    setTCXOVoltage(STM32WLx_TCXO_VOLTAGE);
#endif

    lora.setRfSwitchTable(rfswitch_pins, rfswitch_table);

    limitPower(STM32WLx_MAX_POWER);

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);

    LOG_INFO("STM32WLx init result %d", res);

    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

#endif // ARCH_STM32WL
