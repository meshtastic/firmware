// GpioteOverride.cpp — GPIOTE interrupt handler for eink-ra01s-gp02.
//
// Replaces WInterrupts.c from libFrameworkArduino.a by providing the same
// three public symbols it exports:
//
//   int  attachInterrupt(uint32_t pin, voidFuncPtr callback, uint32_t mode)
//   void detachInterrupt(uint32_t pin)
//   void GPIOTE_IRQHandler(void)
//
// Because this translation unit defines all three, the linker has no unresolved
// references that would pull WInterrupts.c.o out of the archive, so there is
// no multiple-definition conflict and no extra linker flag is required.
//
// Extension over WInterrupts.c:
//   Adds GPIOTE PORT event handling (GPIO SENSE / DETECT path).
//   Cost: ~2.4 µA System ON sleep for all 6 buttons combined.
//   Compare: ~17 µA for the first dedicated GPIOTE IN channel.
//
// Usage by SixKeyNavInput:
//   registerGpiotePortPin(physPin)    — configure a pin for SENSE + PORT event
//   enableGpiotePortCallback(cb)      — start PORT event interrupts

#include "GpioteOverride.h"

#if defined(Nodara)

#include <Arduino.h>     // g_ADigitalPinMap, PINS_COUNT
#include <WInterrupts.h> // voidFuncPtr, ISR_DEFERRED
#include <nrf.h>
#include <nrf_gpio.h>   // nrf_gpio_cfg_sense_input, nrf_gpio_cfg_sense_set, nrf_gpio_pin_read
#include <nrf_gpiote.h> // nrf_gpiote_te_is_enabled
#include <string.h>     // memset

// ─── Constants ────────────────────────────────────────────────────────────────

#if defined(NRF52) || defined(NRF52_SERIES)
static constexpr int N_GPIOTE_CH = 8;
#else
static constexpr int N_GPIOTE_CH = 4;
#endif

#ifdef GPIOTE_CONFIG_PORT_Msk
#define GPIOTE_PORT_PIN_MSK (GPIOTE_CONFIG_PORT_Msk | GPIOTE_CONFIG_PSEL_Msk)
#else
#define GPIOTE_PORT_PIN_MSK GPIOTE_CONFIG_PSEL_Msk
#endif

// ─── IN-event channel state (mirrors WInterrupts.c internals) ─────────────────

static voidFuncPtr s_cb[N_GPIOTE_CH] = {};
static int8_t s_map[N_GPIOTE_CH] = {-1, -1, -1, -1, -1, -1, -1, -1};
static bool s_init = false;

// ─── PORT-event state ─────────────────────────────────────────────────────────

static constexpr uint8_t MAX_PORT_PINS = 8;
static uint32_t s_portPins[MAX_PORT_PINS] = {}; // physical nRF GPIO pin numbers
static uint8_t s_portPinCount = 0;
static voidFuncPtr s_portCb = nullptr;

// ─── Internal helpers ─────────────────────────────────────────────────────────

static void initNVIC()
{
    NVIC_DisableIRQ(GPIOTE_IRQn);
    NVIC_ClearPendingIRQ(GPIOTE_IRQn);
    NVIC_SetPriority(GPIOTE_IRQn, 3);
    NVIC_EnableIRQ(GPIOTE_IRQn);
}

// ─── Public API for SixKeyNavInput ────────────────────────────────────────────

extern "C" {

// Call once per button pin before enableGpiotePortCallback().
// physPin: physical nRF52 GPIO number (g_ADigitalPinMap[arduinoPin]).
// Configures the pin as input with pull-up and SENSE_LOW (active-low button).
void registerGpiotePortPin(uint32_t physPin)
{
    if (s_portPinCount >= MAX_PORT_PINS)
        return;
    nrf_gpio_cfg_sense_input(physPin, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    s_portPins[s_portPinCount++] = physPin;
}

// Call after all pins are registered.  cb is invoked from GPIOTE_IRQHandler
// every time any registered pin changes state (press or release).
// The SENSE direction is toggled for all pins inside the ISR to prevent
// continuous re-firing when a button is held (standard nRF52 PORT event idiom).
void enableGpiotePortCallback(voidFuncPtr cb)
{
    if (!s_init) {
        memset(s_cb, 0, sizeof(s_cb));
        memset(s_map, -1, sizeof(s_map));
        initNVIC();
        s_init = true;
    }
    s_portCb = cb;
    NRF_GPIOTE->EVENTS_PORT = 0;
    NRF_GPIOTE->INTENSET = GPIOTE_INTENSET_PORT_Msk;
}

// Undo registerGpiotePortPin: clear SENSE, remove from list.
void unregisterGpiotePortPin(uint32_t physPin)
{
    for (uint8_t i = 0; i < s_portPinCount; i++) {
        if (s_portPins[i] != physPin)
            continue;

        // Restore to plain input with pull-up, no SENSE.
        nrf_gpio_cfg_input(physPin, NRF_GPIO_PIN_PULLUP);

        // Remove from array by shifting remaining entries.
        s_portPinCount--;
        for (uint8_t j = i; j < s_portPinCount; j++)
            s_portPins[j] = s_portPins[j + 1];
        s_portPins[s_portPinCount] = 0;

        // If no pins left, disable PORT interrupt to save power.
        if (s_portPinCount == 0) {
            NRF_GPIOTE->INTENCLR = GPIOTE_INTENCLR_PORT_Msk;
            NRF_GPIOTE->EVENTS_PORT = 0;
            s_portCb = nullptr;
        }

        return;
    }
}

// ─── attachInterrupt / detachInterrupt ────────────────────────────────────────
// Exact behavioural equivalent of WInterrupts.c — dedicated GPIOTE IN channels.

int attachInterrupt(uint32_t pin, voidFuncPtr callback, uint32_t mode)
{
    if (!s_init) {
        memset(s_cb, 0, sizeof(s_cb));
        memset(s_map, -1, sizeof(s_map));
        initNVIC();
        s_init = true;
    }

    if (pin >= PINS_COUNT)
        return 0;
    pin = g_ADigitalPinMap[pin];

    // Strip ISR_DEFERRED flag (we don't support deferred callbacks here).
    uint32_t polarity;
    switch (mode & ~ISR_DEFERRED) {
    case CHANGE:
        polarity = GPIOTE_CONFIG_POLARITY_Toggle;
        break;
    case FALLING:
        polarity = GPIOTE_CONFIG_POLARITY_HiToLo;
        break;
    case RISING:
        polarity = GPIOTE_CONFIG_POLARITY_LoToHi;
        break;
    default:
        return 0;
    }

    const uint32_t oldMask = ~(GPIOTE_PORT_PIN_MSK | GPIOTE_CONFIG_POLARITY_Msk | GPIOTE_CONFIG_MODE_Msk);
    const uint32_t newBits = ((pin << GPIOTE_CONFIG_PSEL_Pos) & GPIOTE_PORT_PIN_MSK) |
                             ((polarity << GPIOTE_CONFIG_POLARITY_Pos) & GPIOTE_CONFIG_POLARITY_Msk) |
                             ((GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos) & GPIOTE_CONFIG_MODE_Msk);

    // Find existing channel for this pin, or allocate a free one.
    int ch = -1, isNew = 0;
    for (int i = 0; i < N_GPIOTE_CH; i++) {
        if ((uint32_t)s_map[i] == pin) {
            ch = i;
            break;
        }
    }
    if (ch == -1) {
        for (int i = 0; i < N_GPIOTE_CH; i++) {
            if (s_map[i] != -1)
                continue;
            if (nrf_gpiote_te_is_enabled(NRF_GPIOTE, i))
                continue;
            ch = i;
            isNew = 1;
            break;
        }
    }
    if (ch == -1)
        return 0; // all channels full

    s_map[ch] = pin;
    s_cb[ch] = callback;
    NRF_GPIOTE->CONFIG[ch] = (NRF_GPIOTE->CONFIG[ch] & oldMask) | newBits;
    if (isNew) {
        NRF_GPIOTE->EVENTS_IN[ch] = 0;
        NRF_GPIOTE->INTENSET = (1u << ch);
    }
    return (1 << ch);
}

void detachInterrupt(uint32_t pin)
{
    if (pin >= PINS_COUNT)
        return;
    pin = g_ADigitalPinMap[pin];

    for (int ch = 0; ch < N_GPIOTE_CH; ch++) {
        if ((uint32_t)s_map[ch] != pin)
            continue;
        NRF_GPIOTE->INTENCLR = (1u << ch);
        NRF_GPIOTE->CONFIG[ch] = 0;
        NRF_GPIOTE->EVENTS_IN[ch] = 0;
        s_map[ch] = -1;
        s_cb[ch] = nullptr;
        break;
    }
}

// ─── GPIOTE_IRQHandler ────────────────────────────────────────────────────────
// Handles both PORT events (our 6 SENSE-configured buttons) and IN events
// (dedicated channels used by e.g. RadioLib for SX1262 DIO1).

void GPIOTE_IRQHandler()
{
    // ── PORT event: one or more SENSE-configured pins changed state ───────────
    if (NRF_GPIOTE->EVENTS_PORT) {
        NRF_GPIOTE->EVENTS_PORT = 0;

        // Toggle SENSE for each registered pin so the NEXT edge fires again.
        // Rule: if pin is LOW now (pressed), watch for HIGH (release).
        //       if pin is HIGH now (released), watch for LOW (press).
        // After toggling, DETECT goes LOW for all pins → no immediate re-fire.
        for (uint8_t i = 0; i < s_portPinCount; i++) {
            uint32_t p = s_portPins[i];
            nrf_gpio_cfg_sense_set(p, nrf_gpio_pin_read(p) == 0 ? NRF_GPIO_PIN_SENSE_HIGH : NRF_GPIO_PIN_SENSE_LOW);
        }

        if (s_portCb)
            s_portCb();
    }

    // ── IN events: dedicated GPIOTE channels (e.g. SX1262 DIO1) ─────────────
    const uint32_t enabled = NRF_GPIOTE->INTENSET;
    for (int ch = 0; ch < N_GPIOTE_CH; ch++) {
        if (0 == (enabled & (1u << ch)))
            continue;
        if (0 == NRF_GPIOTE->EVENTS_IN[ch])
            continue;
        if (s_map[ch] != -1 && s_cb[ch]) {
            s_cb[ch]();
        }
        NRF_GPIOTE->EVENTS_IN[ch] = 0;
    }

#if __CORTEX_M == 0x04
    // Required errata workaround: DSB + NOPs ensure the event clear propagates
    // across the AHB bus before the IRQ handler exits.  See nRF52840 PS §6.1.8.
    __DSB();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
#endif
}

} // extern "C"

#endif // Nodara
