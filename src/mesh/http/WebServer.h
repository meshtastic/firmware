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
    uint32_t requestRestart = 0;

  protected:
    virtual int32_t runOnce() override;
};

extern WebServerThread *webServerThread;
