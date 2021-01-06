#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>


class JMTest : private concurrency::OSThread
{

  public:
    JMTest();

  protected:

    virtual int32_t runOnce();
};

extern JMTest jMTest;
