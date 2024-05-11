#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
#include "EspUsbHost.h"

class KbUsbBase : public Observable<const InputEvent *>, public concurrency::OSThread, public EspUsbHost
{
  public:
    explicit KbUsbBase(const char *name);

  protected:
    virtual int32_t runOnce() override;

  private:
    void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier);
    const char *_originName;
    bool firstTime = 1;
};
#endif