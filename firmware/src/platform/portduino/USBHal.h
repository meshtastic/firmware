#ifndef PI_HAL_LGPIO_H
#define PI_HAL_LGPIO_H

// include RadioLib
#include "platform/portduino/PortduinoGlue.h"
#include <RadioLib.h>
#include <csignal>
#include <iostream>
#include <libpinedio-usb.h>
#include <unistd.h>

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
            throw(s + std::to_string(ret));
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
        strncpy(_product_string, pinedio.product_string, len);
    }

    void init() override {}
    void term() override {}

    // GPIO-related methods (pinMode, digitalWrite etc.) should check
    // RADIOLIB_NC as an alias for non-connected pins
    void pinMode(uint32_t pin, uint32_t mode) override
    {
        if (pin == RADIOLIB_NC) {
            return;
        }
        pinedio_set_pin_mode(&pinedio, pin, mode);
    }

    void digitalWrite(uint32_t pin, uint32_t value) override
    {
        if (pin == RADIOLIB_NC) {
            return;
        }
        pinedio_digital_write(&pinedio, pin, value);
    }

    uint32_t digitalRead(uint32_t pin) override
    {
        if (pin == RADIOLIB_NC) {
            return 0;
        }
        return pinedio_digital_read(&pinedio, pin);
    }

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override
    {
        if (interruptNum == RADIOLIB_NC) {
            return;
        }
        // LOG_DEBUG("Attach interrupt to pin %d", interruptNum);
        pinedio_attach_interrupt(&this->pinedio, (pinedio_int_pin)interruptNum, (pinedio_int_mode)mode, interruptCb);
    }

    void detachInterrupt(uint32_t interruptNum) override
    {
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
        int32_t ret = pinedio_transceive(&this->pinedio, out, in, len);
        if (ret < 0) {
            std::cerr << "Could not perform SPI transfer: " << ret << std::endl;
        }
    }

    void spiEndTransaction() {}
    void spiEnd() {}

  private:
    pinedio_inst pinedio = {0};
};

#endif
