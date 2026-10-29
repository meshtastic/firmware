#include "RotaryEncoderInterruptBase.h"

#include "UptimeClock.h"
#include "configuration.h"

const uint8_t RotaryEncoderInterruptBase::encoderTable[7][4] = {
    { R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START },
    { R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW },
    { R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START },
    { R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START },
    { R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START },
    { R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW },
    { R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START }
};

RotaryEncoderInterruptBase::RotaryEncoderInterruptBase(const char *name)
    : concurrency::OSThread(name)
{
    _originName = name;
}

void RotaryEncoderInterruptBase::init(
    uint8_t pinA,
    uint8_t pinB,
    uint8_t pinPress,
    input_broker_event eventCw,
    input_broker_event eventCcw,
    input_broker_event eventPressed,
    input_broker_event eventPressedLong,
    void (*onIntA)(),
    void (*onIntB)(),
    void (*onIntPress)()
)
{
    _pinA = pinA;
    _pinB = pinB;
    _pinPress = pinPress;

    _eventCw = eventCw;
    _eventCcw = eventCcw;
    _eventPressed = eventPressed;
    _eventPressedLong = eventPressedLong;

    (void)onIntA;
    (void)onIntB;
    (void)onIntPress;

    if (_pinA != 0) {
        pinMode(_pinA, INPUT_PULLUP);
        pinMode(_pinB, INPUT_PULLUP);
    }

    if (_pinPress != 0) {
        pinMode(_pinPress, INPUT_PULLUP);
        buttonRaw = digitalRead(_pinPress);
        buttonStable = buttonRaw;
    }

    encoderState = R_START;
    lastPinState = readPhysicalState();

    uint32_t now = Time::stampMillis();
    buttonLastChange = now;
    pressStartTime = 0;
    selectReleaseTime = 0;
    lastRotationTime = 0;

    pressDetected = false;
    longPressFired = false;
    pressAndTurnFired = false;
    pressCandidateValid = false;
    selectPending = false;

    LOG_INFO("Rotary initialized (%d, %d, %d)", _pinA, _pinB, _pinPress);
}

uint8_t RotaryEncoderInterruptBase::readPhysicalState() const
{
    uint8_t a = digitalRead(_pinA);
    uint8_t b = digitalRead(_pinB);
    return (a << 1) | b;
}

uint8_t RotaryEncoderInterruptBase::processFSM(uint8_t pinState)
{
    encoderState = encoderTable[encoderState & 0x0F][pinState];
    return encoderState & 0x30;
}

void RotaryEncoderInterruptBase::sendInputEvent(input_broker_event event)
{
    if (event == INPUT_BROKER_NONE) {
        return;
    }

    InputEvent e = {};
    e.source = _originName;
    e.inputEvent = event;
    e.kbchar = 0;
    e.touchX = 0;
    e.touchY = 0;

    LOG_DEBUG("Rotary event %d", static_cast<int>(event));
    notifyObservers(&e);
}

void RotaryEncoderInterruptBase::sendKeyboardEvent(unsigned char key)
{
    if (key == 0) {
        return;
    }

    InputEvent e = {};
    e.source = _originName;
    e.inputEvent = INPUT_BROKER_NONE;
    e.kbchar = key;
    e.touchX = 0;
    e.touchY = 0;

    notifyObservers(&e);
}

void RotaryEncoderInterruptBase::cancelPressBecauseOfRotation(uint32_t now)
{
    lastRotationTime = now;

    if (pressDetected || pressCandidateValid || selectPending) {
        LOG_DEBUG("Rotary press canceled by rotation");
    }

    pressCandidateValid = false;
    selectPending = false;
    pressAndTurnFired = false;
}

void RotaryEncoderInterruptBase::processButton(uint32_t now)
{
    if (_pinPress == 0) {
        return;
    }

    bool reading = digitalRead(_pinPress);

    if (reading != buttonRaw) {
        buttonRaw = reading;
        buttonLastChange = now;
    }

    if (now - buttonLastChange < BUTTON_DEBOUNCE_MS) {
        return;
    }

    // Button remains stably LOW.
    if (reading == LOW && buttonStable == LOW) {

        if (
            pressDetected &&
            !pressCandidateValid &&
            now - pressStartTime >= PRESS_MIN_STABLE_MS
        ) {
            if (
                lastRotationTime == 0 ||
                now - lastRotationTime > ROTATION_BLOCK_MS
            ) {
                pressCandidateValid = true;
                LOG_DEBUG("Rotary press validated");
            }
        }

        if (
            pressDetected &&
            pressCandidateValid &&
            !longPressFired &&
            !pressAndTurnEnabled() &&
            _eventPressedLong != INPUT_BROKER_NONE &&
            now - pressStartTime >= LONG_PRESS_DURATION &&
            (
                lastRotationTime == 0 ||
                now - lastRotationTime > ROTATION_BLOCK_MS
            )
        ) {
            longPressFired = true;
            selectPending = false;

            LOG_DEBUG("Rotary event Press long");
            sendInputEvent(_eventPressedLong);
        }

        return;
    }

    if (reading == buttonStable) {
        return;
    }

    buttonStable = reading;

    // Debounced press down.
    if (buttonStable == LOW) {
        pressDetected = true;
        pressStartTime = now;
        longPressFired = false;
        pressAndTurnFired = false;
        pressCandidateValid = false;
        selectPending = false;

        LOG_DEBUG("Rotary press down");
        return;
    }

    // Debounced release.
    if (!pressDetected) {
        return;
    }

    if (
        pressCandidateValid &&
        !pressAndTurnFired &&
        !longPressFired
    ) {
        if (
            lastRotationTime == 0 ||
            now - lastRotationTime > ROTATION_BLOCK_MS
        ) {
            selectPending = true;
            selectReleaseTime = now;
            LOG_DEBUG("Rotary select pending");
        }
    }

    pressDetected = false;
    pressStartTime = 0;
    pressCandidateValid = false;
    pressAndTurnFired = false;
}

void RotaryEncoderInterruptBase::processPendingSelect(uint32_t now)
{
    if (!selectPending) {
        return;
    }

    if (lastRotationTime > selectReleaseTime) {
        LOG_DEBUG("Rotary select canceled during guard");
        selectPending = false;
        return;
    }

    if (now - selectReleaseTime < SELECT_GUARD_MS) {
        return;
    }

    selectPending = false;

    LOG_DEBUG("Rotary event Press short");
    sendInputEvent(_eventPressed);
}

void RotaryEncoderInterruptBase::handleRotation(bool cw, uint32_t now)
{
    // Preserve a legitimate press+turn before invalidating a normal SELECT.
    if (
        pressDetected &&
        buttonStable == LOW &&
        pressAndTurnEnabled()
    ) {
        lastRotationTime = now;
        pressCandidateValid = false;
        selectPending = false;

        unsigned char key = cw ? _pressAndTurnCw : _pressAndTurnCcw;

        LOG_DEBUG("Rotary event Press %s", cw ? "CW" : "CCW");
        sendKeyboardEvent(key);
        pressAndTurnFired = true;
        return;
    }

    cancelPressBecauseOfRotation(now);

    if (cw) {
        LOG_DEBUG("Rotary event CW");
        sendInputEvent(_eventCw);
    } else {
        LOG_DEBUG("Rotary event CCW");
        sendInputEvent(_eventCcw);
    }
}

int32_t RotaryEncoderInterruptBase::runOnce()
{
    uint32_t now = Time::stampMillis();

    processButton(now);

    uint8_t pinState = readPhysicalState();

    if (pinState != lastPinState) {
        lastPinState = pinState;

        uint8_t result = processFSM(pinState);

        if (result == DIR_CW) {
            handleRotation(true, now);
        } else if (result == DIR_CCW) {
            handleRotation(false, now);
        }
    }

    processPendingSelect(now);

    return 1;
}

void RotaryEncoderInterruptBase::intAHandler()
{
}

void RotaryEncoderInterruptBase::intBHandler()
{
}

void RotaryEncoderInterruptBase::intPressHandler()
{
}

void RotaryEncoderInterruptBase::setPressAndTurnChars(
    unsigned char cw,
    unsigned char ccw
)
{
    _pressAndTurnCw = cw;
    _pressAndTurnCcw = ccw;
}
