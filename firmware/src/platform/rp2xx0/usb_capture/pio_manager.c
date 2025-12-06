/**
 * @file pio_manager.c
 * @brief PIO state machine management implementation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pio_manager.h"
#include "common.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include <string.h>

/* Include generated PIO header - this MUST be the generated file */
#include "okhi.pio.h"

void pio_manager_init(void)
{
    /* Nothing to initialize currently */
}

float pio_manager_calculate_clock_divider(bool full_speed)
{
    float target_frequency_hz = full_speed ? 120000000.0f : 15000000.0f;
    float sys_clk_hz = (float)clock_get_hz(clk_sys);
    return sys_clk_hz / target_frequency_hz;
}

static void free_all_pio_state_machines(PIO pio)
{
    for (int sm = 0; sm < 4; sm++)
    {
        if (pio_sm_is_claimed(pio, sm))
        {
            pio_sm_unclaim(pio, sm);
        }
    }
}

void pio_manager_destroy_all(void)
{
    free_all_pio_state_machines(pio0);
    free_all_pio_state_machines(pio1);
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);
}

bool pio_manager_configure_capture(pio_config_t *config, bool full_speed)
{
    /* Create a modifiable copy of the PIO program - CRITICAL */
    uint16_t tar_pio0_program_instructions_current[32];
    struct pio_program tar_pio0_program_current;

    memcpy(&tar_pio0_program_current, &tar_pio0_program, sizeof(tar_pio0_program_current));
    tar_pio0_program_current.instructions = tar_pio0_program_instructions_current;
    memcpy(tar_pio0_program_instructions_current, tar_pio0_program_instructions,
           tar_pio0_program.length * sizeof(uint16_t));

    /* Clean slate - destroy any existing PIO configuration */
    pio_manager_destroy_all();

    /* Get speed-specific template instructions */
    const uint16_t *instructions = full_speed ? usb_full_speed_template_program_instructions : usb_low_speed_template_program_instructions;

    /* Add template program FIRST - this is required by the original code */
    pio_add_program(pio0, full_speed ? &usb_full_speed_template_program : &usb_low_speed_template_program);

    /* Patch in speed-specific wait instructions into the main program */
    tar_pio0_program_instructions_current[1] = instructions[0];
    tar_pio0_program_instructions_current[2] = instructions[1];

    /* Calculate clock divider */
    float div = pio_manager_calculate_clock_divider(full_speed);

    /* Determine which pin to use for jump condition */
    uint jmp_pin = full_speed ? DP_INDEX : DM_INDEX;

    /* SECOND destroy - clean up the template, prepare for actual programs */
    pio_manager_destroy_all();

    /* Initialize GPIO pins */
    gpio_init(START_INDEX);
    gpio_set_dir(START_INDEX, GPIO_OUT);
    gpio_put(START_INDEX, false);
    gpio_init(DP_INDEX);
    gpio_set_dir(DP_INDEX, GPIO_IN);
    gpio_init(DM_INDEX);
    gpio_set_dir(DM_INDEX, GPIO_IN);

    /* Configure PIO1 (synchronization) */
    pio_gpio_init(pio1, START_INDEX);
    int pio1_sm = pio_claim_unused_sm(pio1, true);
    uint offset_pio1 = pio_add_program(pio1, &tar_pio1_program);
    pio_sm_set_consecutive_pindirs(pio1, pio1_sm, START_INDEX, 1, true);
    pio_sm_set_consecutive_pindirs(pio1, pio1_sm, DP_INDEX, 2, false);
    pio_sm_config c_pio1 = tar_pio1_program_get_default_config(offset_pio1);
    sm_config_set_set_pins(&c_pio1, START_INDEX, 1);
    sm_config_set_in_shift(&c_pio1, false, false, 0);
    sm_config_set_out_shift(&c_pio1, false, false, 0);
    sm_config_set_in_pins(&c_pio1, DP_INDEX);
    sm_config_set_clkdiv(&c_pio1, div);
    pio_sm_init(pio1, pio1_sm, offset_pio1, &c_pio1);
    pio_sm_set_enabled(pio1, pio1_sm, false);
    pio_sm_clear_fifos(pio1, pio1_sm);
    pio_sm_restart(pio1, pio1_sm);
    pio_sm_clkdiv_restart(pio1, pio1_sm);

    /* Configure PIO0 (data capture) with the PATCHED program */
    int pio0_sm = pio_claim_unused_sm(pio0, true);
    uint offset_pio0 = pio_add_program(pio0, &tar_pio0_program_current);
    pio_sm_set_consecutive_pindirs(pio0, pio0_sm, DP_INDEX, 3, false);
    pio_sm_config c_pio0 = tar_pio0_program_get_default_config(offset_pio0);
    sm_config_set_in_pins(&c_pio0, DP_INDEX);
    sm_config_set_jmp_pin(&c_pio0, jmp_pin);
    sm_config_set_in_shift(&c_pio0, false, true, 31);
    sm_config_set_out_shift(&c_pio0, false, false, 32);
    sm_config_set_fifo_join(&c_pio0, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c_pio0, div);
    pio_sm_init(pio0, pio0_sm, offset_pio0, &c_pio0);
    pio_sm_set_enabled(pio0, pio0_sm, false);
    pio_sm_clear_fifos(pio0, pio0_sm);
    pio_sm_restart(pio0, pio0_sm);
    pio_sm_clkdiv_restart(pio0, pio0_sm);

    /* Start both state machines with proper initialization sequence */
    pio_sm_exec(pio0, pio0_sm, pio_encode_jmp(31));
    pio_sm_set_enabled(pio0, pio0_sm, true);
    pio_sm_exec(pio0, pio0_sm, pio_encode_jmp(31));
    pio_sm_clear_fifos(pio0, pio0_sm);
    pio_sm_set_enabled(pio1, pio1_sm, true);

    /* Store configuration */
    config->pio0_instance = pio0;
    config->pio1_instance = pio1;
    config->pio0_sm = pio0_sm;
    config->pio1_sm = pio1_sm;
    config->pio0_offset = offset_pio0;
    config->pio1_offset = offset_pio1;
    config->initialized = true;

    return true;
}

void pio_manager_stop_capture(pio_config_t *config)
{
    if (!config->initialized)
    {
        return;
    }

    /* Disable state machines */
    pio_sm_set_enabled(config->pio0_instance, config->pio0_sm, false);
    pio_sm_set_enabled(config->pio1_instance, config->pio1_sm, false);

    config->initialized = false;
}