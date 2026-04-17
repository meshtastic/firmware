/**
 * nrf54l15_arduino.cpp — Arduino shim implementations for Zephyr/nRF54L15
 *
 * Provides concrete implementations for Print, HardwareSerial, GPIO, SPI,
 * and String methods declared in Arduino.h / SPI.h.
 *
 * Phase 3: real GPIO via Zephyr GPIO API and real SPI via Zephyr SPI API.
 * Pin numbering convention: P0.n = n, P1.n = 16+n, P2.n = 32+n.
 */

#include "Arduino.h"
#include "SPI.h"
#include "Wire.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
// ── Bluefruit singleton stub (satisfies NodeDB.cpp ARCH_NRF52 path) ──────────
#include "bluefruit.h"
BlueFruitClass Bluefruit;

// ── _fini stub — ARM newlib's __libc_fini_array references _fini, but ────────
// Zephyr startup doesn't provide it. Provide a weak no-op so the linker
// is satisfied when C++ global dtors or atexit() pull in __libc_fini_array.
extern "C" void __attribute__((weak)) _fini(void) {}

// ── SPI / Wire singletons ─────────────────────────────────────────────────────
SPIClass SPI;
SPIClass SPI1;
TwoWire Wire;
TwoWire Wire1;

// ── HardwareSerial singletons ────────────────────────────────────────────────
HardwareSerial Serial;
HardwareSerial Serial1;
HardwareSerial Serial2;

// ── Timing functions — C linkage to match extern "C" declarations ────────────
extern "C" uint32_t millis(void)
{
    return (uint32_t)k_uptime_get_32();
}
extern "C" uint32_t micros(void)
{
    return (uint32_t)(k_uptime_get() * 1000ULL);
}
extern "C" void delay(uint32_t ms)
{
    k_sleep(K_MSEC(ms));
}
extern "C" void delayMicroseconds(uint32_t us)
{
    k_sleep(K_USEC(us));
}
extern "C" void yield(void)
{
    k_yield();
}

// ── NVIC_SystemReset — wraps __NVIC_SystemReset from CMSIS core_cm33.h ───────
// core_cm33.h has #define NVIC_SystemReset __NVIC_SystemReset, so undef it
// before defining our own implementation to prevent macro expansion collision.
#pragma push_macro("NVIC_SystemReset")
#undef NVIC_SystemReset
extern "C" void NVIC_SystemReset(void)
{
    sys_reboot(SYS_REBOOT_COLD);
}
#pragma pop_macro("NVIC_SystemReset")

// ── HardwareSerial::write ─────────────────────────────────────────────────────
size_t HardwareSerial::write(uint8_t c)
{
    // TODO(nrf54l15 Phase 3): route through Zephyr UART / USB-CDC console
    // For now use printk so we at least get something over RTT/UART0
    printk("%c", (char)c);
    return 1;
}

size_t HardwareSerial::write(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        printk("%c", (char)buf[i]);
    return n;
}

// ── Print::printf ─────────────────────────────────────────────────────────────
int Print::printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0)
        write((const uint8_t *)buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
    return n;
}

// ── strlcpy — BSD extension not in Zephyr newlib ────────────────────────────
extern "C" size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t copy = len < size - 1 ? len : size - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}

// ── Print numeric helpers ─────────────────────────────────────────────────────
static size_t printNumber(Print &p, unsigned long n, uint8_t base)
{
    if (base == 0)
        return p.write((uint8_t)n);

    char buf[8 * sizeof(long) + 1];
    char *end = buf + sizeof(buf) - 1;
    *end = '\0';
    if (n == 0) {
        *--end = '0';
    } else {
        while (n > 0) {
            unsigned long remainder = n % base;
            *--end = (char)(remainder < 10 ? '0' + remainder : 'A' + remainder - 10);
            n /= base;
        }
    }
    return p.write((const uint8_t *)end, strlen(end));
}

static size_t printFloat(Print &p, double number, uint8_t digits)
{
    if (isnan(number))
        return p.print("nan");
    if (isinf(number))
        return p.print("inf");
    if (number > 4294967040.0 || number < -4294967040.0)
        return p.print("ovf");

    size_t n = 0;
    if (number < 0.0) {
        n += p.write('-');
        number = -number;
    }

    // Round
    double rounding = 0.5;
    for (uint8_t i = 0; i < digits; i++)
        rounding /= 10.0;
    number += rounding;

    unsigned long int_part = (unsigned long)number;
    double remainder = number - (double)int_part;
    n += printNumber(p, int_part, 10);
    if (digits > 0) {
        n += p.write('.');
        for (uint8_t i = 0; i < digits; i++) {
            remainder *= 10.0;
            unsigned int d = (unsigned int)remainder;
            n += p.write('0' + d);
            remainder -= d;
        }
    }
    return n;
}

size_t Print::print(unsigned char n, int base)
{
    return printNumber(*this, n, base);
}
size_t Print::print(int n, int base)
{
    if (base == 10 && n < 0) {
        size_t r = write('-');
        return r + printNumber(*this, (unsigned long)(-n), base);
    }
    return printNumber(*this, (unsigned long)n, base);
}
size_t Print::print(long n, int base)
{
    if (base == 10 && n < 0) {
        size_t r = write('-');
        return r + printNumber(*this, (unsigned long)(-n), base);
    }
    return printNumber(*this, (unsigned long)n, base);
}
size_t Print::print(unsigned int n, int base)
{
    return printNumber(*this, n, base);
}
size_t Print::print(unsigned long n, int base)
{
    return printNumber(*this, n, base);
}
size_t Print::print(float n, int d)
{
    return printFloat(*this, n, d);
}
size_t Print::print(double n, int d)
{
    return printFloat(*this, n, d);
}

// ── String::replace(String, String) ─────────────────────────────────────────
void String::replace(const String &from, const String &to)
{
    if (from.isEmpty() || !_buf)
        return;
    // Simple O(n²) replace — fine for typical Meshtastic string lengths
    String result;
    const char *p = _buf;
    while (*p) {
        if (strncmp(p, from.c_str(), from.length()) == 0) {
            result += to;
            p += from.length();
        } else {
            result += *p++;
        }
    }
    *this = result;
}

// ═════════════════════════════════════════════════════════════════════════════
// GPIO — Real Zephyr implementation (Phase 3)
// Pin mapping: P0.n = n (0-15), P1.n = 16+n (16-31), P2.n = 32+n (32-47)
// ═════════════════════════════════════════════════════════════════════════════

static const struct device *_gpio_dev_for_pin(uint32_t pin, gpio_pin_t *zpin)
{
    if (pin < 16) {
        *zpin = (gpio_pin_t)pin;
        return DEVICE_DT_GET(DT_NODELABEL(gpio0));
    } else if (pin < 32) {
        *zpin = (gpio_pin_t)(pin - 16);
        return DEVICE_DT_GET(DT_NODELABEL(gpio1));
    } else {
        *zpin = (gpio_pin_t)(pin - 32);
        return DEVICE_DT_GET(DT_NODELABEL(gpio2));
    }
}

void pinMode(uint32_t pin, uint32_t mode)
{
    gpio_pin_t zpin;
    const struct device *dev = _gpio_dev_for_pin(pin, &zpin);
    if (!device_is_ready(dev))
        return;

    gpio_flags_t flags;
    switch (mode) {
    case OUTPUT:
        flags = GPIO_OUTPUT_INACTIVE;
        break;
    case INPUT_PULLUP:
        flags = GPIO_INPUT | GPIO_PULL_UP;
        break;
    case INPUT_PULLDOWN:
        flags = GPIO_INPUT | GPIO_PULL_DOWN;
        break;
    default:
        flags = GPIO_INPUT;
        break;
    }
    gpio_pin_configure(dev, zpin, flags);
}

// Log first N CS (pin 37=P2.05) and RST (pin 32=P2.00) toggles to verify GPIO works.
// Silenced after GPIO_LOG_MAX calls to keep the log readable.
#define GPIO_LOG_MAX 20
static uint32_t _gpio_log_count = 0;

void digitalWrite(uint32_t pin, uint32_t value)
{
    // Before the very first NRESET pulse, snapshot BUSY state.
    // If BUSY is already HIGH here, the chip never completed power-on calibration.
    if (pin == 32 && value == 0) {
        static bool _first_nreset = true;
        if (_first_nreset) {
            _first_nreset = false;
            const struct device *bdev = DEVICE_DT_GET(DT_NODELABEL(gpio2));
            if (device_is_ready(bdev)) {
                gpio_pin_configure(bdev, 3, GPIO_INPUT); // P2.03 = BUSY
                int busy_before = gpio_pin_get(bdev, 3);
                printk("[nrf54l15] BUSY before first NRESET = %d%s\n", busy_before,
                       busy_before ? " ← STUCK HIGH (chip damaged?)" : " ← LOW (chip OK)");
            }
        }
    }

    gpio_pin_t zpin;
    const struct device *dev = _gpio_dev_for_pin(pin, &zpin);
    if (!device_is_ready(dev)) {
        printk("[GPIO] pin%u dev NOT READY\n", (unsigned)pin);
        return;
    }
    gpio_pin_set(dev, zpin, (int)value);
    if ((pin == 37 || pin == 32) && _gpio_log_count < GPIO_LOG_MAX) {
        // Read back the pin state to confirm it actually changed
        int actual = gpio_pin_get(dev, zpin);
        printk("[GPIO] pin%u → %u (read-back=%d)\n", (unsigned)pin, (unsigned)value, actual);
        _gpio_log_count++;
    }
}

int digitalRead(uint32_t pin)
{
    gpio_pin_t zpin;
    const struct device *dev = _gpio_dev_for_pin(pin, &zpin);
    if (!device_is_ready(dev))
        return 0;
    int v = gpio_pin_get(dev, zpin);
    // Log BUSY pin (35=P2.03) state changes + periodic updates for 10 seconds
    if (pin == 35) {
        static uint32_t busy_log_count = 0;
        static int last_busy = -1;
        static uint32_t first_read_ms = 0;
        if (first_read_ms == 0)
            first_read_ms = k_uptime_get_32();
        uint32_t elapsed_ms = k_uptime_get_32() - first_read_ms;

        // Always log state changes
        if (v != last_busy) {
            printk("[BUSY] %ums: state changed %d → %d\n", (unsigned)elapsed_ms, last_busy, v);
            last_busy = v;
        }
        // Also log every 500ms for first 10 seconds so we can see timeline
        if (elapsed_ms < 10000 && busy_log_count < 20 && (elapsed_ms / 500) > (busy_log_count)) {
            printk("[BUSY] %ums: pin=%d (periodic)\n", (unsigned)elapsed_ms, v);
            busy_log_count = (elapsed_ms / 500) + 1;
        }
    }
    return v;
}

// ─── attachInterrupt — supports up to NRF54L15_MAX_IRQS pins ────────────────
#define NRF54L15_MAX_IRQS 8

struct _PinIrq {
    struct gpio_callback cb;
    voidFuncPtr user_cb;
    const struct device *dev;
    gpio_pin_t zpin;
    bool used;
};

static _PinIrq _irq_table[NRF54L15_MAX_IRQS];

static void _gpio_irq_dispatch(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    _PinIrq *irq = CONTAINER_OF(cb, _PinIrq, cb);
    if (irq->user_cb)
        irq->user_cb();
}

void attachInterrupt(uint32_t pin, voidFuncPtr cb, int mode)
{
    gpio_pin_t zpin;
    const struct device *dev = _gpio_dev_for_pin(pin, &zpin);
    if (!device_is_ready(dev))
        return;

    // Find a free slot (or reuse existing registration for same pin)
    _PinIrq *slot = nullptr;
    for (int i = 0; i < NRF54L15_MAX_IRQS; i++) {
        if (_irq_table[i].used && _irq_table[i].dev == dev && _irq_table[i].zpin == zpin) {
            // Re-register: remove old callback first
            gpio_remove_callback(dev, &_irq_table[i].cb);
            slot = &_irq_table[i];
            break;
        }
        if (!slot && !_irq_table[i].used)
            slot = &_irq_table[i];
    }
    if (!slot)
        return; // table full

    gpio_flags_t irq_flags;
    switch (mode) {
    case RISING:
        irq_flags = GPIO_INT_EDGE_RISING;
        break;
    case FALLING:
        irq_flags = GPIO_INT_EDGE_FALLING;
        break;
    default:
        irq_flags = GPIO_INT_EDGE_BOTH;
        break;
    }

    slot->user_cb = cb;
    slot->dev = dev;
    slot->zpin = zpin;
    slot->used = true;

    gpio_pin_configure(dev, zpin, GPIO_INPUT);
    gpio_init_callback(&slot->cb, _gpio_irq_dispatch, BIT(zpin));
    gpio_add_callback(dev, &slot->cb);
    gpio_pin_interrupt_configure(dev, zpin, irq_flags);
}

void detachInterrupt(uint32_t pin)
{
    gpio_pin_t zpin;
    const struct device *dev = _gpio_dev_for_pin(pin, &zpin);
    for (int i = 0; i < NRF54L15_MAX_IRQS; i++) {
        if (_irq_table[i].used && _irq_table[i].dev == dev && _irq_table[i].zpin == zpin) {
            gpio_pin_interrupt_configure(dev, zpin, GPIO_INT_DISABLE);
            gpio_remove_callback(dev, &_irq_table[i].cb);
            _irq_table[i].used = false;
            break;
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SPI — Real Zephyr implementation using SPIM00 (HP domain, 3.0V)
// CS is handled by RadioLib via digitalWrite() — hardware CS not used.
// Mode 0 (CPOL=0, CPHA=0), MSB first.
// ═════════════════════════════════════════════════════════════════════════════

// Use SPIM00 (HP domain, 3.0V) — SPIM20 is 1.8V LP domain, incompatible with SX1262.
// Lazy-init: DEVICE_DT_GET in global scope fails when the extern symbol is
// not visible in this translation unit.  Use a function-local static instead.
static const struct device *_spi00(void)
{
    static const struct device *dev = nullptr;
    if (!dev) {
        dev = DEVICE_DT_GET(DT_NODELABEL(spi00));
        if (!device_is_ready(dev)) {
            printk("[nrf54l15] spi00 NOT READY\n");
            dev = nullptr;
        } else {
            printk("[nrf54l15] spi00 ready\n");
        }
    }
    return dev;
}

// SPI config: Mode 0, MSB first, no hardware CS (RadioLib does it manually)
//
// SPIM00 base clock = 128 MHz (nRF54L15 default when NRF_CONFIG_CPU_FREQ_MHZ
// is not set; SystemInit() applies 128 MHz).  Hardware prescaler must be EVEN
// and in [4, 126] (SPIM00_PRESCALER_DIVISOR_RANGE_MIN/MAX from MDK).
// 1 MHz → prescaler = 128 > 126 → NRFX_ERROR_INVALID_PARAM → -EIO on every
// transfer.  Minimum valid frequency is 2 MHz (prescaler = 64).
static const struct spi_config _spi00_cfg = {
    .frequency = 2000000U, // 2 MHz — minimum valid for SPIM00 at 128 MHz base
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = {}, // CS = NULL → RadioLib handles CS via GPIO
};

// Static DMA buffers — stack-allocated bufs on nRF54L15 may not be reachable
// by SPIM20 EasyDMA.  Static placement in .bss/.data is always in Global SRAM.
// rx_byte is pre-filled with 0xAA before every transfer so we can distinguish:
//   0xAA → DMA never wrote (EasyDMA can't reach the buffer)
//   0x00 → MISO actively driven LOW (chip in reset / bus fight)
//   0xFF → MISO floating HIGH
//   other → real chip response
static uint8_t _spi_tx_byte __attribute__((aligned(4)));
static uint8_t _spi_rx_byte __attribute__((aligned(4)));

// Dump the first SPI_DUMP_N byte exchanges so we can see what MISO returns.
#define SPI_DUMP_N 30
static uint32_t _spi_dump_count = 0;

uint8_t SPIClass::transfer(uint8_t data)
{
    const struct device *dev = _spi00();
    if (!dev)
        return 0xFF;

    _spi_tx_byte = data;
    _spi_rx_byte = 0xAA; // sentinel: if DMA doesn't write, we return 0xAA

    struct spi_buf tx_buf = {.buf = &_spi_tx_byte, .len = 1};
    struct spi_buf rx_buf = {.buf = &_spi_rx_byte, .len = 1};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    static uint32_t spi_err_count = 0;
    int ret = spi_transceive(dev, &_spi00_cfg, &tx_set, &rx_set);
    if (ret != 0 && spi_err_count++ < 3)
        printk("[SPI] err=%d tx=0x%02x\n", ret, data);

    if (_spi_dump_count < SPI_DUMP_N) {
        printk("[SPI] #%u tx=0x%02x rx=0x%02x\n", (unsigned)_spi_dump_count, data, _spi_rx_byte);
        _spi_dump_count++;
    }

    return _spi_rx_byte;
}

uint16_t SPIClass::transfer16(uint16_t data)
{
    const struct device *dev = _spi00();
    if (!dev)
        return 0xFFFF;

    uint8_t tx[2] = {(uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
    uint8_t rx[2] = {0, 0};
    struct spi_buf tx_buf = {.buf = tx, .len = 2};
    struct spi_buf rx_buf = {.buf = rx, .len = 2};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
    spi_transceive(dev, &_spi00_cfg, &tx_set, &rx_set);
    return ((uint16_t)rx[0] << 8) | rx[1];
}

void SPIClass::transferBytes(const uint8_t *tx, uint8_t *rx, uint32_t count)
{
    if (!count)
        return;
    const struct device *dev = _spi00();
    if (!dev)
        return;
    // Zephyr requires non-const buf pointer; cast is safe for tx-only direction
    struct spi_buf tx_buf = {.buf = const_cast<uint8_t *>(tx), .len = count};
    struct spi_buf rx_buf = {.buf = rx, .len = count};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = rx_buf.buf ? &rx_buf : nullptr, .count = rx_buf.buf ? 1U : 0U};
    spi_transceive(dev, &_spi00_cfg, &tx_set, rx ? &rx_set : nullptr);
}

void SPIClass::transfer(void *buf, size_t count)
{
    if (!count || !buf)
        return;
    transferBytes(reinterpret_cast<const uint8_t *>(buf), reinterpret_cast<uint8_t *>(buf), (uint32_t)count);
}
