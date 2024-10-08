/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _HARDWARE_ROSC_H_
#define _HARDWARE_ROSC_H_

#include "hardware/structs/rosc.h"
#include "pico.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file rosc.h
 *  \defgroup hardware_rosc hardware_rosc
 *
 * Ring Oscillator (ROSC) API
 *
 * A Ring Oscillator is an on-chip oscillator that requires no external crystal. Instead, the output is generated from a series of
 * inverters that are chained together to create a feedback loop. RP2040 boots from the ring oscillator initially, meaning the
 * first stages of the bootrom, including booting from SPI flash, will be clocked by the ring oscillator. If your design has a
 * crystal oscillator, you’ll likely want to switch to this as your reference clock as soon as possible, because the frequency is
 * more accurate than the ring oscillator.
 */

/*! \brief  Set frequency of the Ring Oscillator
 *  \ingroup hardware_rosc
 *
 * \param code The drive strengths. See the RP2040 datasheet for information on this value.
 */
void rosc_set_freq(uint32_t code);

/*! \brief  Set range of the Ring Oscillator
 *  \ingroup hardware_rosc
 *
 * Frequency range. Frequencies will vary with Process, Voltage & Temperature (PVT).
 * Clock output will not glitch when changing the range up one step at a time.
 *
 * \param range 0x01 Low, 0x02 Medium, 0x03 High, 0x04 Too High.
 */
void rosc_set_range(uint range);

/*! \brief  Disable the Ring Oscillator
 *  \ingroup hardware_rosc
 *
 */
void rosc_disable(void);

/*! \brief  Put Ring Oscillator in to dormant mode.
 *  \ingroup hardware_rosc
 *
 * The ROSC supports a dormant mode,which stops oscillation until woken up up by an asynchronous interrupt.
 * This can either come from the RTC, being clocked by an external clock, or a GPIO pin going high or low.
 * If no IRQ is configured before going into dormant mode the ROSC will never restart.
 *
 * PLLs should be stopped before selecting dormant mode.
 */
void rosc_set_dormant(void);

// FIXME: Add doxygen

uint32_t next_rosc_code(uint32_t code);

uint rosc_find_freq(uint32_t low_mhz, uint32_t high_mhz);

void rosc_set_div(uint32_t div);

inline static void rosc_clear_bad_write(void)
{
    hw_clear_bits(&rosc_hw->status, ROSC_STATUS_BADWRITE_BITS);
}

inline static bool rosc_write_okay(void)
{
    return !(rosc_hw->status & ROSC_STATUS_BADWRITE_BITS);
}

inline static void rosc_write(io_rw_32 *addr, uint32_t value)
{
    rosc_clear_bad_write();
    assert(rosc_write_okay());
    *addr = value;
    assert(rosc_write_okay());
};

#ifdef __cplusplus
}
#endif

#endif