#pragma once

#include "PhoneAPI.h"
#include "concurrency/OSThread.h"
#include <Arduino.h>
#include <functional>

void initWebServer();
void createSSLCert();

void handleNotFound();

void handleWebResponse();


//void handleHotspot();

//void handleStyleCSS();
//void handleRoot();


// Interface to the PhoneAPI to access the protobufs with messages
class HttpAPI : public PhoneAPI
{

  public:
    // Nothing here yet

  private:
    // Nothing here yet

  protected:
    // Nothing here yet
};

class WebServerThread : private concurrency::OSThread
{

  public:
    WebServerThread();

  protected:

    virtual int32_t runOnce();
};

extern WebServerThread *webServerThread;
