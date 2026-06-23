#include "ServerAPI.h"
#include "Throttle.h"
#include "configuration.h"
#include <Arduino.h>

static constexpr uint32_t TCP_IDLE_TIMEOUT_MS = 15 * 60 * 1000UL;

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

/// Check the current underlying physical link to see if the client is currently
/// connected
template <typename T> bool ServerAPI<T>::checkIsConnected()
{
    return client.connected();
}

template <typename T> bool ServerAPI<T>::canWriteFrame(size_t)
{
    // Only a dropped link is a reason to refuse a write up front. A full transmit
    // buffer (availableForWrite() == 0) is normal backpressure, not a dead socket,
    // so we must not close the connection on it. A genuinely failed write is
    // detected after the fact in onFrameWriteFailed().
    if (!client.connected()) {
        canWrite = false;
        enabled = false;
        LOG_WARN("TCP client disconnected before write, closing API service");
        close();
        return false;
    }

    return true;
}

template <typename T> void ServerAPI<T>::onFrameWriteFailed(size_t frameLen, size_t writtenLen)
{
    canWrite = false;
    enabled = false;
    LOG_WARN("TCP client write short (%lu/%lu bytes), closing API service", (unsigned long)writtenLen, (unsigned long)frameLen);
    close();
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
    // Clean up previous connection if its client already disconnected
    if (openAPI && !openAPI->checkIsConnected()) {
        openAPI.reset();
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
            openAPI.reset();
        }

        openAPI.reset(new T(client));
    }

#if RAK_4631
    waitTime = 100;
#endif
    return 100; // only check occasionally for incoming connections
}
