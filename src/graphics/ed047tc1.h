#ifndef _ED047TC1_H_
#define _ED047TC1_H_

#if defined(T5_S3_EPAPER_PRO)

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/***        include files                                                   ***/
/******************************************************************************/

#include <driver/gpio.h>

#include <stdint.h>

/******************************************************************************/
/***        macro definitions                                               ***/
/******************************************************************************/

/* Config Reggister Control */
#define CFG_DATA GPIO_NUM_2
#define CFG_CLK GPIO_NUM_42
#define CFG_STR GPIO_NUM_1

/* Control Lines */
#define CKV GPIO_NUM_39
#define STH GPIO_NUM_9

/* Edges */
#define CKH GPIO_NUM_10

/* Data Lines */
#define D7 GPIO_NUM_38
#define D6 GPIO_NUM_45
#define D5 GPIO_NUM_47
#define D4 GPIO_NUM_21
#define D3 GPIO_NUM_14
#define D2 GPIO_NUM_13
#define D1 GPIO_NUM_12
#define D0 GPIO_NUM_11

#else
#error "Unknown SOC"
#endif

/******************************************************************************/
/***        type definitions                                                ***/
/******************************************************************************/

/******************************************************************************/
/***        exported variables                                              ***/
/******************************************************************************/

/******************************************************************************/
/***        exported functions                                              ***/
/******************************************************************************/

void epd_base_init(uint32_t epd_row_width);
void epd_poweron();
void epd_poweroff();

/**
 * @brief Start a draw cycle.
 */
void epd_start_frame();

/**
 * @brief End a draw cycle.
 */
void epd_end_frame();

/**
 * @brief output row data
 *
 * @note Waits until all previously submitted data has been written.
 *       Then, the following operations are initiated:
 *
 *           1. Previously submitted data is latched to the output register.
 *           2. The RMT peripheral is set up to pulse the vertical (gate) driver
 *              for `output_time_dus` / 10 microseconds.
 *           3. The I2S peripheral starts transmission of the current buffer to
 *              the source driver.
 *           4. The line buffers are switched.
 *
 *       This sequence of operations allows for pipelining data preparation and
 *       transfer, reducing total refresh times.
 */
void IRAM_ATTR epd_output_row(uint32_t output_time_dus);

/**
 * @brief Skip a row without writing to it.
 */
void IRAM_ATTR epd_skip();

/**
 * @brief Get the currently writable line buffer.
 */
uint8_t *IRAM_ATTR epd_get_current_buffer();

/**
 * @brief Switches front and back line buffer.
 *
 * @note If the switched-to line buffer is currently in use, this function
 *       blocks until transmission is done.
 */
void IRAM_ATTR epd_switch_buffer();

#ifdef __cplusplus
}
#endif

#endif
/******************************************************************************/
/***        END OF FILE                                                     ***/
/******************************************************************************/