/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico.h"

// For MHZ definitions etc
#include "hardware/clocks.h"
#include "hardware/rosc.h"

// Given a ROSC delay stage code, return the next-numerically-higher code.
// Top result bit is set when called on maximum ROSC code.
uint32_t next_rosc_code(uint32_t code)
{
    return ((code | 0x08888888u) + 1u) & 0xf7777777u;
}

uint rosc_find_freq(uint32_t low_mhz, uint32_t high_mhz)
{
    // TODO: This could be a lot better
    rosc_set_div(1);
    for (uint32_t code = 0; code <= 0x77777777u; code = next_rosc_code(code)) {
        rosc_set_freq(code);
        uint rosc_mhz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC) / 1000;
        if ((rosc_mhz >= low_mhz) && (rosc_mhz <= high_mhz)) {
            return rosc_mhz;
        }
    }
    return 0;
}

void rosc_set_div(uint32_t div)
{
    assert(div <= 31 && div >= 1);
    rosc_write(&rosc_hw->div, ROSC_DIV_VALUE_PASS + div);
}

void rosc_set_freq(uint32_t code)
{
    rosc_write(&rosc_hw->freqa, (ROSC_FREQA_PASSWD_VALUE_PASS << ROSC_FREQA_PASSWD_LSB) | (code & 0xffffu));
    rosc_write(&rosc_hw->freqb, (ROSC_FREQA_PASSWD_VALUE_PASS << ROSC_FREQA_PASSWD_LSB) | (code >> 16u));
}

void rosc_set_range(uint range)
{
    // Range should use enumvals from the headers and thus have the password correct
    rosc_write(&rosc_hw->ctrl, (ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB) | range);
}

void rosc_disable(void)
{
    uint32_t tmp = rosc_hw->ctrl;
    tmp &= (~ROSC_CTRL_ENABLE_BITS);
    tmp |= (ROSC_CTRL_ENABLE_VALUE_DISABLE << ROSC_CTRL_ENABLE_LSB);
    rosc_write(&rosc_hw->ctrl, tmp);
    // Wait for stable to go away
    while (rosc_hw->status & ROSC_STATUS_STABLE_BITS)
        ;
}

void rosc_set_dormant(void)
{
    // WARNING: This stops the rosc until woken up by an irq
    rosc_write(&rosc_hw->dormant, ROSC_DORMANT_VALUE_DORMANT);
    // Wait for it to become stable once woken up
    while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS))
        ;
}