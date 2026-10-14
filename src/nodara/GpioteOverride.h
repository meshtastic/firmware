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

#ifndef _GPIOTE_OVERRIDE_H_
#define _GPIOTE_OVERRIDE_H_

#if defined(Nodara)

#include <Arduino.h>     // g_ADigitalPinMap, PINS_COUNT
#include <WInterrupts.h> // voidFuncPtr, ISR_DEFERRED
#include <nrf.h>
#include <nrf_gpio.h>   // nrf_gpio_cfg_sense_input, nrf_gpio_cfg_sense_set, nrf_gpio_pin_read
#include <nrf_gpiote.h> // nrf_gpiote_te_is_enabled
#include <string.h>     // memset

// ─── Public API for SixKeyNavInput ────────────────────────────────────────────

extern "C" {

// Call once per button pin before enableGpiotePortCallback().
// physPin: physical nRF52 GPIO number (g_ADigitalPinMap[arduinoPin]).
// Configures the pin as input with pull-up and SENSE_LOW (active-low button).
void registerGpiotePortPin(uint32_t physPin);

// Call after all pins are registered.  cb is invoked from GPIOTE_IRQHandler
// every time any registered pin changes state (press or release).
// The SENSE direction is toggled for all pins inside the ISR to prevent
// continuous re-firing when a button is held (standard nRF52 PORT event idiom).
void enableGpiotePortCallback(voidFuncPtr cb);

// Undo registerGpiotePortPin: clear SENSE on the pin and remove it from the
// registered list.  If no pins remain, also disables the PORT interrupt.
void unregisterGpiotePortPin(uint32_t physPin);

// ─── attachInterrupt / detachInterrupt ────────────────────────────────────────
// Exact behavioural equivalent of WInterrupts.c — dedicated GPIOTE IN channels.

int attachInterrupt(uint32_t pin, voidFuncPtr callback, uint32_t mode);

void detachInterrupt(uint32_t pin);

// ─── GPIOTE_IRQHandler ────────────────────────────────────────────────────────
// Handles both PORT events (our 6 SENSE-configured buttons) and IN events
// (dedicated channels used by e.g. RadioLib for SX1262 DIO1).

void GPIOTE_IRQHandler();

} // extern "C"

#endif // Nodara
#endif // _GPIOTE_OVERRIDE_H_
