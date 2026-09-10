#include "Nrf52Twim.h"

#if defined(ARCH_NRF52) && defined(HAS_QMA6100P)

#include <Arduino.h>
#include <Wire.h>

namespace
{
constexpr uint32_t i2cTimeoutUs = 5000;

void stopTwim(NRF_TWIM_Type *twim);

bool waitForEventOrError(volatile uint32_t &event, NRF_TWIM_Type *twim)
{
    const uint32_t start = micros();
    while (!event && !twim->EVENTS_ERROR) {
        if ((uint32_t)(micros() - start) >= i2cTimeoutUs) {
            stopTwim(twim);
            return false;
        }
    }
    return event && !twim->EVENTS_ERROR;
}

void configureTwim(NRF_TWIM_Type *twim)
{
    twim->ENABLE = TWIM_ENABLE_ENABLE_Disabled;
    twim->PSEL.SCL = PIN_WIRE_SCL;
    twim->PSEL.SDA = PIN_WIRE_SDA;
    twim->FREQUENCY = TWIM_FREQUENCY_FREQUENCY_K100;
    // ENABLE is a volatile HW register: the peripheral must be disabled (above) before
    // PSEL/FREQUENCY can be changed, then re-enabled here. Not a redundant store.
    twim->ENABLE = TWIM_ENABLE_ENABLE_Enabled; // cppcheck-suppress redundantAssignment
}

void clearTwimEvents(NRF_TWIM_Type *twim)
{
    twim->SHORTS = 0;
    twim->EVENTS_STOPPED = 0;
    twim->EVENTS_ERROR = 0;
    twim->EVENTS_SUSPENDED = 0;
    twim->EVENTS_RXSTARTED = 0;
    twim->EVENTS_TXSTARTED = 0;
    twim->EVENTS_LASTRX = 0;
    twim->EVENTS_LASTTX = 0;
    twim->ERRORSRC = TWIM_ERRORSRC_ANACK_Msk | TWIM_ERRORSRC_DNACK_Msk | TWIM_ERRORSRC_OVERRUN_Msk;
}

void stopTwim(NRF_TWIM_Type *twim)
{
    twim->TASKS_STOP = 1;
    const uint32_t start = micros();
    while (!twim->EVENTS_STOPPED && (uint32_t)(micros() - start) < i2cTimeoutUs)
        ;
    clearTwimEvents(twim);
}

bool waitForStop(NRF_TWIM_Type *twim)
{
    const uint32_t start = micros();
    while (!twim->EVENTS_STOPPED && !twim->EVENTS_ERROR) {
        if ((uint32_t)(micros() - start) >= i2cTimeoutUs) {
            stopTwim(twim);
            return false;
        }
    }
    const bool stopped = twim->EVENTS_STOPPED && !twim->EVENTS_ERROR;
    if (!stopped)
        stopTwim(twim);
    else
        clearTwimEvents(twim);
    return stopped;
}
} // namespace

namespace Nrf52Twim
{
void restoreBus()
{
#if defined(TRACKER_T1000_E)
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, HIGH);
    pinMode(PIN_3V3_ACC_EN, OUTPUT);
    digitalWrite(PIN_3V3_ACC_EN, HIGH);
#ifdef T1000X_SENSOR_EN_PIN
    pinMode(T1000X_SENSOR_EN_PIN, OUTPUT);
    digitalWrite(T1000X_SENSOR_EN_PIN, HIGH);
#endif
    delay(20);
#endif

    Wire.end();

    pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
    pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
    delayMicroseconds(10);

    for (uint8_t i = 0; i < 9 && digitalRead(PIN_WIRE_SDA) == LOW; i++) {
        pinMode(PIN_WIRE_SCL, OUTPUT);
        digitalWrite(PIN_WIRE_SCL, LOW);
        delayMicroseconds(5);
        pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
        delayMicroseconds(5);
    }

    pinMode(PIN_WIRE_SDA, OUTPUT);
    digitalWrite(PIN_WIRE_SDA, LOW);
    delayMicroseconds(5);
    pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
    delayMicroseconds(5);
    pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
    delayMicroseconds(5);

    Wire.begin();
}

bool readRegister(uint8_t address, uint8_t reg, uint8_t &value)
{
    NRF_TWIM_Type *twim = NRF_TWIM0;

    configureTwim(twim);
    clearTwimEvents(twim);
    twim->ADDRESS = address;
    twim->TXD.PTR = (uint32_t)&reg;
    twim->TXD.MAXCNT = 1;
    // LASTTX_SUSPEND shortcut: peripheral suspends automatically after the TX
    // byte, so EVENTS_SUSPENDED is set before we even poll - no clearing race.
    twim->SHORTS = TWIM_SHORTS_LASTTX_SUSPEND_Msk;
    twim->TASKS_RESUME = 1;
    twim->TASKS_STARTTX = 1;

    if (!waitForEventOrError(twim->EVENTS_SUSPENDED, twim)) {
        stopTwim(twim);
        return false;
    }

    twim->EVENTS_SUSPENDED = 0;
    twim->EVENTS_LASTRX = 0;
    twim->RXD.PTR = (uint32_t)&value;
    twim->RXD.MAXCNT = 1;
    twim->SHORTS = TWIM_SHORTS_LASTRX_STOP_Msk;
    twim->TASKS_RESUME = 1;
    twim->TASKS_STARTRX = 1;

    return waitForStop(twim) && twim->RXD.AMOUNT == 1;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value)
{
    NRF_TWIM_Type *twim = NRF_TWIM0;
    uint8_t data[] = {reg, value};

    configureTwim(twim);
    clearTwimEvents(twim);
    twim->ADDRESS = address;
    twim->TXD.PTR = (uint32_t)data;
    twim->TXD.MAXCNT = sizeof(data);
    twim->SHORTS = TWIM_SHORTS_LASTTX_STOP_Msk;
    twim->TASKS_RESUME = 1;
    twim->TASKS_STARTTX = 1;

    return waitForStop(twim);
}
} // namespace Nrf52Twim

#endif
