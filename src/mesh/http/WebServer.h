#pragma once

#include "PhoneAPI.h"
#include "concurrency/OSThread.h"
#include <Arduino.h>
#include <functional>

void initWebServer();
void createSSLCert();

class WebServerThread : private concurrency::OSThread
{
  private:
    uint32_t lastActivityTime = 0;

  public:
    WebServerThread();
    uint32_t requestRestart = 0;
    void markActivity();

  protected:
    virtual int32_t runOnce() override;
    int32_t getAdaptiveInterval();
};

extern WebServerThread *webServerThread;
