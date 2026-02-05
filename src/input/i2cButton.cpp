#include "i2cButton.h"
#include "meshUtils.h"

#include "configuration.h"
#if defined(M5STACK_UNITC6L)

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

#define PI4IO_M_ADDR 0x43
#define getbit(x, y) ((x) >> (y)&0x01)
#define PI4IO_REG_IRQ_STA 0x13
#define PI4IO_REG_IN_STA 0x0F
#define PI4IO_REG_CHIP_RESET 0x01

i2cButtonThread::i2cButtonThread(const char *name) : OSThread(name)
{
    _originName = name;
    if (inputBroker)
        inputBroker->registerSource(this);
}

int32_t i2cButtonThread::runOnce()
{
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
    return 50;
}
#endif