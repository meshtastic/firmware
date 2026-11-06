#pragma once

#include "configuration.h"

// Needs POSIX serial ports and sockets: Linux and macOS meshtasticd, not the Windows or browser builds.
#if defined(ARCH_PORTDUINO) && !defined(ARCH_PORTDUINO_WASM) && !defined(_WIN32)
#define HAS_TRANSPARENT_RADIO 1
#else
#define HAS_TRANSPARENT_RADIO 0
#endif

#if HAS_TRANSPARENT_RADIO

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

/** Byte pipe to a node running with LoRaConfig.transparent_mode, carrying its framed client API. */
class TransparentLink
{
  public:
    virtual ~TransparentLink() = default;

    /// (Re)open the link. Returns false if the device can't be reached right now.
    virtual bool open() = 0;
    /// True when open() returned false only because it is still resolving the host; call open() again soon.
    virtual bool openPending() const { return false; }
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    /// Read up to len bytes without blocking: the count read (0 when none waiting), or -1 if the link is gone.
    virtual int read(uint8_t *buf, size_t len) = 0;

    /// Write all of buf. False if the link is gone.
    virtual bool write(const uint8_t *buf, size_t len) = 0;

    virtual const std::string &describe() const = 0;
};

/// A path ("/dev/ttyACM0") opens a serial port; anything else is "host" or "host:port" over TCP (port 4403 by default).
std::unique_ptr<TransparentLink> makeTransparentLink(const std::string &endpoint);

#endif
