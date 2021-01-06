#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>


class WebServerThread : private concurrency::OSThread
{

  public:
    WebServerThread();

  protected:

    virtual int32_t runOnce();
};

extern WebServerThread webServerThread;
