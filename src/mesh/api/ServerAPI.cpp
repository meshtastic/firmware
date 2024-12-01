#include "ServerAPI.h"
#include "configuration.h"
#include <Arduino.h>

template <typename T>
ServerAPI<T>::ServerAPI(T &_client) : StreamAPI(&client), concurrency::OSThread("ServerAPI"), client(_client)
{
    LOG_INFO("Incoming API connection");
}

template <typename T> ServerAPI<T>::~ServerAPI()
{
    client.stop();
}

template <typename T> void ServerAPI<T>::close()
{
    client.stop(); // drop tcp connection
    StreamAPI::close();
}

/// Check the current underlying physical link to see if the client is currently connected
template <typename T> bool ServerAPI<T>::checkIsConnected()
{
    return client.connected();
}

template <class T> int32_t ServerAPI<T>::runOnce()
{
    if (client.connected()) {
        return StreamAPI::runOncePart();
    } else {
        LOG_INFO("Client dropped connection, suspend API service");
        enabled = false; // we no longer need to run
        return 0;
    }
}

template <class T, class U> APIServerPort<T, U>::APIServerPort(int port) : U(port), concurrency::OSThread("ApiServer") {}

template <class T, class U> void APIServerPort<T, U>::init()
{
    U::begin();
}

template <class T, class U> int32_t APIServerPort<T, U>::runOnce()
{
#ifdef ARCH_ESP32
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    auto client = U::accept();
#else
    auto client = U::available();
#endif
#else
    auto client = U::available();
#endif
    if (client) {
        // Close any previous connection (see FIXME in header file)
        if (openAPI) {
#if RAK_4631
            // RAK13800 Ethernet requests periodically take more time
            // This backoff addresses most cases keeping max wait < 1s
            // Reconnections are delayed by full wait time
            if (waitTime < 400) {
                waitTime *= 2;
                LOG_INFO("Previous TCP connection still open, try again in %dms", waitTime);
                return waitTime;
            }
#endif
            LOG_INFO("Force close previous TCP connection");
            delete openAPI;
        }

        openAPI = new T(client);
    }

#if RAK_4631
    waitTime = 100;
#endif
    return 100; // only check occasionally for incoming connections
}
