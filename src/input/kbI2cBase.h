#pragma once

#include "SinglePortModule.h" // TODO: what header file to include?
#include "InputBroker.h"

class KbI2cBase :
    public Observable<const InputEvent *>,
    private concurrency::OSThread
{
  public:
    explicit KbI2cBase(const char *name);

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;
};
