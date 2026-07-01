#ifdef ARCH_PORTDUINO

#include "GpsdSerial.h"
#include "configuration.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace arduino
{

// The single global instance used by PortduinoGlue and GPS::createGps().
GpsdSerial gpsdSerial;

void GpsdSerial::setAddress(const std::string &host, int port)
{
    _host = host;
    _port = port;
}

bool GpsdSerial::connectToGpsd()
{
    if (_host.empty())
        return false;

    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string portStr = std::to_string(_port);

    if (getaddrinfo(_host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        LOG_WARN("gpsdSerial: could not resolve %s", _host.c_str());
        return false;
    }

    // Try every address returned by getaddrinfo (e.g. ::1 before 127.0.0.1).
    int fd = -1;
    for (struct addrinfo *rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // connected
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        LOG_WARN("gpsdSerial: connect to %s:%d failed", _host.c_str(), _port);
        return false;
    }

    // Switch to non-blocking so available()/read() never stall the GPS thread.
    fcntl(fd, F_SETFL, O_NONBLOCK);

    // Ask gpsd to stream raw NMEA sentences.
    const char watchCmd[] = "?WATCH={\"enable\":true,\"nmea\":true}\n";
    ::write(fd, watchCmd, sizeof(watchCmd) - 1);

    _sockfd = fd;
    _rxBuf.clear();
    LOG_INFO("gpsdSerial: connected to %s:%d", _host.c_str(), _port);
    return true;
}

void GpsdSerial::begin(unsigned long /*baud*/, uint16_t /*config*/)
{
    if (_sockfd >= 0)
        end();
    _lastConnectAttemptMs = 0; // force immediate connect on begin()
    connectToGpsd();
}

void GpsdSerial::end()
{
    if (_sockfd >= 0) {
        ::close(_sockfd);
        _sockfd = -1;
    }
    _rxBuf.clear();
}

void GpsdSerial::fillBuffer()
{
    if (_sockfd < 0)
        return;
    // Guard: if the buffer is already full, skip recv entirely so n is always
    // assigned before the post-loop disconnect check below.
    if (_rxBuf.size() >= RX_BUF_MAX)
        return;

    uint8_t tmp[256];
    ssize_t n;
    while (_rxBuf.size() < RX_BUF_MAX && (n = recv(_sockfd, tmp, sizeof(tmp), MSG_DONTWAIT)) > 0) {
        size_t space = RX_BUF_MAX - _rxBuf.size();
        size_t toCopy = (static_cast<size_t>(n) < space) ? static_cast<size_t>(n) : space;
        for (size_t i = 0; i < toCopy; i++)
            _rxBuf.push_back(tmp[i]);
    }

    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        // gpsd closed the connection or a real error occurred.
        LOG_WARN("gpsdSerial: disconnected, will retry");
        ::close(_sockfd);
        _sockfd = -1;
        _rxBuf.clear();
    }
}

int GpsdSerial::available()
{
    if (_sockfd < 0) {
        // Throttle reconnect attempts to avoid log spam and repeated DNS lookups
        // when gpsd is unreachable.
        uint32_t now = millis();
        if (now - _lastConnectAttemptMs >= RECONNECT_INTERVAL_MS) {
            _lastConnectAttemptMs = now;
            connectToGpsd();
        }
    }
    fillBuffer();
    return static_cast<int>(_rxBuf.size());
}

int GpsdSerial::peek()
{
    if (_rxBuf.empty())
        return -1;
    return static_cast<int>(_rxBuf.front());
}

int GpsdSerial::read()
{
    if (_rxBuf.empty())
        return -1;
    uint8_t c = _rxBuf.front();
    _rxBuf.pop_front();
    return static_cast<int>(c);
}

} // namespace arduino

#endif // ARCH_PORTDUINO
