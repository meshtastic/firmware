#pragma once

#include "STM32WLxInterface.h"

/**
 * Our adapter for STM32WLE5JC radios
 */
class STM32WLE5JCInterface : public STM32WLxInterface<STM32WLx>
{
  public:
    STM32WLE5JCInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);

    /* https://wiki.seeedstudio.com/LoRa-E5_STM32WLE5JC_Module/
    * Wio-E5 module ONLY transmits through RFO_HP
    * Receive: PA4=1, PA5=0
    * Transmit(high output power, SMPS mode): PA4=0, PA5=1 */
    const RADIOLIB_PIN_TYPE rfswitch_pins[3] = {PA4, PA5, RADIOLIB_NC}; 

    const Module::RfSwitchMode_t rfswitch_table[4] = {
        {STM32WLx::MODE_IDLE,  {LOW,  LOW}},
        {STM32WLx::MODE_RX,    {HIGH, LOW}},
        {STM32WLx::MODE_TX_HP, {LOW, HIGH}},
        STM32WLx::END_OF_MODE_TABLE,
        };
};