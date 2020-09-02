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
    WiFiServerAPI(WiFiClient &_client);

    virtual ~WiFiServerAPI();

    virtual void loop(); // Check for dropped client connections

  protected:
    /// Hookable to find out when connection changes
    virtual void onConnectionChanged(bool connected);
};

/**
 * Listens for incoming connections and does accepts and creates instances of WiFiServerAPI as needed
 */
class WiFiServerPort : public WiFiServer
{
  public:
    WiFiServerPort();

    void init();

    void loop();
};
