#pragma once

#include "StreamAPI.h"

#define SERVER_API_DEFAULT_PORT 4403

/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
template <class T> class ServerAPI : public StreamAPI, private concurrency::OSThread
{
  private:
    T client;

  public:
    explicit ServerAPI(T &_client);

    virtual ~ServerAPI();

    /// override close to also shutdown the TCP link
    virtual void close();

  protected:
    /// We override this method to prevent publishing EVENT_SERIAL_CONNECTED/DISCONNECTED for wifi links (we want the board to
    /// stay in the POWERED state to prevent disabling wifi)
    virtual void onConnectionChanged(bool connected) override {}

    virtual int32_t runOnce() override; // Check for dropped client connections

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;
};

/**
 * Listens for incoming connections and does accepts and creates instances of ServerAPI as needed
 */
template <class T, class U> class APIServerPort : public U, private concurrency::OSThread
{
    /** The currently open port
     *
     * FIXME: We currently only allow one open TCP connection at a time, because we depend on the loop() call in this class to
     * delegate to the worker.  Once coroutines are implemented we can relax this restriction.
     */
    T *openAPI = NULL;
#if RAK_4631
    // Track wait time for RAK13800 Ethernet requests
    int32_t waitTime = 100;
#endif

  public:
    explicit APIServerPort(int port);

    void init();

  protected:
    int32_t runOnce() override;
};
