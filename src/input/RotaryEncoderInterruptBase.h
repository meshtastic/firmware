#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"

class RotaryEncoderInterruptBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit RotaryEncoderInterruptBase(const char *name);

    void init(
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
    );

    void intPressHandler();
    void intAHandler();
    void intBHandler();

    void setPressAndTurnChars(unsigned char cw, unsigned char ccw);

    // Turning while the button is held emits `cw` / `ccw` as kbchar and suppresses the press
    // event. Both default to 0, leaving rotation-while-pressed ignored.
  protected:
    virtual int32_t runOnce() override;

  private:
    static constexpr uint8_t DIR_NONE = 0x00;
    static constexpr uint8_t DIR_CW   = 0x10;
    static constexpr uint8_t DIR_CCW  = 0x20;

    static constexpr uint8_t R_START      = 0x00;
    static constexpr uint8_t R_CW_FINAL   = 0x01;
    static constexpr uint8_t R_CW_BEGIN   = 0x02;
    static constexpr uint8_t R_CW_NEXT    = 0x03;
    static constexpr uint8_t R_CCW_BEGIN  = 0x04;
    static constexpr uint8_t R_CCW_FINAL  = 0x05;
    static constexpr uint8_t R_CCW_NEXT   = 0x06;

    static const uint8_t encoderTable[7][4];

    uint8_t encoderState = R_START;
    uint8_t lastPinState = 0xFF;

    uint8_t _pinA = 0;
    uint8_t _pinB = 0;
    uint8_t _pinPress = 0;

    input_broker_event _eventCw = INPUT_BROKER_NONE;
    input_broker_event _eventCcw = INPUT_BROKER_NONE;
    input_broker_event _eventPressed = INPUT_BROKER_NONE;
    input_broker_event _eventPressedLong = INPUT_BROKER_NONE;

    const char *_originName = nullptr;

    bool buttonRaw = HIGH;
    bool buttonStable = HIGH;

    uint32_t buttonLastChange = 0;
    uint32_t pressStartTime = 0;
    uint32_t selectReleaseTime = 0;
    uint32_t lastRotationTime = 0;

    bool pressDetected = false;
    bool longPressFired = false;
    bool pressAndTurnFired = false;
    bool pressCandidateValid = false;
    bool selectPending = false;

    static constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
    static constexpr uint32_t PRESS_MIN_STABLE_MS = 80;
    static constexpr uint32_t LONG_PRESS_DURATION = 300;
    static constexpr uint32_t SELECT_GUARD_MS = 120;
    static constexpr uint32_t ROTATION_BLOCK_MS = 150;

    unsigned char _pressAndTurnCw = 0;
    unsigned char _pressAndTurnCcw = 0;

    bool pressAndTurnEnabled() const
    {
        return _pressAndTurnCw != 0 || _pressAndTurnCcw != 0;
    }

    uint8_t readPhysicalState() const;
    uint8_t processFSM(uint8_t pinState);

    void processButton(uint32_t now);
    void processPendingSelect(uint32_t now);
    void handleRotation(bool cw, uint32_t now);
    void cancelPressBecauseOfRotation(uint32_t now);

    void sendInputEvent(input_broker_event event);
    void sendKeyboardEvent(unsigned char key);
};
