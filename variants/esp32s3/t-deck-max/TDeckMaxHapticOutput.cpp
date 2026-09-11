#include "TDeckMaxHapticOutput.h"

#include "DebugConfiguration.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"
#include "platform/DevicePowerController.h"
#include "platform/DeviceVariant.h"
#include "TDeckMaxBoard.h"

#include <Arduino.h>

bool TDeckMaxHapticOutput::begin()
{
    power = getDevicePowerController();
    if (!power)
        return false;

    power->setMotorPower(true);
    delay(10);
    if (!driver.begin()) {
        power->setMotorPower(false);
        LOG_ERROR("T-Deck-MAX: DRV2605 initialization failed");
        return false;
    }

    driver.selectLibrary(1);
    driver.setMode(DRV2605_MODE_INTTRIG);
    power->setMotorPower(false);
    ready = true;
    return true;
}

void TDeckMaxHapticOutput::play(HapticEffect effect)
{
    concurrency::LockGuard guard(&driverLock);
    if (!ready || effect == HapticEffect::NONE)
        return;

    pulseActive = false;
    triggerLocked(effect);
}

void TDeckMaxHapticOutput::triggerLocked(HapticEffect effect)
{
    if (!ready || effect == HapticEffect::NONE)
        return;

    if (!motorPowerOn) {
        power->setMotorPower(true);
        motorPowerOn = true;
    }
    motorPowerChangedAt = millis();
    driver.setWaveform(0, static_cast<uint8_t>(effect));
    driver.setWaveform(1, 0);
    driver.go();
}

void TDeckMaxHapticOutput::stop()
{
    concurrency::LockGuard guard(&driverLock);
    if (!ready)
        return;

    stopDriverLocked();
    delayedPulsePending = false;
}

void TDeckMaxHapticOutput::stopDriverLocked()
{
    driver.stop();
    pulseActive = false;
    if (motorPowerOn)
        motorPowerChangedAt = millis();
}

void TDeckMaxHapticOutput::pulse(uint16_t durationMs)
{
    concurrency::LockGuard guard(&driverLock);
    if (!ready)
        return;

    triggerLocked(HapticEffect::SELECT);
    pulseStartedAt = millis();
    pulseDuration = durationMs;
    pulseActive = true;
}

void TDeckMaxHapticOutput::armDelayedPulse(uint16_t delayMs, uint16_t durationMs)
{
    concurrency::LockGuard guard(&driverLock);
    if (!ready)
        return;

    delayedPulseStartedAt = millis();
    delayedPulseDelay = delayMs;
    delayedPulseDuration = durationMs;
    delayedPulsePending = true;
}

void TDeckMaxHapticOutput::cancelDelayedPulse()
{
    concurrency::LockGuard guard(&driverLock);
    delayedPulsePending = false;
}

int32_t TDeckMaxHapticOutput::runOnce()
{
    concurrency::LockGuard guard(&driverLock);

    if (pulseActive && !Throttle::isWithinTimespanMs(pulseStartedAt, pulseDuration))
        stopDriverLocked();

    if (delayedPulsePending && !Throttle::isWithinTimespanMs(delayedPulseStartedAt, delayedPulseDelay)) {
        const uint16_t duration = delayedPulseDuration;
        delayedPulsePending = false;
        triggerLocked(HapticEffect::SELECT);
        pulseStartedAt = millis();
        pulseDuration = duration;
        pulseActive = true;
    }

    if (!motorPowerOn)
        return delayedPulsePending ? 10 : INT_MAX;

    if (!Throttle::isWithinTimespanMs(motorPowerChangedAt, t_deck_max::HAPTIC_POWER_HOLD_MS)) {
        power->setMotorPower(false);
        motorPowerOn = false;
    }

    return pulseActive || delayedPulsePending || motorPowerOn ? 10 : INT_MAX;
}
