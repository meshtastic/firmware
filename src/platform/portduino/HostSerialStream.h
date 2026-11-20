#pragma once

#if defined(ARCH_PORTDUINO) && !defined(_WIN32)

#include <Arduino.h>
#include <string>

// A non-blocking Stream over a host tty, or over a PTY it creates when the path is "pty:<link>".
// Opens with O_NOCTTY so the daemon never takes the tty as its controlling terminal.
class HostSerialStream : public Stream
{
  public:
    ~HostSerialStream() { end(); }

    bool begin(const std::string &path, uint32_t baud);
    void end();
    // The tty a client opens: the device itself, or the PTY slave behind the link.
    const std::string &clientPath() const { return clientName; }
    bool isOpen() const { return fd >= 0; }

    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    // Free space in the kernel's output queue; 0 while the far end is not draining.
    int availableForWrite() override;
    using Print::write;

  private:
    bool openPty(const std::string &link);
    bool openTty(const std::string &path, uint32_t baud);

    int fd = -1;
    // Held open so the master reads EAGAIN, not EIO, while no client has the slave open.
    int ptySlaveFd = -1;
    int peeked = -1;
    std::string linkPath;
    std::string clientName;
};

#endif
