#pragma once
#include "concurrency/OSThread.h"
#include "configuration.h"

class NoiseMeasureModule : private concurrency::OSThread
{
  public:
    NoiseMeasureModule();

  protected:
    virtual int32_t runOnce() override;
};
