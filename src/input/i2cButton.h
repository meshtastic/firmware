#pragma once

#include "InputBroker.h"
#include "OneButton.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#if defined(M5STACK_UNITC6L)

class i2cButtonThread : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    const char *_originName;
    explicit i2cButtonThread(const char *name);
    int32_t runOnce() override;
};

extern i2cButtonThread *i2cButton;
#endif
