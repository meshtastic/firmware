#include <stdbool.h>
#include "architecture.h"
#include "stm32wlxx.h"
#include "stm32wlxx_hal.h"

void HardFault_Handler(void)
{
    asm("bkpt");
}

void stm32wl_emulate_digitalWrite(long unsigned int pin, long unsigned int value)
{
    switch (pin)
    {
        case SX126X_CS: /* active low */
            if (value)
                LL_PWR_UnselectSUBGHZSPI_NSS();
            else
                LL_PWR_SelectSUBGHZSPI_NSS();
            break;
        case SX126X_RESET: /* active low */
            if (value)
                LL_RCC_RF_DisableReset();
            else
            {
                LL_RCC_RF_EnableReset();
                LL_RCC_HSE_EnableTcxo();
                LL_RCC_HSE_Enable();
                while (!LL_RCC_HSE_IsReady());
            }
            break;
        default:
            asm("bkpt");
            break;
    }
}

static bool irq_happened;

void SUBGHZ_Radio_IRQHandler(void)
{
    NVIC_DisableIRQ(SUBGHZ_Radio_IRQn);
    irq_happened = true;   
}

int stm32wl_emulate_digitalRead(long unsigned int pin)
{
    int outcome = 0;

    switch (pin)
    {
        case SX126X_BUSY:
//            return ((LL_PWR_IsActiveFlag_RFBUSYMS() & LL_PWR_IsActiveFlag_RFBUSYS()) == 1UL);
            outcome = LL_PWR_IsActiveFlag_RFBUSYS();
            break;
        case SX126X_DIO1:
        default:
            NVIC_ClearPendingIRQ(SUBGHZ_Radio_IRQn);
            irq_happened = false;
            NVIC_EnableIRQ(SUBGHZ_Radio_IRQn);
            for (int i = 0; i < 64; i++) asm("nop");
            outcome = irq_happened;
            break;
    }
    return outcome;
}

