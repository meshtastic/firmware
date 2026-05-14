#include "ServerAPI.h"
#include "Throttle.h"
#include "configuration.h"
#include <Arduino.h>
#include <algorithm>

#ifndef MESHTASTIC_TCP_API_IDLE_TIMEOUT_MS
#define MESHTASTIC_TCP_API_IDLE_TIMEOUT_MS (15 * 60 * 1000UL)
#endif

#ifndef MESHTASTIC_TCP_API_MAX_CLIENTS
#define MESHTASTIC_TCP_API_MAX_CLIENTS 1
#endif

static constexpr uint32_t TCP_IDLE_TIMEOUT_MS = MESHTASTIC_TCP_API_IDLE_TIMEOUT_MS;
static constexpr size_t TCP_API_MAX_CLIENTS = MESHTASTIC_TCP_API_MAX_CLIENTS;

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
        if (lastContactMsec > 0 && !Throttle::isWithinTimespanMs(lastContactMsec, TCP_IDLE_TIMEOUT_MS)) {
            LOG_WARN("TCP connection timeout, no data for %lu ms", (unsigned long)(millis() - lastContactMsec));
            close();
            enabled = false;
            return 0;
        }
        return StreamAPI::runOncePart();
    } else {
        LOG_INFO("Client dropped connection, suspend API service");
        close();
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
    // Clean up connections whose clients already disconnected.
    for (auto api = openAPIs.begin(); api != openAPIs.end();) {
        if (!(*api)->checkIsConnected()) {
            api = openAPIs.erase(api);
        } else {
            ++api;
        }
    }

#ifdef ARCH_ESP32
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    auto client = U::accept();
#else
    auto client = U::available();
#endif
#elif defined(ARCH_RP2040) || defined(ARCH_NRF52)
    auto client = U::accept();
#else
    auto client = U::available();
#endif
    if (client) {
        if (openAPIs.size() >= TCP_API_MAX_CLIENTS) {
#if MESHTASTIC_TCP_API_MAX_CLIENTS <= 1
            // Preserve historical single-client behavior unless a variant explicitly opts into a client pool.
#if RAK_4631
            // RAK13800 Ethernet requests periodically take more time.
            // This backoff addresses most cases keeping max wait < 1s.
            // Reconnections are delayed by full wait time.
            if (waitTime < 400) {
                waitTime *= 2;
                LOG_INFO("Previous TCP connection still open, try again in %dms", waitTime);
                return waitTime;
            }
#endif
            LOG_INFO("Force close previous TCP connection");
            openAPIs.clear();
#else
            auto oldest =
                std::min_element(openAPIs.begin(), openAPIs.end(), [](const std::unique_ptr<T> &a, const std::unique_ptr<T> &b) {
                    return a->getLastContactMsec() < b->getLastContactMsec();
                });
            if (oldest != openAPIs.end()) {
                LOG_WARN("TCP API client limit reached (%u), closing oldest connection", (unsigned)TCP_API_MAX_CLIENTS);
                openAPIs.erase(oldest);
            }
#endif
        }

        openAPIs.emplace_back(new T(client));
    }

#if RAK_4631
    waitTime = 100;
#endif
    return 100; // only check occasionally for incoming connections
}
