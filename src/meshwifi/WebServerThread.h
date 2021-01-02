#pragma once

#include "concurrency/OSThread.h"
#include "concurrency/Periodic.h"
#include <Arduino.h>
#include <functional>
#include "configuration.h"


class WebServerThread : private concurrency::OSThread
{

  public:
    WebServerThread();


  protected:

    virtual int32_t runOnce();
};

extern WebServerThread webServerThread;