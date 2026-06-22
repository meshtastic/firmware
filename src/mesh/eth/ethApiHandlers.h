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
// helpers are available for free — the only thing implementations have to
// supply on the write side is `write(uint8_t)` + the bulk `write(buf, len)`.
class IStreamReadWrite : public Print
{
  public:
    virtual ~IStreamReadWrite() = default;

    // Write side — Print pure virtual + bulk override
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

    // Logging helper — used by request log line
    virtual IPAddress remoteIP() = 0;
};

// Parsed HTTP request line + headers we care about. Shared between the
// API handlers (fromradio/toradio) and the OTA handlers (nonce/upload) —
// the OTA flow needs the extra X-OTA-* metadata headers, but if we ran a
// second parser inside the OTA module the underlying stream would already
// be past the headers; capturing them in one pass is simpler.
struct Request {
    String method;
    String path;
    String query; // raw "key=val&..." (without leading '?'), empty if none
    long contentLength = 0;

    // OTA-specific (empty/0 when not present)
    String xOtaNonce;
    String xOtaAuth;
    long xOtaSize = 0;
    String xOtaCrc32;
};

// Parse the request line + headers from the transport. Returns true on
// success (req populated through the blank-line terminator), false on
// timeout / disconnect / malformed.
bool parseRequest(IStreamReadWrite &client, Request &req, uint32_t deadlineMs);

// Drive a single HTTP request → routing → response cycle on the given
// transport. Caller is responsible for closing the underlying connection
// after this returns.
void handleApiClient(IStreamReadWrite &client);

#endif // HAS_ETHERNET && (HAS_ETHERNET_API || HAS_ETHERNET_TLS_API)
