/*
 * Host-side harness: builds the probe's command layer against a stub chip and prints every SPI
 * frame it would send. No hardware, no cross-compiler.
 *
 *   cc -std=c11 -Wall -Wextra -I../src -o trace_host trace_host.c ../src/lr2021_probe.c && ./trace_host
 *
 * The output is the init command stream as bytes on the wire, which is the thing to diff against
 * another driver's trace for the same configuration.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lr2021_probe.h"

static uint32_t fake_millis_value;
static uint16_t last_opcode;
static bool expect_reply;
/* what the stub chip reports on the next passive IRQ read */
static uint32_t stub_irq;

static void dump(const char *dir, const uint8_t *buf, size_t len)
{
    printf("  %s ", dir);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

static void stub_spi(const uint8_t *tx, uint8_t *rx, size_t len)
{
    memset(rx, 0, len);

    /* the passive IRQ read: six bytes of zeros, no opcode */
    if (!expect_reply && len == 6 && tx[0] == 0x00 && tx[1] == 0x00) {
        rx[2] = (uint8_t)(stub_irq >> 24);
        rx[3] = (uint8_t)(stub_irq >> 16);
        rx[4] = (uint8_t)(stub_irq >> 8);
        rx[5] = (uint8_t)stub_irq;
        dump("<>", rx, len);
        return;
    }

    /* the direct FIFO read: one frame, opcode then data, no stat bytes */
    if (!expect_reply && len > 2 && tx[0] == 0x00 && tx[1] == 0x01) {
        const char *canned = "P3 F04 CR04";
        memcpy(&rx[2], canned, strlen(canned) < len - 2 ? strlen(canned) : len - 2);
        dump("<>", rx, len);
        return;
    }

    if (expect_reply) {
        /* second frame of a read: two stat bytes then the reply the chip would give */
        rx[0] = 0x00;
        rx[1] = 0x00;
        switch (last_opcode) {
        case 0x0101: /* GetVersion */
            rx[2] = 0x01;
            rx[3] = 0x18;
            break;
        case 0x0110: /* GetErrors */
            break;
        case 0x0106: /* ReadRegMem32 - a plausible packet-status word */
            rx[2] = 0x62;
            rx[3] = 0x41;
            rx[4] = 0x01;
            rx[5] = 0x14;
            break;
        case 0x0212: /* GetRxPktLength */
            rx[2] = 0x00;
            rx[3] = 0x0B;
            break;
        case 0x022A:      /* GetLoraPacketStatus */
            rx[2] = 0x15; /* crc set, cr = 0x05: the accumulated value */
            rx[3] = 0x0C;
            rx[4] = 0x28;
            rx[5] = 0x78;
            break;
        default:
            break;
        }
        dump("<-", rx, len);
        expect_reply = false;
        return;
    }

    dump("->", tx, len);
    last_opcode = (uint16_t)(((uint16_t)tx[0] << 8) | tx[1]);
    /* the reads the init path uses */
    if (last_opcode == 0x0101 || last_opcode == 0x0110 || last_opcode == 0x0106 || last_opcode == 0x022A ||
        last_opcode == 0x0212) {
        expect_reply = true;
    }
}

static bool stub_busy(void)
{
    return false;
}

static void stub_reset(bool low)
{
    printf("  NRESET %s\n", low ? "low" : "high");
}

static void stub_delay(uint32_t ms)
{
    fake_millis_value += ms;
}

static uint32_t stub_millis(void)
{
    return ++fake_millis_value;
}

static void stub_log(const char *line)
{
    printf("%s\n", line);
}

int main(void)
{
    const lr2021_hal_t hal = {
        .spi_xfer = stub_spi,
        .busy_high = stub_busy,
        .reset_drive_low = stub_reset,
        .delay_ms = stub_delay,
        .millis = stub_millis,
        .log_line = stub_log,
    };

    printf("==== RX role init ====\n");
    int rc = lr2021_probe_begin(&hal, LR2021_ROLE_RX);
    printf("begin rc=%d\n", rc);

    printf("\n==== one RX reception, traced ====\n");
    lr2021_probe_set_trace(true);
    stub_irq = (1UL << 18); /* RX_DONE */
    rc = lr2021_probe_rx_poll();
    printf("rx_poll rc=%d\n", rc);
    lr2021_probe_rx_summary();

    printf("\n==== TX role init ====\n");
    lr2021_probe_set_trace(false);
    rc = lr2021_probe_begin(&hal, LR2021_ROLE_TX);
    printf("begin rc=%d\n", rc);

    printf("\n==== TX: first two frames of phase 1, traced ====\n");
    lr2021_probe_set_trace(true);
    stub_irq = (1UL << 17); /* TX_DONE */
    rc = lr2021_probe_tx_sequence();
    printf("tx_sequence rc=%d\n", rc);

    return 0;
}
