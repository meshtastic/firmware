#pragma once

#include "StreamAPI.h"
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
    explicit WiFiServerAPI(WiFiClient &_client);

    virtual ~WiFiServerAPI();

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

    /// If an api server is running, we try to spit out debug 'serial' characters there
    static void debugOut(char c);
    
  protected:
    int32_t runOnce() override;
};

void initApiServer();
