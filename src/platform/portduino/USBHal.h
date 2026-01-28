#ifndef PI_HAL_LGPIO_H
#define PI_HAL_LGPIO_H

// include RadioLib
#include "platform/portduino/PortduinoGlue.h"
#include <RadioLib.h>
#include <csignal>
#include <iostream>
#include <libpinedio-usb.h>
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

// the HAL must inherit from the base RadioLibHal class
// and implement all of its virtual methods
class Ch341Hal : public RadioLibHal
{
  public:
    // default constructor - initializes the base HAL and any needed private members
    explicit Ch341Hal(uint8_t spiChannel, std::string serial = "", uint32_t vid = 0x1A86, uint32_t pid = 0x5512,
                      uint32_t spiSpeed = 2000000, uint8_t spiDevice = 0, uint8_t gpioDevice = 0)
        : RadioLibHal(PI_INPUT, PI_OUTPUT, PI_LOW, PI_HIGH, PI_RISING, PI_FALLING)
    {
        if (serial != "") {
            strncpy(pinedio.serial_number, serial.c_str(), 8);
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
    }

    ~Ch341Hal() { pinedio_deinit(&pinedio); }

    void getSerialString(char *_serial, size_t len)
    {
        len = len > 8 ? 8 : len;
        strncpy(_serial, pinedio.serial_number, len);
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
            LOG_ERROR("USBHal pinMode: Could not set pin %u mode to %u: %d", pin, mode, res);
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
        auto res = pinedio_digital_write(&pinedio, pin, value);
        if (res < 0 && rebootAtMsec == 0) {
            LOG_ERROR("USBHal digitalWrite: Could not write pin %u: %d", pin, res);
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
        auto res = pinedio_digital_read(&pinedio, pin);
        if (res < 0 && rebootAtMsec == 0) {
            LOG_ERROR("USBHal digitalRead: Could not read pin %u: %d", pin, res);
            portduino_status.LoRa_in_error = true;
            return 0;
        }
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
        int32_t ret = pinedio_transceive(&this->pinedio, out, in, len);
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
    pinedio_inst pinedio = {0};
    bool has_warned = false;
};

#endif
