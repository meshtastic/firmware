#include "i2cButton.h"
#include "meshUtils.h"

#include "configuration.h"
#if defined(M5STACK_UNITC6L) || defined(ARDUINO_NESSO_N1)

#include "MeshService.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "input/InputBroker.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#include "sleep.h"
#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

i2cButtonThread *i2cButton;

using namespace concurrency;

extern void i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value);

extern void i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value);
#if defined(ARDUINO_NESSO_N1)
extern uint8_t gpio_ext_get(uint8_t address, uint8_t pin);
#endif

#define PI4IO_M_ADDR 0x43
#define getbit(x, y) ((x) >> (y)&0x01)
#if defined(M5STACK_UNITC6L)
#define PI4IO_REG_IRQ_STA 0x13
#define PI4IO_REG_IN_STA 0x0F
#define PI4IO_REG_CHIP_RESET 0x01
#endif

#if defined(ARDUINO_NESSO_N1)
static bool readPi4ioInputState(uint8_t *inputState)
{
    static uint32_t lastReadErrorLogMs = 0;
    uint8_t state = 0;

    for (uint8_t pin = 0; pin < 8; ++pin) {
        uint8_t bit = gpio_ext_get(PI4IO_M_ADDR, pin);
        if (bit > 1) {
            uint32_t now = millis();
            if (now - lastReadErrorLogMs > 5000) {
                lastReadErrorLogMs = now;
                LOG_WARN("PI4IO button expander read failed at 0x%02x", PI4IO_M_ADDR);
            }
            return false;
        }
        state |= (bit << pin);
    }

    *inputState = state;
    return true;
}
#endif

i2cButtonThread::i2cButtonThread(const char *name) : OSThread(name)
{
    _originName = name;
    if (inputBroker)
        inputBroker->registerSource(this);
}

int32_t i2cButtonThread::runOnce()
{
#if defined(ARDUINO_NESSO_N1)
    static bool btn1_pressed = false;
    static bool btn2_pressed = false;
    static uint32_t press_start_time = 0;
    static uint32_t press2_start_time = 0;
    const uint32_t LONG_PRESS_TIME = 1000;
    static bool long_press_triggered = false;
    static bool long_press2_triggered = false;
    static bool initialized = false;

    uint8_t input_state;
    if (!readPi4ioInputState(&input_state)) {
        return 50;
    }

    if (!initialized) {
        initialized = true;
        btn1_pressed = !getbit(input_state, 0);
        btn2_pressed = !getbit(input_state, 1);
        return 50;
    }

    if (!getbit(input_state, 0)) {
        if (!btn1_pressed) {
            btn1_pressed = true;
            press_start_time = millis();
            long_press_triggered = false;
        }
    } else if (btn1_pressed) {
        btn1_pressed = false;
        uint32_t press_duration = millis() - press_start_time;
        if (long_press_triggered) {
            long_press_triggered = false;
        } else if (press_duration < LONG_PRESS_TIME) {
            InputEvent evt;
            evt.source = "UserButton";
            evt.inputEvent = INPUT_BROKER_USER_PRESS;
            evt.kbchar = 0;
            evt.touchX = 0;
            evt.touchY = 0;
            this->notifyObservers(&evt);
        }
    }

    if (!getbit(input_state, 1)) {
        if (!btn2_pressed) {
            btn2_pressed = true;
            press2_start_time = millis();
            long_press2_triggered = false;
        }
    } else if (btn2_pressed) {
        btn2_pressed = false;
        uint32_t press_duration = millis() - press2_start_time;
        if (long_press2_triggered) {
            long_press2_triggered = false;
        } else if (press_duration < LONG_PRESS_TIME) {
            InputEvent evt;
            evt.source = "BackButton";
            evt.inputEvent = INPUT_BROKER_ALT_PRESS;
            evt.kbchar = 0;
            evt.touchX = 0;
            evt.touchY = 0;
            this->notifyObservers(&evt);
        }
    }

    if (btn1_pressed && !long_press_triggered && (millis() - press_start_time >= LONG_PRESS_TIME)) {
        long_press_triggered = true;
        InputEvent evt;
        evt.source = "UserButton";
        evt.inputEvent = INPUT_BROKER_SELECT;
        evt.kbchar = 0;
        evt.touchX = 0;
        evt.touchY = 0;
        this->notifyObservers(&evt);
    }
    if (btn2_pressed && !long_press2_triggered && (millis() - press2_start_time >= LONG_PRESS_TIME)) {
        long_press2_triggered = true;
        InputEvent evt;
        evt.source = "BackButton";
        evt.inputEvent = INPUT_BROKER_ALT_LONG;
        evt.kbchar = 0;
        evt.touchX = 0;
        evt.touchY = 0;
        this->notifyObservers(&evt);
    }
#else
    static bool btn1_pressed = false;
    static uint32_t press_start_time = 0;
    const uint32_t LONG_PRESS_TIME = 1000;
    static bool long_press_triggered = false;

    uint8_t in_data;
    i2c_read_byte(PI4IO_M_ADDR, PI4IO_REG_IRQ_STA, &in_data);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_IRQ_STA, in_data);
    if (getbit(in_data, 0)) {
        uint8_t input_state;
        i2c_read_byte(PI4IO_M_ADDR, PI4IO_REG_IN_STA, &input_state);

        if (!getbit(input_state, 0)) {
            if (!btn1_pressed) {
                btn1_pressed = true;
                press_start_time = millis();
                long_press_triggered = false;
            }
        } else {
            if (btn1_pressed) {
                btn1_pressed = false;
                uint32_t press_duration = millis() - press_start_time;
                if (long_press_triggered) {
                    long_press_triggered = false;
                    return 50;
                }

                if (press_duration < LONG_PRESS_TIME) {
                    InputEvent evt;
                    evt.source = "UserButton";
                    evt.inputEvent = INPUT_BROKER_USER_PRESS;
                    evt.kbchar = 0;
                    evt.touchX = 0;
                    evt.touchY = 0;
                    this->notifyObservers(&evt);
                }
            }
        }
    }

    if (btn1_pressed && !long_press_triggered && (millis() - press_start_time >= LONG_PRESS_TIME)) {
        long_press_triggered = true;
        InputEvent evt;
        evt.source = "UserButton";
        evt.inputEvent = INPUT_BROKER_SELECT;
        evt.kbchar = 0;
        evt.touchX = 0;
        evt.touchY = 0;
        this->notifyObservers(&evt);
    }
#endif
    return 50;
}
#endif
