#ifndef PI_HAL_LGPIO_H
#define PI_HAL_LGPIO_H

// include RadioLib
#include <RadioLib.h>
#include <csignal>
#include <libpinedio-usb.h>

// include the library for Raspberry GPIO pins

#define PI_RISING (PINEDIO_INT_MODE_RISING)
#define PI_FALLING (PINEDIO_INT_MODE_FALLING)
#define PI_INPUT (0)
#define PI_OUTPUT (1)
#define PI_LOW (0)
#define PI_HIGH (1)
#define PI_MAX_USER_GPIO (31)

#define CH341_PIN_CS (101)
#define CH341_PIN_IRQ (102)

// forward declaration of alert handler that will be used to emulate interrupts
// static void lgpioAlertHandler(int num_alerts, lgGpioAlert_p alerts, void *userdata);

// the HAL must inherit from the base RadioLibHal class
// and implement all of its virtual methods
class Ch341Hal : public RadioLibHal
{
  public:
    // default constructor - initializes the base HAL and any needed private members
    Ch341Hal(uint8_t spiChannel, uint32_t spiSpeed = 2000000, uint8_t spiDevice = 0, uint8_t gpioDevice = 0)
        : RadioLibHal(PI_INPUT, PI_OUTPUT, PI_LOW, PI_HIGH, PI_RISING, PI_FALLING)
    {
    }

    void init() override
    {
        // now the SPI
        spiBegin();
    }

    void term() override
    {
        // stop the SPI
        spiEnd();
    }

    // GPIO-related methods (pinMode, digitalWrite etc.) should check
    // RADIOLIB_NC as an alias for non-connected pins
    void pinMode(uint32_t pin, uint32_t mode) override
    {
        if (pin == RADIOLIB_NC) {
            return;
        }
        if (pin == CH341_PIN_CS || pin == CH341_PIN_IRQ) {
            return;
        }
        fprintf(stderr, "pinMode for pin %d and mode %d is not supported!\n", pin, mode);
    }

    void digitalWrite(uint32_t pin, uint32_t value) override
    {
        if (pin == RADIOLIB_NC) {
            return;
        }
        if (pin == CH341_PIN_CS) {
            pinedio_set_cs(&pinedio, value == 0);
            return;
        }
        fprintf(stderr, "digitalWrite for pin %d is not supported!\n", pin);
    }

    uint32_t digitalRead(uint32_t pin) override
    {
        if (pin == RADIOLIB_NC) {
            return 0;
        }
        if (pin == CH341_PIN_IRQ) {

            return pinedio_get_irq_state(&pinedio);
        }
        fprintf(stderr, "digitalRead for pin %d is not supported!\n", pin);
        return 0;
    }

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override
    {
        if ((interruptNum == RADIOLIB_NC) || (interruptNum > PI_MAX_USER_GPIO)) {
            return;
        }

        pinedio_attach_interrupt(&this->pinedio, (pinedio_int_pin)interruptNum, (pinedio_int_mode)mode, interruptCb);

        // set lgpio alert callback
        //      int result = lgGpioClaimAlert(_gpioHandle, 0, mode, interruptNum, -1);
        //      if(result < 0) {
        //        fprintf(stderr, "Could not claim pin %" PRIu32 " for alert: %s\n", interruptNum, lguErrorText(result));
        //        return;
        //      }

        //      // enable emulated interrupt
        //      interruptEnabled[interruptNum] = true;
        //      interruptModes[interruptNum] = mode;
        //      interruptCallbacks[interruptNum] = interruptCb;

        //      lgGpioSetAlertsFunc(_gpioHandle, interruptNum, lgpioAlertHandler, (void *)this);
    }

    void detachInterrupt(uint32_t interruptNum) override
    {
        if ((interruptNum == RADIOLIB_NC) || (interruptNum > PI_MAX_USER_GPIO)) {
            return;
        }

        pinedio_deattach_interrupt(&this->pinedio, (pinedio_int_pin)interruptNum);

        //      // clear emulated interrupt
        //      interruptEnabled[interruptNum] = false;
        //      interruptModes[interruptNum] = 0;
        //      interruptCallbacks[interruptNum] = NULL;

        // disable lgpio alert callback
        //      lgGpioFree(_gpioHandle, interruptNum);
        //      lgGpioSetAlertsFunc(_gpioHandle, interruptNum, NULL, NULL);
    }

    void delay(unsigned long ms) override
    {
        if (ms == 0) {
            sched_yield();
            return;
        }

        usleep(ms * 1000);
        //      lguSleep(ms / 1000.0);
    }

    void delayMicroseconds(unsigned long us) override
    {
        if (us == 0) {
            sched_yield();
            return;
        }
        usleep(us);

        //      lguSleep(us / 1000000.0);
    }

    void yield() override { sched_yield(); }

    unsigned long millis() override
    {
        //      uint32_t time = lguTimestamp() / 1000000UL;
        //      return time;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000ULL);
    }

    unsigned long micros() override
    {
        //      uint32_t time = lguTimestamp() / 1000UL;
        //      return time;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (tv.tv_sec * 1000000ULL) + tv.tv_usec;
    }

    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override
    {
        fprintf(stderr, "pulseIn for pin %d is not supported!\n", pin);
    }

    void spiBegin()
    {
        if (!pinedio_is_init) {
            int32_t ret = pinedio_init(&pinedio, NULL);
            if (ret != 0) {
                fprintf(stderr, "Could not open SPI: %d\n", ret);
            } else {
                pinedio_is_init = true;
                pinedio_set_option(&pinedio, PINEDIO_OPTION_AUTO_CS, 0);
            }
        }
    }

    void spiBeginTransaction() {}

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in)
    {
        int32_t result = pinedio_transceive(&this->pinedio, out, in, len);
        //      int result = lgSpiXfer(_spiHandle, (char *)out, (char*)in, len);
        if (result < 0) {
            fprintf(stderr, "Could not perform SPI transfer: %d\n", result);
        }
    }

    void spiEndTransaction() {}

    void spiEnd()
    {
        if (pinedio_is_init) {
            pinedio_deinit(&pinedio);
            pinedio_is_init = false;
        }
    }

#if 0
    void tone(uint32_t pin, unsigned int frequency, unsigned long duration = 0) {
      lgTxPwm(_gpioHandle, pin, frequency, 50, 0, duration);
    }

    void noTone(uint32_t pin) {
      lgTxPwm(_gpioHandle, pin, 0, 0, 0, 0);
    }
#endif

  private:
    // the HAL can contain any additional private members
    pinedio_inst pinedio;
    bool pinedio_is_init = false;
};

#if 0
// this handler emulates interrupts
static void lgpioAlertHandler(int num_alerts, lgGpioAlert_p alerts, void *userdata) {
  if(!userdata)
    return;

  // Ch341Hal instance is passed via the user data
  Ch341Hal* hal = (Ch341Hal*)userdata;

  // check the interrupt is enabled, the level matches and a callback exists
  for(lgGpioAlert_t *alert = alerts; alert < (alerts + num_alerts); alert++) {
    if((hal->interruptEnabled[alert->report.gpio]) &&
       (hal->interruptModes[alert->report.gpio] == alert->report.level) &&
       (hal->interruptCallbacks[alert->report.gpio])) {
      hal->interruptCallbacks[alert->report.gpio]();
    }
  }
}
#endif

#endif