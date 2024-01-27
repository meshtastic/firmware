#pragma once

#include "InputBroker.h"
#include "mesh/NodeDB.h"

class TrackballInterruptBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit TrackballInterruptBase(const char *name);
    void init(uint8_t pinDown, uint8_t pinUp, uint8_t pinLeft, uint8_t pinRight, uint8_t pinPress, char eventDown, char eventUp,
              char eventLeft, char eventRight, char eventPressed, void (*onIntDown)(), void (*onIntUp)(), void (*onIntLeft)(),
              void (*onIntRight)(), void (*onIntPress)());
    void intPressHandler();
    void intDownHandler();
    void intUpHandler();
    void intLeftHandler();
    void intRightHandler();

    virtual int32_t runOnce() override;

  protected:
    enum TrackballInterruptBaseActionType {
        TB_ACTION_NONE,
        TB_ACTION_PRESSED,
        TB_ACTION_UP,
        TB_ACTION_DOWN,
        TB_ACTION_LEFT,
        TB_ACTION_RIGHT
    };

    volatile TrackballInterruptBaseActionType action = TB_ACTION_NONE;

  private:
    uint8_t _pinDown = 0;
    uint8_t _pinUp = 0;
    uint8_t _pinLeft = 0;
    uint8_t _pinRight = 0;
    char _eventDown = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    char _eventUp = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    char _eventLeft = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    char _eventRight = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    char _eventPressed = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    const char *_originName;
};
