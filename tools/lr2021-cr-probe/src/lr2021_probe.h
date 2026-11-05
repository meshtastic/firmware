/*
 * LR2021 coding-rate probe - no radio driver, no library.
 *
 * Every SPI frame this file sends is built here, byte by byte, so the whole command stream is
 * visible in one place and can be diffed against another driver's. The host port supplies six
 * functions through lr2021_hal_t; nothing else is needed.
 *
 * Protocol, from LR20xx datasheet rev 2.2 section 5.4:
 *   - a command is a 16-bit opcode, big-endian, followed by its arguments;
 *   - BUSY must be low before a frame starts;
 *   - a write is one frame;
 *   - a read is two frames: the first carries the opcode and arguments, then BUSY goes low when
 *     the reply is ready, and the second clocks it out. The second frame returns the 16 stat bits
 *     first, so the first two bytes are discarded and the reply bytes follow;
 *   - the radio FIFO read (opcode 0x0001) is the exception: one frame, no stat bytes, data
 *     follows the opcode directly (datasheet section 5.4.1.3, "Direct Read").
 */

#ifndef LR2021_PROBE_H
#define LR2021_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The host port implements these. spi_xfer sends and receives one whole frame with NSS asserted
 * for its duration; tx and rx are both len bytes and either may alias a scratch buffer. */
typedef struct {
    void (*spi_xfer)(const uint8_t *tx, uint8_t *rx, size_t len);
    bool (*busy_high)(void);
    void (*reset_drive_low)(bool low);
    void (*delay_ms)(uint32_t ms);
    uint32_t (*millis)(void);
    void (*log_line)(const char *line);
} lr2021_hal_t;

typedef enum {
    LR2021_ROLE_RX = 0,
    LR2021_ROLE_TX = 1,
} lr2021_role_t;

/* Reset, identify and configure the chip for LoRa. Returns 0 on success, negative on failure.
 * The sequence mirrors RadioLib's LR2021::begin() command for command; see lr2021_probe.c. */
int lr2021_probe_begin(const lr2021_hal_t *hal, lr2021_role_t role);

/* RX: poll once. Handles at most one reception per call and logs a line for it.
 * Returns 1 if a reception was handled, 0 if not, negative on an SPI error. */
int lr2021_probe_rx_poll(void);

/* RX: print the running summary, or write the configured coding rate back into the packet-status
 * nibble (a mechanism experiment, not a fix - the address is undocumented). */
void lr2021_probe_rx_summary(void);
int lr2021_probe_clear_cr_nibble(void);

/* The coding rate the chip reported for the last reception, and the one both ends are configured
 * for. 0xFF means nothing has been received yet. */
uint8_t lr2021_probe_rx_last_cr(void);
uint8_t lr2021_probe_configured_cr(void);

/* TX: run the five-phase sequence once. Blocks for roughly fifteen seconds. */
int lr2021_probe_tx_sequence(void);

/* Log every SPI frame in hex. Off by default; the init trace is printed regardless. */
void lr2021_probe_set_trace(bool on);

#endif /* LR2021_PROBE_H */
