#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include <I2CKeyPad.h>

class PeMatrixBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit PeMatrixBase(const char *name);

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;
    bool firstTime = 1;
    // char keymap[19] = "123A456B789C*0#DNF";  // N = NoKey, F = Fail
    char keymap[19] = {0x1b, 0xb5, '3', 'A', 0xb4, 0x0d, 0xb7, 'B', '7', 0xb6, '9', 'C', 0x09, '0', 0x08, 'D', 'N', 'F'};
    char key = 0;
    char prevkey = 0;
};