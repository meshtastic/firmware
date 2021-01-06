#pragma once

#include "StreamAPI.h"
#include "concurrency/OSThread.h"
#include <WiFi.h>

/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
class WiFiServerAPI : public StreamAPI
{
  private:
    WiFiClient client;

  public:
    WiFiServerAPI(WiFiClient &_client);

    virtual ~WiFiServerAPI();

    /// @return true if we want to keep running, or false if we are ready to be destroyed
    virtual bool loop(); // Check for dropped client connections

    /// override close to also shutdown the TCP link
    virtual void close();

  protected:
    /// Hookable to find out when connection changes
    virtual void onConnectionChanged(bool connected);
};

/**
 * Listens for incoming connections and does accepts and creates instances of WiFiServerAPI as needed
 */
class WiFiServerPort : public WiFiServer, private concurrency::OSThread
{
    /** The currently open port
     *
     * FIXME: We currently only allow one open TCP connection at a time, because we depend on the loop() call in this class to
     * delegate to the worker.  Once coroutines are implemented we can relax this restriction.
     */
    WiFiServerAPI *openAPI = NULL;

  public:
    WiFiServerPort();

    void init();

    int32_t runOnce();
};
