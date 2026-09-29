#ifndef PI_HAL_LGPIO_H
#define PI_HAL_LGPIO_H

// include RadioLib
#include "platform/portduino/PortduinoGlue.h"
#include <RadioLib.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <libpinedio-usb.h>
#include <sys/time.h> // gettimeofday(), previously pulled in via libusb.h
#include <time.h>     // clock_gettime()
#include <unistd.h>

extern uint32_t rebootAtMsec;

// include the library for Raspberry GPIO pins

#define PI_RISING (PINEDIO_INT_MODE_RISING)
#define PI_FALLING (PINEDIO_INT_MODE_FALLING)
#define PI_INPUT (0)
#define PI_OUTPUT (1)
#define PI_LOW (0)
#define PI_HIGH (1)

#define CH341_PIN_CS (101)
#define CH341_PIN_IRQ (0)

// The adapter's chip select is D0, which is the pin pinedio_transceive_cs() drives.
#define CH341_PIN_CS_INDEX (0)

// the HAL must inherit from the base RadioLibHal class
// and implement all of its virtual methods
class Ch341Hal : public RadioLibHal
{
  public:
    // default constructor - initializes the base HAL and any needed private members
    explicit Ch341Hal(uint8_t spiChannel, const std::string &serial = "", uint32_t vid = 0x1A86, uint32_t pid = 0x5512,
                      uint32_t spiSpeed = 2000000, uint8_t spiDevice = 0, uint8_t gpioDevice = 0)
        : RadioLibHal(PI_INPUT, PI_OUTPUT, PI_LOW, PI_HIGH, PI_RISING, PI_FALLING)
    {
        if (serial != "") {
            std::strncpy(pinedio.serial_number, serial.c_str(), 8);
            pinedio_set_option(&pinedio, PINEDIO_OPTION_SEARCH_SERIAL, 1);
        }
        // LOG_INFO("USB Serial: %s", pinedio.serial_number);

        // There is no vendor with 0x0 -> so check
        if (vid != 0x0) {
            pinedio_set_option(&pinedio, PINEDIO_OPTION_VID, vid);
            pinedio_set_option(&pinedio, PINEDIO_OPTION_PID, pid);
        }
        int32_t ret = pinedio_init(&pinedio, NULL);
        if (ret != 0) {
            std::string s = "Could not open SPI: ";
            throw std::runtime_error(s + std::to_string(ret));
        }

        pinedio_set_option(&pinedio, PINEDIO_OPTION_AUTO_CS, 0);
        pinedio_set_pin_mode(&pinedio, 3, true);
        pinedio_set_pin_mode(&pinedio, 5, true);
#if defined(CH341_PACKED_CS) && defined(PINEDIO_HAS_TRANSCEIVE_CS)
        // Bench: pinedio_transceive_cs() drives D0 itself, so it is only equivalent where D0 is the CS line
        packedCs = portduino_config.lora_cs_pin.pin == CH341_PIN_CS_INDEX;
#ifdef PINEDIO_HAS_TRANSCEIVE_SELECT
        packedCsHigh = packedCs;
#endif
        LOG_INFO("CH341 packed CS %s (Lora.CS %d)",
                 !packedCs ? "off, CS is not D0" : (packedCsHigh ? "on, both levels" : "on, the low level only"),
                 portduino_config.lora_cs_pin.pin);
#endif
#ifdef CH341_FAST_HAL
        LOG_INFO("CH341 fast HAL on: a BUSY read straight after one that found it low is answered from it; short "
                 "delays spin");
#endif
    }

    ~Ch341Hal() { pinedio_deinit(&pinedio); }

    void getSerialString(char *_serial, size_t len)
    {
        if (len == 0)
            return;
        size_t bytesCopied = (len - 1) < 8 ? (len - 1) : 8;
        std::strncpy(_serial, pinedio.serial_number, bytesCopied);
        _serial[bytesCopied] = '\0';
    }

    void getProductString(char *_product_string, size_t len)
    {
        len = len > 95 ? 95 : len;
        memcpy(_product_string, pinedio.product_string, len);
    }

    void init() override {}
    void term() override {}

    // GPIO-related methods (pinMode, digitalWrite etc.) should check
    // RADIOLIB_NC as an alias for non-connected pins
    void pinMode(uint32_t pin, uint32_t mode) override
    {
        if (checkError()) {
            return;
        }
        if (pin == RADIOLIB_NC) {
            return;
        }
        auto res = pinedio_set_pin_mode(&pinedio, pin, mode);
        if (res < 0 && rebootAtMsec == 0) {
            LOG_ERROR("USBHal pinMode: Can't set pin %u mode to %u: %d", pin, mode, res);
        }
    }

    void digitalWrite(uint32_t pin, uint32_t value) override
    {
        if (checkError()) {
            return;
        }
        if (pin == RADIOLIB_NC) {
            return;
        }
#ifdef CH341_FAST_HAL
        busyLowPin = RADIOLIB_NC; // anything done to the chip ends what the last BUSY read can vouch for
#endif
        // Bench: with the packed-CS path the transfer carries the select, and with the select path the deselect too
        if (packedCs && pin == CH341_PIN_CS_INDEX && (value == PI_LOW || packedCsHigh)) {
            return;
        }
        auto res = pinedio_digital_write(&pinedio, pin, value);
        if (res < 0 && rebootAtMsec == 0) {
            LOG_ERROR("USBHal digitalWrite: Can't write pin %u: %d", pin, res);
            portduino_status.LoRa_in_error = true;
        }
    }

    uint32_t digitalRead(uint32_t pin) override
    {
        if (checkError()) {
            return 0;
        }
        if (pin == RADIOLIB_NC) {
            return 0;
        }
#ifdef CH341_FAST_HAL
        // Bench: RadioLib waits for BUSY low after each command and again before the next. With nothing sent to the
        // chip in between, the second read can only repeat the first, so it is answered from it, once.
        if (pin == busyLowPin && micros() - busyLowAtUs < BUSY_REUSE_US) {
            busyLowPin = RADIOLIB_NC;
            return 0;
        }
        busyLowPin = RADIOLIB_NC;
#endif
        auto res = pinedio_digital_read(&pinedio, pin);
        if (res < 0 && rebootAtMsec == 0) {
            LOG_ERROR("USBHal digitalRead: Can't read pin %u: %d", pin, res);
            portduino_status.LoRa_in_error = true;
            return 0;
        }
#ifdef CH341_FAST_HAL
        // The first pin read after a transfer is RadioLib's BUSY wait; it keeps reading that pin until it is low
        if (postCmdPin == RADIOLIB_NC && afterSpi)
            postCmdPin = pin;
        if (afterSpi && pin == postCmdPin && res == 0) {
            afterSpi = false;
            postCmdPin = RADIOLIB_NC;
            busyLowPin = pin;
            busyLowAtUs = micros();
        } else if (afterSpi && pin != postCmdPin) {
            afterSpi = false;
            postCmdPin = RADIOLIB_NC;
        }
#endif
        return res;
    }

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override
    {
        if (checkError()) {
            return;
        }
        if (interruptNum == RADIOLIB_NC) {
            return;
        }
        // LOG_DEBUG("Attach interrupt to pin %d", interruptNum);
        pinedio_attach_interrupt(&this->pinedio, (pinedio_int_pin)interruptNum, (pinedio_int_mode)mode, interruptCb);
    }

    void detachInterrupt(uint32_t interruptNum) override
    {
        if (checkError()) {
            return;
        }
        if (interruptNum == RADIOLIB_NC) {
            return;
        }
        // LOG_DEBUG("Detach interrupt from pin %d", interruptNum);
        pinedio_deattach_interrupt(&this->pinedio, (pinedio_int_pin)interruptNum);
    }

    void delay(unsigned long ms) override { delayMicroseconds(ms * 1000); }

    void delayMicroseconds(unsigned long us) override
    {
        if (us == 0) {
            sched_yield();
            return;
        }
#ifdef CH341_FAST_HAL
        // Bench: usleep() rounds a few microseconds up to the timer slack, ~50 us; RadioLib's 1 us settle waits
        if (us < SPIN_DELAY_MAX_US) {
            struct timespec start, now;
            clock_gettime(CLOCK_MONOTONIC, &start);
            do {
                clock_gettime(CLOCK_MONOTONIC, &now);
            } while ((unsigned long)((now.tv_sec - start.tv_sec) * 1000000L + (now.tv_nsec - start.tv_nsec) / 1000) < us);
            return;
        }
#endif
        usleep(us);
    }

    void yield() override { sched_yield(); }

    unsigned long millis() override
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000ULL);
    }

    unsigned long micros() override
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (tv.tv_sec * 1000000ULL) + tv.tv_usec;
    }

    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override
    {
        std::cerr << "pulseIn for pin " << pin << "is not supported!" << std::endl;
        return 0;
    }

    void spiBegin() {}
    void spiBeginTransaction() {}

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in)
    {
        if (checkError()) {
            return;
        }
#ifdef CH341_FAST_HAL
        busyLowPin = RADIOLIB_NC;
        afterSpi = true;
        postCmdPin = RADIOLIB_NC;
#endif
        int32_t ret = transceive(out, in, len);
        if (ret < 0) {
            std::cerr << "Could not perform SPI transfer: " << ret << std::endl;
        }
    }

    void spiEndTransaction() {}
    void spiEnd() {}
    bool checkError()
    {
        if (pinedio.in_error) {
            if (!has_warned)
                LOG_ERROR("USBHal: libch341 in_error detected");
            portduino_status.LoRa_in_error = true;
            has_warned = true;
            return true;
        }
        has_warned = false;
        return false;
    }

  private:
    /** One SPI transfer, carrying the CS levels too where the library and the wiring allow it (-DCH341_PACKED_CS).
     *  RadioLib does exactly one spiTransfer per CS window (Module::SPItransferStream), so the chip sees the same
     *  traffic: select, command and deselect cost one wait on the bus instead of three. */
    int32_t transceive(uint8_t *out, uint8_t *in, size_t len)
    {
#ifdef PINEDIO_HAS_TRANSCEIVE_SELECT
        if (packedCsHigh)
            return pinedio_transceive_select(&pinedio, out, in, len);
#endif
#ifdef PINEDIO_HAS_TRANSCEIVE_CS
        if (packedCs)
            return pinedio_transceive_cs(&pinedio, out, in, len);
#endif
        return pinedio_transceive(&pinedio, out, in, len);
    }

    pinedio_inst pinedio = {0};
    bool has_warned = false;
    // -DCH341_PACKED_CS, and only where D0 really is the configured CS pin: the transfer carries the CS low, and
    // with a library that has pinedio_transceive_select() the high as well
    bool packedCs = false;
    bool packedCsHigh = false;
#ifdef CH341_FAST_HAL
    static constexpr unsigned long BUSY_REUSE_US = 1000;
    static constexpr unsigned long SPIN_DELAY_MAX_US = 100;
    // A transfer went out and its BUSY wait has not yet seen the pin low
    bool afterSpi = false;
    uint32_t postCmdPin = RADIOLIB_NC;
    // The pin that wait last found low, and when; RADIOLIB_NC once anything else touches the chip
    uint32_t busyLowPin = RADIOLIB_NC;
    unsigned long busyLowAtUs = 0;
#endif
};

#endif
