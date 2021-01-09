#include "mesh/wifi/WebServerThread.h"
#include "meshwifi/meshhttp.h"
#include <Arduino.h>

WebServerThread *webServerThread;

WebServerThread::WebServerThread() : concurrency::OSThread("WebServerThread") {}

int32_t WebServerThread::runOnce()
{
    //DEBUG_MSG("WebServerThread::runOnce()\n");
    handleWebResponse();

    // Loop every 5ms. 
    return (5);
}

