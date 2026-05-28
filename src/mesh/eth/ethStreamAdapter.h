#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_API)

#include "ethApiHandlers.h"

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

// Adapter that exposes an EthernetClient through the transport-agnostic
// IStreamReadWrite interface. Lets handlers (both API and OTA) drive plain
// TCP exactly the same way they drive the TLS transport.
class EthernetClientStream : public IStreamReadWrite
{
  public:
    explicit EthernetClientStream(EthernetClient &c) : c_(c) {}

    size_t write(uint8_t b) override { return c_.write(b); }
    size_t write(const uint8_t *buf, size_t len) override { return c_.write(buf, len); }

    int available() override { return c_.available(); }
    int read() override { return c_.read(); }
    int read(uint8_t *buf, size_t len) override { return c_.read(buf, len); }

    bool connected() override { return c_.connected(); }
    void flush() override { c_.flush(); }
    IPAddress remoteIP() override { return c_.remoteIP(); }

  private:
    EthernetClient &c_;
};

#endif // HAS_ETHERNET && HAS_ETHERNET_API
