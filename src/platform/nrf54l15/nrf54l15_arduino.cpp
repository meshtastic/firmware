/**
 * nrf54l15_arduino.cpp — Arduino shim implementations for Zephyr/nRF54L15
 *
 * Provides the concrete implementations for Print, HardwareSerial,
 * and String methods declared in Arduino.h.
 *
 * Phase 2: minimal stubs that compile cleanly.
 * Phase 3: replace Serial with real Zephyr UART/USB-CDC backend.
 */

#include "Arduino.h"
#include "SPI.h"
#include "Wire.h"
#include <zephyr/kernel.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// ── Bluefruit singleton stub (satisfies NodeDB.cpp ARCH_NRF52 path) ──────────
#include "bluefruit.h"
BlueFruitClass Bluefruit;

// ── Filesystem singleton (stub for Phase 2) ──────────────────────────────────
#include "InternalFileSystem.h"
Adafruit_LittleFS_Namespace::InternalFileSystem InternalFS;

// ── SAADC lock stub (Phase 2) ────────────────────────────────────────────────
#include "Nrf52SaadcLock.h"
namespace concurrency { Lock *nrf52SaadcLock = nullptr; }

// ── SPI / Wire singletons ─────────────────────────────────────────────────────
SPIClass SPI;
SPIClass SPI1;
TwoWire  Wire;
TwoWire  Wire1;

// ── HardwareSerial singletons ────────────────────────────────────────────────
HardwareSerial Serial;
HardwareSerial Serial1;
HardwareSerial Serial2;

// ── Timing functions — C linkage to match extern "C" declarations ────────────
extern "C" uint32_t millis(void)            { return (uint32_t)k_uptime_get_32(); }
extern "C" uint32_t micros(void)            { return (uint32_t)(k_uptime_get() * 1000ULL); }
extern "C" void     delay(uint32_t ms)      { k_sleep(K_MSEC(ms)); }
extern "C" void     delayMicroseconds(uint32_t us) { k_sleep(K_USEC(us)); }
extern "C" void     yield(void)             { k_yield(); }

// ── NVIC_SystemReset — wraps __NVIC_SystemReset from CMSIS core_cm33.h ───────
// core_cm33.h has #define NVIC_SystemReset __NVIC_SystemReset, so undef it
// before defining our own implementation to prevent macro expansion collision.
#pragma push_macro("NVIC_SystemReset")
#undef NVIC_SystemReset
extern "C" void NVIC_SystemReset(void)      { sys_reboot(SYS_REBOOT_COLD); }
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
    if (n > 0) write((const uint8_t*)buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
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
    return p.write((const uint8_t*)end, strlen(end));
}

static size_t printFloat(Print &p, double number, uint8_t digits)
{
    if (isnan(number))  return p.print("nan");
    if (isinf(number))  return p.print("inf");
    if (number >  4294967040.0 || number < -4294967040.0)
        return p.print("ovf");

    size_t n = 0;
    if (number < 0.0) { n += p.write('-'); number = -number; }

    // Round
    double rounding = 0.5;
    for (uint8_t i = 0; i < digits; i++) rounding /= 10.0;
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

size_t Print::print(unsigned char n, int base) { return printNumber(*this, n, base); }
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
size_t Print::print(unsigned int n, int base)  { return printNumber(*this, n, base); }
size_t Print::print(unsigned long n, int base) { return printNumber(*this, n, base); }
size_t Print::print(float n, int d)            { return printFloat(*this, n, d); }
size_t Print::print(double n, int d)           { return printFloat(*this, n, d); }

// ── String::replace(String, String) ─────────────────────────────────────────
void String::replace(const String &from, const String &to)
{
    if (from.isEmpty() || !_buf) return;
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
