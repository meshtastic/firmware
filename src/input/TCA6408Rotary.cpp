#include "TCA6408Rotary.h"

#if defined(HAS_TCA6408_ROTARY)

#include "Throttle.h"
#include "main.h"
#include <Wire.h>

namespace
{
constexpr uint8_t TCA6408_ADDR = 0x20;
constexpr uint8_t TCA6408_INPUT_REG = 0x00;
constexpr uint8_t TCA6408_POLARITY_REG = 0x02;
constexpr uint8_t TCA6408_CONFIG_REG = 0x03;
constexpr uint8_t TCA6408_ROTARY_A_MASK = 0x01;
constexpr uint8_t TCA6408_ROTARY_B_MASK = 0x02;
constexpr uint8_t TCA6408_ROTARY_MASK = TCA6408_ROTARY_A_MASK | TCA6408_ROTARY_B_MASK;
constexpr uint32_t TCA6408_DEBOUNCE_MS = 5;
constexpr uint32_t TCA6408_POLL_MS = 100;

enum class RotaryAction : uint8_t { NONE, UP, DOWN };
} // namespace

TCA6408Rotary *tca6408Rotary;
TCA6408Rotary *TCA6408Rotary::instance = nullptr;

TCA6408Rotary::TCA6408Rotary(const char *name)
    : concurrency::OSThread(name, TCA6408_POLL_MS), _originName(name), inputState(TCA6408_ROTARY_MASK)
{
}

bool TCA6408Rotary::init()
{
    if (!inputBroker)
        return false;

    powerSensorBus();
    pinMode(SENSOR_INT, INPUT_PULLUP);

    // No input inversion, all eight pins configured as inputs.
    if (!writeRegister(TCA6408_POLARITY_REG, 0x00) || !writeRegister(TCA6408_CONFIG_REG, 0xFF) || !readInput(inputState)) {
        LOG_INFO("TCA6408 rotary not detected");
        concurrency::OSThread::disable();
        return false;
    }

    inputBroker->registerSource(this);
    instance = this;
    attachInterrupt(digitalPinToInterrupt(SENSOR_INT), interruptHandler, FALLING);
    ready = true;
    LOG_INFO("TCA6408 rotary ready at 0x%02x", TCA6408_ADDR);
    return true;
}

int32_t TCA6408Rotary::runOnce()
{
    if (!ready)
        return concurrency::OSThread::disable();

    uint8_t newState = 0;
    if (!readInput(newState)) {
        LOG_DEBUG("TCA6408 rotary read failed");
        return TCA6408_POLL_MS;
    }

    handleTransition(newState);
    inputState = newState;
    return TCA6408_POLL_MS;
}

// Only wakes the thread. Reading the expander here would put an I2C transfer in interrupt
// context and race runOnce() for the bus and the decoder state.
void TCA6408Rotary::interruptHandler()
{
    if (!instance)
        return;

    instance->setIntervalFromNow(0);
    runASAP = true;
    BaseType_t higherWake = 0;
    concurrency::mainDelay.interruptFromISR(&higherWake);
}

void TCA6408Rotary::powerSensorBus()
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

bool TCA6408Rotary::writeRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(TCA6408_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool TCA6408Rotary::readInput(uint8_t &value)
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

// Whichever of A/B falls first decides the direction for the whole detent; activeLowPhase then
// suppresses further events until both inputs come back high. The rising-edge cases below catch
// detents whose falling edge was missed because the poll landed mid-rotation.
void TCA6408Rotary::handleTransition(uint8_t newState)
{
    const uint8_t changed = (inputState ^ newState) & TCA6408_ROTARY_MASK;
    const bool aLow = (newState & TCA6408_ROTARY_A_MASK) == 0;
    const bool bLow = (newState & TCA6408_ROTARY_B_MASK) == 0;
    RotaryAction action = RotaryAction::NONE;

    if (!aLow && !bLow)
        activeLowPhase = false; // back at the detent, arm for the next turn

    if (!activeLowPhase) {
        if ((changed & TCA6408_ROTARY_A_MASK) && aLow && !bLow) {
            action = RotaryAction::UP;
            activeLowPhase = true;
        } else if ((changed & TCA6408_ROTARY_B_MASK) && bLow && !aLow) {
            action = RotaryAction::DOWN;
            activeLowPhase = true;
        } else if ((changed & TCA6408_ROTARY_A_MASK) && !aLow && bLow) {
            action = RotaryAction::UP;
        } else if ((changed & TCA6408_ROTARY_B_MASK) && !bLow && aLow) {
            action = RotaryAction::DOWN;
        }
    }

    if (action == RotaryAction::NONE || Throttle::isWithinTimespanMs(lastEventMs, TCA6408_DEBOUNCE_MS))
        return;

    lastEventMs = millis();
    InputEvent event = {};
    event.source = _originName;
    event.inputEvent = action == RotaryAction::DOWN ? INPUT_BROKER_DOWN : INPUT_BROKER_UP;
    LOG_DEBUG("TCA6408 rotary event %d state=0x%02x", event.inputEvent, newState);
    notifyObservers(&event);
}

#endif
