#ifdef ARCH_PORTDUINO

#include "GpsdSerial.h"
#include "configuration.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace arduino {

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

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        LOG_WARN("gpsdSerial: socket() failed");
        return false;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        ::close(fd);
        LOG_WARN("gpsdSerial: connect to %s:%d failed", _host.c_str(), _port);
        return false;
    }
    freeaddrinfo(res);

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

    uint8_t tmp[256];
    ssize_t n;
    while ((n = recv(_sockfd, tmp, sizeof(tmp), MSG_DONTWAIT)) > 0) {
        for (ssize_t i = 0; i < n; i++)
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
    if (_sockfd < 0)
        connectToGpsd();
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
