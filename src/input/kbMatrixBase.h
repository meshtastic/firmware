#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"

class KbMatrixBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit KbMatrixBase(const char *name);

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;
    bool firstTime = 1;
    int shift = 0;
    char key = 0;
    char prevkey = 0;
};