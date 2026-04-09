#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#if defined(HAS_PCF8574_BUTTON)

class PCF8574ButtonThread : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit PCF8574ButtonThread(const char *name);
    int32_t runOnce() override;
    
  private:
    const char *_originName;
    uint8_t readPCF8574();
    void writePCF8574(uint8_t value);
    uint8_t lastState;
    bool initialized;
    
    // 按键状态跟踪
    struct ButtonState {
        bool pressed;
        uint32_t pressStartTime;
        uint32_t lastRepeatTime;
        bool shortPressTriggered;
        bool longPressTriggered;
    };
    
    ButtonState buttonStates[8]; // PCF8574有8个引脚
};

extern PCF8574ButtonThread *pcf8574Button;

#endif
