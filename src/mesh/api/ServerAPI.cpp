#include "ServerAPI.h"
#include "configuration.h"
#include <Arduino.h>

template <typename T>
ServerAPI<T>::ServerAPI(T &_client) : StreamAPI(&client), concurrency::OSThread("ServerAPI"), client(_client)
{
    LOG_INFO("Incoming wifi connection\n");
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
        LOG_INFO("Client dropped connection, suspending API service\n");
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
    auto client = U::available();
    if (client) {
        // Close any previous connection (see FIXME in header file)
        if (openAPI) {
            LOG_INFO("Force closing previous TCP connection\n");
            delete openAPI;
        }

        openAPI = new T(client);
    }

    return 100; // only check occasionally for incoming connections
}
