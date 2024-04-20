#pragma once

#include "InputBroker.h"
#include "mesh/NodeDB.h"

class UpDownInterruptBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit UpDownInterruptBase(const char *name);
    void init(uint8_t pinDown, uint8_t pinUp, uint8_t pinPress, char eventDown, char eventUp, char eventPressed,
              void (*onIntDown)(), void (*onIntUp)(), void (*onIntPress)());
    void intPressHandler();
    void intDownHandler();
    void intUpHandler();

    int32_t runOnce() override;

  protected:
    enum UpDownInterruptBaseActionType { UPDOWN_ACTION_NONE, UPDOWN_ACTION_PRESSED, UPDOWN_ACTION_UP, UPDOWN_ACTION_DOWN };

    volatile UpDownInterruptBaseActionType action = UPDOWN_ACTION_NONE;

  private:
    uint8_t _pinDown = 0;
    uint8_t _pinUp = 0;
    char _eventDown = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    char _eventUp = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    char _eventPressed = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    const char *_originName;
};
