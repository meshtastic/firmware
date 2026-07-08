#include "i2cButton.h"
#include "Throttle.h"
#include "meshUtils.h"
#include "configuration.h"
#include <Wire.h>

#if defined(M5STACK_UNITC6L) || defined(HELTEC_RC32)

#if defined(M5STACK_UNITC6L)
#include "MeshService.h"
#include "Power.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "input/InputBroker.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "sleep.h"
#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif
#endif

i2cButtonThread *i2cButton;

#if defined(M5STACK_UNITC6L)
using namespace concurrency;

extern void i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value);

extern void i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value);

#define PI4IO_M_ADDR 0x43
#define getbit(x, y) ((x) >> (y)&0x01)
#define PI4IO_REG_IRQ_STA 0x13
#define PI4IO_REG_IN_STA 0x0F
#define PI4IO_REG_CHIP_RESET 0x01
#endif

#if defined(HELTEC_RC32)
namespace
{
constexpr uint8_t TCA6408_ADDR = 0x20;
constexpr uint8_t TCA6408_INPUT_REG = 0x00;
constexpr uint8_t TCA6408_POLARITY_REG = 0x02;
constexpr uint8_t TCA6408_CONFIG_REG = 0x03;
constexpr uint8_t TCA6408_ROTARY_A_MASK = 0x01;
constexpr uint8_t TCA6408_ROTARY_B_MASK = 0x02;
constexpr uint32_t TCA6408_DEBOUNCE_MS = 5;
constexpr uint32_t TCA6408_POLL_MS = 100;

enum class Tca6408RotaryAction : uint8_t { NONE, UP, DOWN };
} // namespace

i2cButtonThread *i2cButtonThread::instance = nullptr;
#endif

i2cButtonThread::i2cButtonThread(const char *name)
#if defined(HELTEC_RC32)
    : concurrency::OSThread(name, TCA6408_POLL_MS), _originName(name),
      inputState(TCA6408_ROTARY_A_MASK | TCA6408_ROTARY_B_MASK)
#else
    : OSThread(name)
#endif
{
#if defined(M5STACK_UNITC6L)
    _originName = name;
    if (inputBroker)
        inputBroker->registerSource(this);
#endif
}

int32_t i2cButtonThread::runOnce()
{
#if defined(HELTEC_RC32)
    if (!ready)
        return concurrency::OSThread::disable();

    pollOnce();
    return TCA6408_POLL_MS;
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
    return 50;
#endif
}

#if defined(HELTEC_RC32)

bool i2cButtonThread::init()
{
    if (!inputBroker)
        return false;

    powerSensorBus();
    pinMode(SENSOR_INT_PIN, INPUT_PULLUP);
    if (!writeRegister(TCA6408_POLARITY_REG, 0x00) || !writeRegister(TCA6408_CONFIG_REG, 0xFF) || !readInput(inputState)) {
        LOG_INFO("Heltec RC32 TCA6408 rotary not detected");
        concurrency::OSThread::disable();
        return false;
    }

    inputBroker->registerSource(this);
    instance = this;
    attachInterrupt(SENSOR_INT_PIN, interruptHandler, FALLING);
    ready = true;
    LOG_INFO("Heltec RC32 TCA6408 rotary ready at 0x%02x", TCA6408_ADDR);
    return true;
}

void i2cButtonThread::pollOnce()
{
    uint8_t newState = 0;
    if (!readInput(newState)) {
        LOG_DEBUG("Heltec RC32 TCA6408 rotary read failed");
        return;
    }

    handleTransition(newState);
    inputState = newState;
}

void i2cButtonThread::interruptHandler()
{
    if (instance) {
        instance->setIntervalFromNow(0);
        runASAP = true;
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
    }
}

void i2cButtonThread::powerSensorBus()
{
#ifdef SENSOR_POWER_CTRL_PIN
    pinMode(SENSOR_POWER_CTRL_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_CTRL_PIN, SENSOR_POWER_ON);
#ifdef PERIPHERAL_WARMUP_MS
    delay(PERIPHERAL_WARMUP_MS);
#else
    delay(20);
#endif
#endif
}

bool i2cButtonThread::writeRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(TCA6408_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool i2cButtonThread::readInput(uint8_t &value)
{
    Wire.beginTransmission(TCA6408_ADDR);
    Wire.write(TCA6408_INPUT_REG);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom(TCA6408_ADDR, static_cast<uint8_t>(1)) != 1)
        return false;

    value = Wire.read();
    return true;
}

void i2cButtonThread::handleTransition(uint8_t newState)
{
    uint8_t changed = (inputState ^ newState) & (TCA6408_ROTARY_A_MASK | TCA6408_ROTARY_B_MASK);
    Tca6408RotaryAction action = Tca6408RotaryAction::NONE;
    bool aLow = (newState & TCA6408_ROTARY_A_MASK) == 0;
    bool bLow = (newState & TCA6408_ROTARY_B_MASK) == 0;

    if (!aLow && !bLow)
        activeLowPhase = false;

    if (!activeLowPhase && (changed & TCA6408_ROTARY_A_MASK) && aLow && !bLow) {
        action = Tca6408RotaryAction::UP;
        activeLowPhase = true;
    } else if (!activeLowPhase && (changed & TCA6408_ROTARY_B_MASK) && bLow && !aLow) {
        action = Tca6408RotaryAction::DOWN;
        activeLowPhase = true;
    }

    if (action == Tca6408RotaryAction::NONE && !activeLowPhase && (changed & TCA6408_ROTARY_A_MASK)) {
        bool aRising = (newState & TCA6408_ROTARY_A_MASK) != 0;
        if (aRising && bLow)
            action = Tca6408RotaryAction::UP;
    }

    if (action == Tca6408RotaryAction::NONE && !activeLowPhase && (changed & TCA6408_ROTARY_B_MASK)) {
        bool bRising = (newState & TCA6408_ROTARY_B_MASK) != 0;
        if (bRising && aLow)
            action = Tca6408RotaryAction::DOWN;
    }

    if (action == Tca6408RotaryAction::NONE || Throttle::isWithinTimespanMs(lastEventMs, TCA6408_DEBOUNCE_MS))
        return;

    lastEventMs = millis();
    InputEvent event = {};
    event.source = _originName;
    event.inputEvent = action == Tca6408RotaryAction::DOWN ? INPUT_BROKER_DOWN : INPUT_BROKER_UP;
    LOG_DEBUG("Heltec RC32 TCA6408 rotary event %d state=0x%02x", event.inputEvent, newState);
    notifyObservers(&event);
}
#endif

#endif
