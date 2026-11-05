#include "lr2021_probe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- what the probe is configured for ---------------------------------------------------- */

#ifndef PROBE_FREQ_HZ
#define PROBE_FREQ_HZ 868100000UL
#endif
#ifndef PROBE_SF
#define PROBE_SF 7
#endif
#ifndef PROBE_BW
#define PROBE_BW LR2021_LORA_BW_500
#endif
#ifndef PROBE_POWER_DBM
#define PROBE_POWER_DBM 10
#endif
/* Set PROBE_USE_TCXO to 0 for a board with a plain crystal instead of a TCXO. */
#ifndef PROBE_USE_TCXO
#define PROBE_USE_TCXO 1
#endif
#ifndef PROBE_TCXO_CODE
#define PROBE_TCXO_CODE LR2021_TCXO_1_6V
#endif
#ifndef PROBE_TCXO_STARTUP_US
#define PROBE_TCXO_STARTUP_US 1000000UL
#endif
#ifndef PROBE_PREAMBLE
#define PROBE_PREAMBLE 8
#endif
#ifndef PROBE_SYNCWORD
#define PROBE_SYNCWORD 0x12
#endif
/* The DIO the IRQ is mapped to. We never read the pin - the probe polls GetIrqStatus - but the
 * command is issued anyway so the command stream matches the firmware's. */
#ifndef PROBE_IRQ_DIO
#define PROBE_IRQ_DIO 5
#endif
/* Semtech's own DCDC sensitivity workaround, as ported by RadioLib. 1 to apply it, 0 to leave it
 * out: running the probe both ways says whether it has anything to do with the accumulator. */
#ifndef PROBE_APPLY_DCDC_WORKAROUND
#define PROBE_APPLY_DCDC_WORKAROUND 1
#endif

/* The rate both ends are configured for: raw code 0x04 is 4/8 in datasheet Table 9-4. */
#define PROBE_CR_CONFIGURED 0x04

/* ---- datasheet constants ----------------------------------------------------------------- */

/* opcodes, datasheet section 5.6 */
#define LR2021_CMD_READ_RX_FIFO 0x0001
#define LR2021_CMD_WRITE_TX_FIFO 0x0002
#define LR2021_CMD_WRITE_REG_MEM_32 0x0104
#define LR2021_CMD_WRITE_REG_MEM_MASK_32 0x0105
#define LR2021_CMD_READ_REG_MEM_32 0x0106
#define LR2021_CMD_SET_STANDBY 0x0128
#define LR2021_CMD_SET_RX 0x020C
#define LR2021_CMD_SET_TX 0x020D
#define LR2021_CMD_SET_RX_TX_FALLBACK_MODE 0x0206
#define LR2021_CMD_GET_RX_PKT_LENGTH 0x0212
#define LR2021_CMD_RESET_RX_STATS 0x020A
#define LR2021_CMD_CALIBRATE 0x0122
#define LR2021_CMD_CALIB_FRONT_END 0x0123
#define LR2021_CMD_GET_STATUS 0x0100
#define LR2021_CMD_GET_VERSION 0x0101
#define LR2021_CMD_CLEAR_ERRORS 0x0111
#define LR2021_CMD_GET_ERRORS 0x0110
#define LR2021_CMD_SET_DIO_FUNCTION 0x0112
#define LR2021_CMD_SET_DIO_IRQ_CONFIG 0x0115
#define LR2021_CMD_CLEAR_IRQ 0x0116
#define LR2021_CMD_GET_IRQ_STATUS 0x0117 /* GetAndClearIrqStatus */
#define LR2021_CMD_CLEAR_TX_FIFO 0x011F
#define LR2021_CMD_SET_TCXO_MODE 0x0120
#define LR2021_CMD_SET_RF_FREQUENCY 0x0200
#define LR2021_CMD_SET_PA_CONFIG 0x0202
#define LR2021_CMD_SET_TX_PARAMS 0x0203
#define LR2021_CMD_SET_PACKET_TYPE 0x0207
#define LR2021_CMD_SET_LORA_MODULATION_PARAMS 0x0220
#define LR2021_CMD_SET_LORA_PACKET_PARAMS 0x0221
#define LR2021_CMD_SET_LORA_SYNCWORD 0x0223
#define LR2021_CMD_GET_LORA_PACKET_STATUS 0x022A

/* parameter values */
#define LR2021_STANDBY_RC 0x00
#define LR2021_FALLBACK_STBY_RC 0x01
#define LR2021_PACKET_TYPE_LORA 0x00
#define LR2021_CALIBRATE_ALL 0x6F
#define LR2021_CALIB_FE_LF_PATH 0x0000 /* bit 15 clear selects the low-frequency path */
#define LR2021_DIO_FUNCTION_IRQ 0x10
#define LR2021_DIO_SLEEP_PULL_DOWN 0x01
#define LR2021_DIO_SLEEP_PULL_UP 0x02
#define LR2021_TCXO_1_6V 0x00
#define LR2021_LORA_BW_125 0x04
#define LR2021_LORA_BW_250 0x05
#define LR2021_LORA_BW_500 0x06
#define LR2021_LORA_LDRO_OFF 0x00
#define LR2021_LORA_HEADER_EXPLICIT 0x00
#define LR2021_LORA_CRC_ON 0x01
#define LR2021_LORA_IQ_STANDARD 0x00
#define LR2021_MAX_PACKET_LENGTH 255
#define LR2021_PA_LF_MODE_FSM 0x00
#define LR2021_PA_HF_DUTY_UNUSED 0x10
#define LR2021_PA_RAMP_48US 0x05
#define LR2021_RX_TIMEOUT_INF 0xFFFFFFUL

/* IRQ bits, datasheet section 5.7 */
#define LR2021_IRQ_LORA_HDR_CRC_ERROR (1UL << 9)
#define LR2021_IRQ_TX_DONE (1UL << 17)
#define LR2021_IRQ_RX_DONE (1UL << 18)
#define LR2021_IRQ_TIMEOUT (1UL << 20)
#define LR2021_IRQ_CRC_ERROR (1UL << 22)
#define LR2021_IRQ_ALL 0xFFFFFFFFUL

/* errors, datasheet section 6.7 */
#define LR2021_HF_XOSC_START_ERR (1U << 1)

/* registers. The DCDC three are Semtech's own workaround addresses; 0x8003C4 is undocumented and
 * is only ever read here, or written by the explicit clear experiment. */
#define LR2021_REG_DCDC_FREQ_LF 0x80004CUL
#define LR2021_REG_DCDC_SWITCHER 0xF20024UL
#define LR2021_REG_DCDC_ADC_CTRL 0xF40200UL
#define LR2021_REG_PACKET_STATUS 0x8003C4UL

#define PROBE_ERR_SPI (-1)
#define PROBE_ERR_CHIP (-2)
#define PROBE_ERR_BUSY (-3)
#define PROBE_ERR_ARG (-4)

/* ---- plumbing ---------------------------------------------------------------------------- */

#define PROBE_MAX_FRAME (2 + 2 + 256)

static const lr2021_hal_t *hal;
static bool trace;
static char logbuf[200];

static void probe_logf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(logbuf, sizeof(logbuf), fmt, args);
    va_end(args);
    if (hal && hal->log_line) {
        hal->log_line(logbuf);
    }
}

static void trace_frame(const char *tag, uint16_t op, const uint8_t *bytes, size_t len)
{
    char hex[3 * 24 + 1];
    size_t n = len > 24 ? 24 : len;
    size_t pos = 0;
    for (size_t i = 0; i < n; i++) {
        pos += (size_t)snprintf(&hex[pos], sizeof(hex) - pos, "%02X ", bytes[i]);
    }
    hex[pos] = '\0';
    probe_logf("SPI %s %04X : %s%s", tag, op, hex, len > n ? "..." : "");
}

static int wait_busy_low(uint32_t timeout_ms)
{
    uint32_t start = hal->millis();
    while (hal->busy_high()) {
        if (hal->millis() - start >= timeout_ms) {
            probe_logf("! BUSY stuck high for %u ms", (unsigned)timeout_ms);
            return PROBE_ERR_BUSY;
        }
    }
    return 0;
}

/* One frame: opcode then arguments. */
static int cmd_write(uint16_t op, const uint8_t *args, size_t nargs)
{
    uint8_t tx[PROBE_MAX_FRAME];
    uint8_t rx[PROBE_MAX_FRAME];

    if (nargs + 2 > sizeof(tx)) {
        return PROBE_ERR_ARG;
    }
    int rc = wait_busy_low(1000);
    if (rc != 0) {
        return rc;
    }

    tx[0] = (uint8_t)(op >> 8);
    tx[1] = (uint8_t)(op & 0xFF);
    if (nargs) {
        memcpy(&tx[2], args, nargs);
    }
    if (trace) {
        trace_frame("W", op, args, nargs);
    }
    hal->spi_xfer(tx, rx, nargs + 2);
    return 0;
}

/* Two frames: the command, then the reply. The reply frame returns stat(15:0) first, so the two
 * leading bytes are dropped - datasheet section 5.4.1.2. */
static int cmd_read(uint16_t op, const uint8_t *args, size_t nargs, uint8_t *out, size_t nout)
{
    uint8_t tx[PROBE_MAX_FRAME];
    uint8_t rx[PROBE_MAX_FRAME];

    if (nout + 2 > sizeof(tx)) {
        return PROBE_ERR_ARG;
    }
    int rc = cmd_write(op, args, nargs);
    if (rc != 0) {
        return rc;
    }
    rc = wait_busy_low(1000);
    if (rc != 0) {
        return rc;
    }

    memset(tx, 0, nout + 2);
    hal->spi_xfer(tx, rx, nout + 2);
    memcpy(out, &rx[2], nout);
    if (trace) {
        trace_frame("R", op, out, nout);
    }
    return 0;
}

static int cmd_u32(uint16_t op, uint32_t value)
{
    uint8_t buf[4] = {
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };
    return cmd_write(op, buf, sizeof(buf));
}

static int reg_read32(uint32_t addr, uint32_t *value)
{
    uint8_t req[4] = {
        (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr, 1, /* word count */
    };
    uint8_t rpl[4] = {0};
    int rc = cmd_read(LR2021_CMD_READ_REG_MEM_32, req, sizeof(req), rpl, sizeof(rpl));
    if (rc != 0) {
        return rc;
    }
    *value = ((uint32_t)rpl[0] << 24) | ((uint32_t)rpl[1] << 16) | ((uint32_t)rpl[2] << 8) | rpl[3];
    return 0;
}

static int reg_write32(uint32_t addr, uint32_t value)
{
    uint8_t buf[7] = {
        (uint8_t)(addr >> 16),  (uint8_t)(addr >> 8),  (uint8_t)addr,  (uint8_t)(value >> 24),
        (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value,
    };
    return cmd_write(LR2021_CMD_WRITE_REG_MEM_32, buf, sizeof(buf));
}

static int reg_write_mask32(uint32_t addr, uint32_t mask, uint32_t value)
{
    uint8_t buf[11] = {
        (uint8_t)(addr >> 16),  (uint8_t)(addr >> 8),  (uint8_t)addr,  (uint8_t)(mask >> 24),
        (uint8_t)(mask >> 16),  (uint8_t)(mask >> 8),  (uint8_t)mask,  (uint8_t)(value >> 24),
        (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value,
    };
    return cmd_write(LR2021_CMD_WRITE_REG_MEM_MASK_32, buf, sizeof(buf));
}

/* The IRQ status needs no command at all: the datasheet (section 5.4.1.1) says MISO carries
 * stat(15:0) followed by IrqStatus(31:0) at the start of every frame, so one frame of zeros reads
 * it. This is what the firmware under test does - it does NOT issue GetAndClearIrqStatus - and the
 * flags are left standing until ClearIrq, which is why the caller clears explicitly. */
static int irq_read(uint32_t *irq)
{
    uint8_t tx[6] = {0};
    uint8_t rx[6] = {0};

    int rc = wait_busy_low(1000);
    if (rc != 0) {
        return rc;
    }
    hal->spi_xfer(tx, rx, sizeof(tx));
    *irq = ((uint32_t)rx[2] << 24) | ((uint32_t)rx[3] << 16) | ((uint32_t)rx[4] << 8) | rx[5];
    if (trace) {
        trace_frame("I", 0x0000, &rx[2], 4);
    }
    return 0;
}

/* ---- the chip's own workaround ----------------------------------------------------------- */

/* A port of Semtech's lr20xx_workarounds.c, by way of RadioLib. The magic numbers are Semtech's:
 * https://github.com/Lora-net/usp/blob/351b20153506/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_workarounds.c
 * Sub-GHz only here, since the probe runs on the LF path. */
static int apply_dcdc_workaround(void)
{
    uint32_t adc_ctrl = 0;
    int rc = reg_read32(LR2021_REG_DCDC_ADC_CTRL, &adc_ctrl);
    if (rc != 0) {
        return rc;
    }
    uint32_t ana_dec = (adc_ctrl >> 8) & 0x7;

    if (ana_dec == 1 || ana_dec == 2) {
        rc = reg_write_mask32(LR2021_REG_DCDC_SWITCHER, 0x0FUL << 20, 11UL << 20);
        if (rc == 0) {
            rc = reg_write_mask32(LR2021_REG_DCDC_SWITCHER, 0x0FUL << 16, 13UL << 16);
        }
    } else {
        rc = reg_write_mask32(LR2021_REG_DCDC_SWITCHER, 0x0FUL << 20, 15UL << 20);
        if (rc == 0) {
            rc = reg_write_mask32(LR2021_REG_DCDC_SWITCHER, 0x0FUL << 16, 15UL << 16);
        }
    }
    if (rc != 0) {
        return rc;
    }

    uint32_t freq_lf = (ana_dec == 1) ? 4508876UL : 2936012UL;
    rc = reg_write32(LR2021_REG_DCDC_FREQ_LF, freq_lf);
    if (rc != 0) {
        return rc;
    }
    probe_logf("# dcdc workaround applied, ana_dec=%u", (unsigned)ana_dec);

    /* the workaround ends by retuning, without recalibrating the front end */
    return cmd_u32(LR2021_CMD_SET_RF_FREQUENCY, PROBE_FREQ_HZ);
}

/* ---- configuration ----------------------------------------------------------------------- */

static int set_modulation_params(uint8_t sf, uint8_t bw, uint8_t cr)
{
    uint8_t buf[2] = {
        (uint8_t)(((sf & 0x0F) << 4) | (bw & 0x0F)),
        (uint8_t)(((cr & 0x0F) << 4) | LR2021_LORA_LDRO_OFF),
    };
    int rc = cmd_write(LR2021_CMD_SET_LORA_MODULATION_PARAMS, buf, sizeof(buf));
#if PROBE_APPLY_DCDC_WORKAROUND
    /* RadioLib runs the workaround after every modulation-params write, so the probe does too. */
    if (rc == 0) {
        rc = apply_dcdc_workaround();
    }
#endif
    return rc;
}

static int set_packet_params(uint16_t preamble, uint8_t payload_len)
{
    uint8_t buf[4] = {
        (uint8_t)(preamble >> 8),
        (uint8_t)preamble,
        payload_len,
        (uint8_t)(((LR2021_LORA_HEADER_EXPLICIT & 0x01) << 2) | ((LR2021_LORA_CRC_ON & 0x01) << 1) |
                  (LR2021_LORA_IQ_STANDARD & 0x01)),
    };
    return cmd_write(LR2021_CMD_SET_LORA_PACKET_PARAMS, buf, sizeof(buf));
}

/* 10 dBm on the low-frequency PA. The three numbers come from RadioLib's LF PA table entry for
 * 10 dBm (paDutyCycle 2, paSlices 1, paVal 38); the HF duty cycle is the "PA not used" code. */
static int set_output_power_10dbm(void)
{
    uint8_t pa[3] = {
        (uint8_t)((0 << 7) | LR2021_PA_LF_MODE_FSM),
        (uint8_t)((2 << 4) | 1),
        LR2021_PA_HF_DUTY_UNUSED,
    };
    int rc = cmd_write(LR2021_CMD_SET_PA_CONFIG, pa, sizeof(pa));
    if (rc != 0) {
        return rc;
    }
    uint8_t txp[2] = {38, LR2021_PA_RAMP_48US};
    return cmd_write(LR2021_CMD_SET_TX_PARAMS, txp, sizeof(txp));
}

static int chip_reset(void)
{
    hal->reset_drive_low(true);
    hal->delay_ms(10);
    hal->reset_drive_low(false);
    /* the datasheet gives the typical transition as 273 ms */
    hal->delay_ms(300);
    return wait_busy_low(3000);
}

static int set_frequency(uint32_t hz, bool calibrate_front_end)
{
    if (calibrate_front_end) {
        /* the front-end calibration takes the nearest multiple of 4 MHz, and bit 15 of the first
         * word selects the LF or HF path */
        uint16_t f0 = (uint16_t)(((hz / 1000000UL) + 2) / 4) | LR2021_CALIB_FE_LF_PATH;
        uint8_t buf[6] = {(uint8_t)(f0 >> 8), (uint8_t)f0, 0, 0, 0, 0};
        int rc = cmd_write(LR2021_CMD_CALIB_FRONT_END, buf, sizeof(buf));
        if (rc != 0) {
            return rc;
        }
        hal->delay_ms(5);
        rc = wait_busy_low(1000);
        if (rc != 0) {
            return rc;
        }
    }
    return cmd_u32(LR2021_CMD_SET_RF_FREQUENCY, hz);
}

int lr2021_probe_begin(const lr2021_hal_t *h, lr2021_role_t role)
{
    if (h == NULL || h->spi_xfer == NULL) {
        return PROBE_ERR_ARG;
    }
    hal = h;

    probe_logf("# LR2021 bare-metal CR probe, role=%s", role == LR2021_ROLE_RX ? "RX" : "TX");
    probe_logf("# freq=%u Hz SF%u bw_code=0x%02X cr=0x%02X power=%d dBm dcdc_workaround=%d", (unsigned)PROBE_FREQ_HZ,
               (unsigned)PROBE_SF, (unsigned)PROBE_BW, PROBE_CR_CONFIGURED, (int)PROBE_POWER_DBM,
               (int)PROBE_APPLY_DCDC_WORKAROUND);

    /* the init trace is always printed: it is the thing another driver's stream is compared to */
    bool trace_was = trace;
    trace = true;

    int rc = chip_reset();
    if (rc != 0) {
        return rc;
    }

    uint8_t ver[2] = {0};
    rc = cmd_read(LR2021_CMD_GET_VERSION, NULL, 0, ver, sizeof(ver));
    if (rc != 0) {
        return rc;
    }
    probe_logf("# chip firmware major=0x%02X minor=0x%02X", ver[0], ver[1]);
    if (ver[0] != 0x01) {
        probe_logf("! GetVersion major is not 0x01 - check wiring before trusting anything below");
        return PROBE_ERR_CHIP;
    }

    uint8_t standby = LR2021_STANDBY_RC;
    rc = cmd_write(LR2021_CMD_SET_STANDBY, &standby, 1);
    if (rc != 0) {
        return rc;
    }

#if PROBE_USE_TCXO
    {
        /* clear a stale oscillator-start error first, as the driver does */
        uint8_t err[2] = {0};
        rc = cmd_read(LR2021_CMD_GET_ERRORS, NULL, 0, err, sizeof(err));
        if (rc != 0) {
            return rc;
        }
        uint16_t errors = (uint16_t)(((uint16_t)err[0] << 8) | err[1]);
        if (errors & LR2021_HF_XOSC_START_ERR) {
            rc = cmd_write(LR2021_CMD_CLEAR_ERRORS, NULL, 0);
            if (rc != 0) {
                return rc;
            }
        }

        /* the startup delay is in 30.52 us ticks */
        uint32_t ticks = (uint32_t)(PROBE_TCXO_STARTUP_US / 30.52f);
        if (ticks == 0) {
            ticks = 1;
        }
        uint8_t tcxo[5] = {
            (uint8_t)(PROBE_TCXO_CODE & 0x07),
            (uint8_t)(ticks >> 24),
            (uint8_t)(ticks >> 16),
            (uint8_t)(ticks >> 8),
            (uint8_t)ticks,
        };
        rc = cmd_write(LR2021_CMD_SET_TCXO_MODE, tcxo, sizeof(tcxo));
        if (rc != 0) {
            return rc;
        }
    }
#endif

    uint8_t fallback = LR2021_FALLBACK_STBY_RC;
    rc = cmd_write(LR2021_CMD_SET_RX_TX_FALLBACK_MODE, &fallback, 1);
    if (rc == 0) {
        rc = cmd_u32(LR2021_CMD_CLEAR_IRQ, LR2021_IRQ_ALL);
    }
    if (rc != 0) {
        return rc;
    }

    {
        uint8_t dio[2] = {
            PROBE_IRQ_DIO,
            (uint8_t)(LR2021_DIO_FUNCTION_IRQ | (PROBE_IRQ_DIO == 5 ? LR2021_DIO_SLEEP_PULL_UP : LR2021_DIO_SLEEP_PULL_DOWN)),
        };
        rc = cmd_write(LR2021_CMD_SET_DIO_FUNCTION, dio, sizeof(dio));
        if (rc != 0) {
            return rc;
        }
    }

    uint8_t blocks = LR2021_CALIBRATE_ALL;
    rc = cmd_write(LR2021_CMD_CALIBRATE, &blocks, 1);
    if (rc != 0) {
        return rc;
    }
    hal->delay_ms(5);
    rc = wait_busy_low(1000);
    if (rc != 0) {
        return rc;
    }

    uint8_t modem = LR2021_PACKET_TYPE_LORA;
    rc = cmd_write(LR2021_CMD_SET_PACKET_TYPE, &modem, 1);
    if (rc != 0) {
        return rc;
    }

    rc = set_frequency(PROBE_FREQ_HZ, true);
    if (rc == 0) {
        rc = set_modulation_params(PROBE_SF, PROBE_BW, PROBE_CR_CONFIGURED);
    }
    if (rc != 0) {
        return rc;
    }

    uint8_t sync = PROBE_SYNCWORD;
    rc = cmd_write(LR2021_CMD_SET_LORA_SYNCWORD, &sync, 1);
    if (rc == 0 && role == LR2021_ROLE_TX) {
        rc = set_output_power_10dbm();
    }
    if (rc == 0) {
        rc = set_packet_params(PROBE_PREAMBLE, LR2021_MAX_PACKET_LENGTH);
    }
    if (rc != 0) {
        return rc;
    }

    rc = cmd_write(LR2021_CMD_RESET_RX_STATS, NULL, 0);
    if (rc != 0) {
        return rc;
    }

    if (role == LR2021_ROLE_RX) {
        const uint32_t rx_irqs = LR2021_IRQ_RX_DONE | LR2021_IRQ_CRC_ERROR | LR2021_IRQ_LORA_HDR_CRC_ERROR | LR2021_IRQ_TIMEOUT;
        uint8_t irqcfg[5] = {
            PROBE_IRQ_DIO, (uint8_t)(rx_irqs >> 24), (uint8_t)(rx_irqs >> 16), (uint8_t)(rx_irqs >> 8), (uint8_t)rx_irqs,
        };
        rc = cmd_write(LR2021_CMD_SET_DIO_IRQ_CONFIG, irqcfg, sizeof(irqcfg));
        if (rc == 0) {
            rc = cmd_u32(LR2021_CMD_CLEAR_IRQ, LR2021_IRQ_ALL);
        }
        if (rc == 0) {
            uint8_t to[3] = {
                (uint8_t)(LR2021_RX_TIMEOUT_INF >> 16),
                (uint8_t)(LR2021_RX_TIMEOUT_INF >> 8),
                (uint8_t)LR2021_RX_TIMEOUT_INF,
            };
            rc = cmd_write(LR2021_CMD_SET_RX, to, sizeof(to));
        }
        if (rc != 0) {
            return rc;
        }
    }

    trace = trace_was;
    probe_logf("# init complete");
    if (role == LR2021_ROLE_RX) {
        probe_logf("#    n reported configured crc rssi_dbm snr_q reg_8003C4 irq      payload");
    }
    return 0;
}

void lr2021_probe_set_trace(bool on)
{
    trace = on;
}

/* ---- RX ---------------------------------------------------------------------------------- */

static uint32_t rx_total;
static uint32_t rx_off_configured;
static uint8_t rx_first_cr = 0xFF;
static uint8_t rx_last_cr = 0xFF;

int lr2021_probe_rx_poll(void)
{
    const uint32_t wanted = LR2021_IRQ_RX_DONE | LR2021_IRQ_CRC_ERROR | LR2021_IRQ_LORA_HDR_CRC_ERROR;
    uint32_t irq = 0;

    /* GetAndClearIrqStatus, so the flags are consumed by this read */
    int rc = irq_read(&irq);
    if (rc != 0) {
        return rc;
    }
    if ((irq & wanted) == 0) {
        return 0;
    }

    /* Read the packet status before re-arming: once the chip is listening again the status
     * describes whatever it is receiving now, not the frame just taken. */
    uint8_t st[9] = {0};
    rc = cmd_read(LR2021_CMD_GET_LORA_PACKET_STATUS, NULL, 0, st, sizeof(st));
    if (rc != 0) {
        return rc;
    }
    uint8_t cr = (uint8_t)(st[0] & 0x0F);
    bool crc_ok = (st[0] & 0x10) != 0;
    uint8_t payload_len = st[1];
    int snr_q = (int8_t)st[2]; /* quarter-dB, the chip's own unit */
    int rssi_dbm = -((int)(((uint16_t)st[3] << 1) | ((st[5] >> 1) & 0x01)) / 2);

    uint32_t reg = 0;
    int rc_reg = reg_read32(LR2021_REG_PACKET_STATUS, &reg);

    /* read the payload out of the FIFO: one frame, opcode then data, no stat bytes */
    char payload[33] = {0};
    uint8_t pkt[2] = {0};
    uint16_t fifo_len = 0;
    if (cmd_read(LR2021_CMD_GET_RX_PKT_LENGTH, NULL, 0, pkt, sizeof(pkt)) == 0) {
        fifo_len = (uint16_t)(((uint16_t)pkt[0] << 8) | pkt[1]);
    }
    if (fifo_len > 0) {
        uint8_t tx[2 + 32] = {0};
        uint8_t rx[2 + 32] = {0};
        size_t take = fifo_len > 32 ? 32 : fifo_len;
        tx[0] = (uint8_t)(LR2021_CMD_READ_RX_FIFO >> 8);
        tx[1] = (uint8_t)(LR2021_CMD_READ_RX_FIFO & 0xFF);
        if (wait_busy_low(1000) == 0) {
            hal->spi_xfer(tx, rx, take + 2);
            for (size_t i = 0; i < take; i++) {
                payload[i] = (rx[i + 2] >= 0x20 && rx[i + 2] < 0x7F) ? (char)rx[i + 2] : '.';
            }
        }
    }

    rx_total++;
    if (rx_first_cr == 0xFF) {
        rx_first_cr = cr;
    }
    rx_last_cr = cr;
    if (cr != PROBE_CR_CONFIGURED) {
        rx_off_configured++;
    }

    probe_logf("%5u 0x%02X     0x%02X       %d   %-8d %-5d 0x%08X %08X %s%s%s", (unsigned)rx_total, cr, PROBE_CR_CONFIGURED,
               crc_ok ? 1 : 0, rssi_dbm, snr_q, (unsigned)(rc_reg == 0 ? reg : 0), (unsigned)irq, payload,
               cr != PROBE_CR_CONFIGURED ? "  OFF-CONFIGURED" : "", (irq & LR2021_IRQ_LORA_HDR_CRC_ERROR) ? "  HDR-ERR" : "");
    (void)payload_len;

    /* clear the standing flags, then re-arm */
    rc = cmd_u32(LR2021_CMD_CLEAR_IRQ, LR2021_IRQ_ALL);
    if (rc != 0) {
        return rc;
    }
    uint8_t to[3] = {
        (uint8_t)(LR2021_RX_TIMEOUT_INF >> 16),
        (uint8_t)(LR2021_RX_TIMEOUT_INF >> 8),
        (uint8_t)LR2021_RX_TIMEOUT_INF,
    };
    rc = cmd_write(LR2021_CMD_SET_RX, to, sizeof(to));
    return rc == 0 ? 1 : rc;
}

void lr2021_probe_rx_summary(void)
{
    probe_logf("# received=%u first=0x%02X last=0x%02X configured=0x%02X off_configured=%u", (unsigned)rx_total, rx_first_cr,
               rx_last_cr, PROBE_CR_CONFIGURED, (unsigned)rx_off_configured);
}

uint8_t lr2021_probe_rx_last_cr(void)
{
    return rx_last_cr;
}

uint8_t lr2021_probe_configured_cr(void)
{
    return PROBE_CR_CONFIGURED;
}

int lr2021_probe_clear_cr_nibble(void)
{
    uint32_t before = 0, after = 0;
    (void)reg_read32(LR2021_REG_PACKET_STATUS, &before);
    int rc = reg_write_mask32(LR2021_REG_PACKET_STATUS, 0x0000000FUL, PROBE_CR_CONFIGURED);
    (void)reg_read32(LR2021_REG_PACKET_STATUS, &after);
    probe_logf("# clear rc=%d 0x%08X -> 0x%08X; watch the next reception", rc, (unsigned)before, (unsigned)after);

    uint8_t to[3] = {
        (uint8_t)(LR2021_RX_TIMEOUT_INF >> 16),
        (uint8_t)(LR2021_RX_TIMEOUT_INF >> 8),
        (uint8_t)LR2021_RX_TIMEOUT_INF,
    };
    (void)cmd_write(LR2021_CMD_SET_RX, to, sizeof(to));
    return rc;
}

/* ---- TX ---------------------------------------------------------------------------------- */

struct probe_phase {
    const char *name;
    uint8_t cr;
    uint8_t frames;
    const char *note;
};

/* P3 is the experiment: the off-rate traffic has stopped and every frame is at the configured
 * rate again. A chip that reports the received frame's rate reads 0x04 here; one that accumulates
 * reads 0x05 and keeps reading it. P4 and P5 show a second step and that it holds. */
static const struct probe_phase PHASES[] = {
    {"P1", 0x04, 10, "baseline at the configured rate"},
    {"P2", 0x01, 1, "one off-rate frame"},
    {"P3", 0x04, 10, "back to configured: correct 0x04, accumulating 0x05"},
    {"P4", 0x02, 1, "a second off-rate frame"},
    {"P5", 0x04, 5, "back again: correct 0x04, accumulating 0x07"},
};

static int transmit_frame(const char *text)
{
    size_t len = strlen(text);
    if (len > LR2021_MAX_PACKET_LENGTH) {
        return PROBE_ERR_ARG;
    }

    /* the transmit FIFO appends, so anything left in it goes first */
    int rc = cmd_write(LR2021_CMD_CLEAR_TX_FIFO, NULL, 0);
    if (rc == 0) {
        rc = set_packet_params(PROBE_PREAMBLE, (uint8_t)len);
    }
    if (rc == 0) {
        rc = cmd_write(LR2021_CMD_WRITE_TX_FIFO, (const uint8_t *)text, len);
    }
    if (rc == 0) {
        rc = cmd_u32(LR2021_CMD_CLEAR_IRQ, LR2021_IRQ_ALL);
    }
    if (rc != 0) {
        return rc;
    }

    uint8_t to[3] = {0, 0, 0}; /* no hardware timeout; the wait below bounds it */
    rc = cmd_write(LR2021_CMD_SET_TX, to, sizeof(to));
    if (rc != 0) {
        return rc;
    }

    uint32_t start = hal->millis();
    for (;;) {
        uint32_t irq = 0;
        rc = irq_read(&irq);
        if (rc != 0) {
            return rc;
        }
        if (irq & LR2021_IRQ_TX_DONE) {
            return 0;
        }
        if (irq & LR2021_IRQ_TIMEOUT) {
            probe_logf("! TX timeout irq=%08X", (unsigned)irq);
            return PROBE_ERR_SPI;
        }
        if (hal->millis() - start > 5000) {
            probe_logf("! no TX_DONE after 5 s, irq=%08X", (unsigned)irq);
            return PROBE_ERR_SPI;
        }
    }
}

int lr2021_probe_tx_sequence(void)
{
    probe_logf("# --- sequence start ---");
    for (size_t p = 0; p < sizeof(PHASES) / sizeof(PHASES[0]); p++) {
        const struct probe_phase *ph = &PHASES[p];
        int rc = set_modulation_params(PROBE_SF, PROBE_BW, ph->cr);
        probe_logf("# %s cr=0x%02X x%u rc=%d (%s)", ph->name, ph->cr, (unsigned)ph->frames, rc, ph->note);
        if (rc != 0) {
            return rc;
        }
        for (uint8_t f = 1; f <= ph->frames; f++) {
            char text[24];
            snprintf(text, sizeof(text), "%s F%02u CR%02X", ph->name, (unsigned)f, ph->cr);
            rc = transmit_frame(text);
            probe_logf("TX %-13s rc=%d", text, rc);
            if (rc != 0) {
                return rc;
            }
            hal->delay_ms(400);
        }
        hal->delay_ms(2000);
    }
    probe_logf("# --- sequence done ---");
    return 0;
}
