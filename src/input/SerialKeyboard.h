#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"

class SerialKeyboard : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit SerialKeyboard(const char *name);

  protected:
    virtual int32_t runOnce() override;
    void erase();

  private:
    const char *_originName;
    bool firstTime = 1;
    int prevKeys = 0;
    int keys = 0;
    int shift = 0;
    int keyPressed = 13;
    int lastKeyPressed = 13;
    int quickPress = 0;
    unsigned long lastPressTime = 0;
};