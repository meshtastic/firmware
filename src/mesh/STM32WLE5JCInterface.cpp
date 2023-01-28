#include "configuration.h"
#include "STM32WLE5JCInterface.h"
#include "error.h"

STM32WLE5JCInterface::STM32WLE5JCInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, 
                                           SPIClass &spi) 
    : STM32WLxInterface(cs, irq, rst, busy, spi, rfswitch_pins, rfswitch_table)
{  
    // https://github.com/Seeed-Studio/LoRaWan-E5-Node/blob/main/Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio_driver.c
    STM32WLxInterface::tcxoVoltage = 1.7;
}
