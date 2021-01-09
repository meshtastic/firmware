#include "mesh/wifi/WebServerThread.h"
#include <Arduino.h>

WebServerThread *webServerThread;

WebServerThread::WebServerThread() : concurrency::OSThread("WebServerThread") {}

int32_t WebServerThread::runOnce()
{
    DEBUG_MSG("WebServerThread::runOnce()\n");

    return (1000 * 1);
}

