#pragma once

#include "PhoneAPI.h"
#include "concurrency/OSThread.h"
#include <Arduino.h>
#include <functional>

void initWebServer();
void createSSLCert();

class WebServerThread : private concurrency::OSThread
{

  public:
    WebServerThread();

  protected:

    virtual int32_t runOnce();
};

extern WebServerThread *webServerThread;
