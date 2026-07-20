#pragma once

#include "configuration.h"

#if HAS_ETHERNET && (defined(HAS_ETHERNET_API) || defined(HAS_ETHERNET_TLS_API))

#include <Arduino.h>
#include <IPAddress.h>
#include <Print.h>

// Transport-agnostic byte stream used by the HTTP request parser + handlers.
// Lets the same code drive plain TCP (EthernetClient) and TLS (mbedtls_ssl)
// transports without recompiling the handlers.
//
// Inherits Print so all `print(int)`, `print(const char *)`, `print(char)`
// helpers are available for free - the only thing implementations have to
// supply on the write side is `write(uint8_t)` + the bulk `write(buf, len)`.
class IStreamReadWrite : public Print
{
  public:
    virtual ~IStreamReadWrite() = default;

    // Write side - Print pure virtual + bulk override
    size_t write(uint8_t b) override = 0;
    size_t write(const uint8_t *buf, size_t len) override = 0;
    using Print::write; // bring in write(const char *str) and friends

    // Read side
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int read(uint8_t *buf, size_t len) = 0;

    // Status
    virtual bool connected() = 0;
    void flush() override = 0; // Print::flush is virtual void with empty default

    // Logging helper - used by request log line
    virtual IPAddress remoteIP() = 0;
};

// Drive a single HTTP request → routing → response cycle on the given
// transport. Caller is responsible for closing the underlying connection
// after this returns.
void handleApiClient(IStreamReadWrite &client);

#endif // HAS_ETHERNET && (HAS_ETHERNET_API || HAS_ETHERNET_TLS_API)
