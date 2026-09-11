#include "HapticFeedback.h"

#include "InputBroker.h"
#include "configuration.h"
#include "platform/DeviceVariant.h"

#include <Arduino.h>
#include <algorithm>
#include <climits>

#include "DebugConfiguration.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"

#ifdef HAPTIC_FEEDBACK_PIN
#ifdef HAPTIC_FEEDBACK_ACTIVE_LOW
#define HAPTIC_FEEDBACK_ON_STATE LOW
#define HAPTIC_FEEDBACK_OFF_STATE HIGH
#else
#define HAPTIC_FEEDBACK_ON_STATE HIGH
#define HAPTIC_FEEDBACK_OFF_STATE LOW
#endif
#endif

namespace
{
static constexpr char HAPTIC_PREFERENCE_FILE[] = "/prefs/haptic.dat";
static constexpr char HAPTIC_PREFERENCE_TEMP_FILE[] = "/prefs/haptic.dat.tmp";
} // namespace

HapticFeedback *hapticFeedback = nullptr;

bool isValidHapticPreferenceRecord(const HapticPreferenceRecord &record, size_t bytesRead)
{
    return bytesRead == sizeof(record) && record.magic == HAPTIC_PREFERENCE_MAGIC &&
           record.version == HAPTIC_PREFERENCE_VERSION && record.reserved == 0 && record.enabled <= 1;
}

bool shouldPlayHapticEffect(bool enabled, HapticEffect effect)
{
    return enabled && effect != HapticEffect::NONE;
}

HapticEffect hapticEffectForInputEvent(uint8_t inputEvent)
{
    switch (inputEvent) {
    case INPUT_BROKER_UP:
    case INPUT_BROKER_DOWN:
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_RIGHT:
        return HapticEffect::NAVIGATION;

    case INPUT_BROKER_SELECT:
    case INPUT_BROKER_USER_PRESS:
    case INPUT_BROKER_ALT_PRESS:
        return HapticEffect::SELECT;

    case INPUT_BROKER_CANCEL:
    case INPUT_BROKER_BACK:
        return HapticEffect::BACK;

    case INPUT_BROKER_SELECT_LONG:
    case INPUT_BROKER_UP_LONG:
    case INPUT_BROKER_DOWN_LONG:
    case INPUT_BROKER_ALT_LONG:
    case INPUT_BROKER_SHUTDOWN:
        return HapticEffect::LONG_PRESS;

    case INPUT_BROKER_SEND_PING:
        return HapticEffect::PING;

    case INPUT_BROKER_ANYKEY:
    case INPUT_BROKER_MATRIXKEY:
    case INPUT_BROKER_FN_F1:
    case INPUT_BROKER_FN_F2:
    case INPUT_BROKER_FN_F3:
    case INPUT_BROKER_FN_F4:
    case INPUT_BROKER_FN_F5:
        return HapticEffect::KEYPRESS;

    default:
        return HapticEffect::NONE;
    }
}

void initHapticFeedback()
{
    if (!hapticFeedback)
        hapticFeedback = new HapticFeedback();
}

HapticFeedback::HapticFeedback() : concurrency::OSThread("Haptic"), output(getHapticOutput())
{
    if (output)
        outputReady = output->begin();

#ifdef HAPTIC_FEEDBACK_PIN
    pinMode(HAPTIC_FEEDBACK_PIN, OUTPUT);
    digitalWrite(HAPTIC_FEEDBACK_PIN, HAPTIC_FEEDBACK_OFF_STATE);
#endif

    enabled.store(loadSettings(), std::memory_order_relaxed);
}

bool HapticFeedback::isEnabled() const
{
    return enabled.load(std::memory_order_relaxed);
}

void HapticFeedback::setEnabled(bool value)
{
    const bool previous = enabled.exchange(value, std::memory_order_relaxed);
    if (previous == value)
        return;

    if (!value)
        stop();

    if (!saveSettings())
        LOG_ERROR("Could not save haptic preference");
}

bool HapticFeedback::loadSettings()
{
#ifdef FSCom
    HapticPreferenceRecord record = {};
    size_t bytesRead = 0;
    bool fileRead = false;

    {
        concurrency::LockGuard guard(spiLock);
        auto file = FSCom.open(HAPTIC_PREFERENCE_FILE, FILE_O_READ);
        if (file) {
            bytesRead = file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record));
            file.close();
            fileRead = true;
        }
    }

    if (fileRead && isValidHapticPreferenceRecord(record, bytesRead))
        return record.enabled != 0;

    if (fileRead)
        LOG_WARN("Invalid haptic preference, using enabled default");
#endif
    return true;
}

bool HapticFeedback::saveSettings() const
{
#ifdef FSCom
    const HapticPreferenceRecord record = {
        HAPTIC_PREFERENCE_MAGIC,
        HAPTIC_PREFERENCE_VERSION,
        static_cast<uint8_t>(enabled.load(std::memory_order_relaxed)),
        0,
    };

    concurrency::LockGuard guard(spiLock);

    if (!FSCom.exists("/prefs") && !FSCom.mkdir("/prefs"))
        return false;

    if (FSCom.exists(HAPTIC_PREFERENCE_TEMP_FILE) && !FSCom.remove(HAPTIC_PREFERENCE_TEMP_FILE))
        return false;

    auto file = FSCom.open(HAPTIC_PREFERENCE_TEMP_FILE, FILE_O_WRITE);
    if (!file)
        return false;

    const size_t bytesWritten = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    file.flush();
    file.close();

    if (bytesWritten != sizeof(record)) {
        FSCom.remove(HAPTIC_PREFERENCE_TEMP_FILE);
        return false;
    }

    if (FSCom.exists(HAPTIC_PREFERENCE_FILE) && !FSCom.remove(HAPTIC_PREFERENCE_FILE)) {
        FSCom.remove(HAPTIC_PREFERENCE_TEMP_FILE);
        return false;
    }

    if (!FSCom.rename(HAPTIC_PREFERENCE_TEMP_FILE, HAPTIC_PREFERENCE_FILE)) {
        FSCom.remove(HAPTIC_PREFERENCE_TEMP_FILE);
        return false;
    }
    return true;
#else
    return false;
#endif
}

void HapticFeedback::play(HapticEffect effect)
{
    if (!shouldPlayHapticEffect(enabled.load(std::memory_order_relaxed), effect))
        return;

    if (outputReady) {
        output->play(effect);
        setIntervalFromNow(10);
        return;
    }

#ifdef HAPTIC_FEEDBACK_PIN
    uint16_t durationMs = 30;
    switch (effect) {
    case HapticEffect::NAVIGATION:
        durationMs = 20;
        break;
    case HapticEffect::SELECT:
        durationMs = 35;
        break;
    case HapticEffect::BACK:
        durationMs = 25;
        break;
    case HapticEffect::LONG_PRESS:
    case HapticEffect::PING:
        durationMs = 60;
        break;
    case HapticEffect::KEYPRESS:
        durationMs = 12;
        break;
    case HapticEffect::MESSAGE:
        durationMs = 80;
        break;
    default:
        break;
    }
    pulse(durationMs);
#else
    (void)effect;
#endif
}

void HapticFeedback::stop()
{
    if (outputReady) {
        output->stop();
        setIntervalFromNow(10);
    }

#ifdef HAPTIC_FEEDBACK_PIN
    motorWrite(false);
    pulseActive = false;
    delayedPulsePending = false;
#endif
}

void HapticFeedback::pulse(uint16_t durationMs)
{
    if (!enabled.load(std::memory_order_relaxed))
        return;

    if (outputReady) {
        output->pulse(durationMs);
        setIntervalFromNow(10);
        return;
    }

#ifdef HAPTIC_FEEDBACK_PIN
    motorWrite(true);
    pulseStartedAt = millis();
    pulseDuration = durationMs;
    pulseActive = true;
    scheduleNext();
#else
    (void)durationMs;
#endif
}

void HapticFeedback::armDelayedPulse(uint16_t delayMs, uint16_t durationMs)
{
    if (!enabled.load(std::memory_order_relaxed))
        return;

    if (outputReady) {
        output->armDelayedPulse(delayMs, durationMs);
        setIntervalFromNow(10);
        return;
    }

#ifdef HAPTIC_FEEDBACK_PIN
    delayedPulseStartedAt = millis();
    delayedPulseDelay = delayMs;
    delayedPulseDuration = durationMs;
    delayedPulsePending = true;
    scheduleNext();
#else
    (void)delayMs;
    (void)durationMs;
#endif
}

void HapticFeedback::cancelDelayedPulse()
{
    if (outputReady)
        output->cancelDelayedPulse();

#ifdef HAPTIC_FEEDBACK_PIN
    delayedPulsePending = false;
#endif
}

#ifdef HAPTIC_FEEDBACK_PIN
void HapticFeedback::motorWrite(bool on)
{
    digitalWrite(HAPTIC_FEEDBACK_PIN, on ? HAPTIC_FEEDBACK_ON_STATE : HAPTIC_FEEDBACK_OFF_STATE);
}

void HapticFeedback::scheduleNext()
{
    if (!pulseActive && !delayedPulsePending)
        return;
    setIntervalFromNow(10);
}
#endif

int32_t HapticFeedback::runOnce()
{
    int32_t nextInterval = INT_MAX;

    if (outputReady)
        nextInterval = output->runOnce();

#ifdef HAPTIC_FEEDBACK_PIN
    if (pulseActive && !Throttle::isWithinTimespanMs(pulseStartedAt, pulseDuration)) {
        motorWrite(false);
        pulseActive = false;
    }

    if (delayedPulsePending && !Throttle::isWithinTimespanMs(delayedPulseStartedAt, delayedPulseDelay)) {
        const uint16_t duration = delayedPulseDuration;
        delayedPulsePending = false;
        pulse(duration);
    }

    if (pulseActive || delayedPulsePending)
        nextInterval = std::min<int32_t>(nextInterval, 10);
#endif

    return nextInterval;
}

void playNavigationHaptic()
{
    if (getHapticOutput() && hapticFeedback)
        hapticFeedback->play(HapticEffect::NAVIGATION);
}
