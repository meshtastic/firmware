#include "meshwifi/WebServerThread.h"
#include <Arduino.h>

// Thread for the HTTP Server
WebServerThread webServerThread;

WebServerThread::WebServerThread() : concurrency::OSThread("WebServerThread") {}

int32_t WebServerThread::runOnce()
{
    DEBUG_MSG("WebServerThread::runOnce()\n");

    return (1000 * 1);
}
