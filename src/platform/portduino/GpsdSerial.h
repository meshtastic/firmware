#pragma once
#ifdef ARCH_PORTDUINO

#include "HardwareSerial.h"
#include <deque>
#include <string>

namespace arduino
{

// Presents a gpsd TCP NMEA stream as a HardwareSerial port.
// Connect gpsd at the configured host:port, send a WATCH request to enable
// raw NMEA output, then expose the incoming bytes through the standard
// available()/read() interface so the GPS class can feed them to TinyGPS++.
class GpsdSerial : public HardwareSerial
{
    static constexpr size_t RX_BUF_MAX = 4096;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

    std::string _host;
    int _port = 2947;
    int _sockfd = -1;
    std::deque<uint8_t> _rxBuf;
    uint32_t _lastConnectAttemptMs = 0;

    bool connectToGpsd();
    void fillBuffer();

  public:
    void setAddress(const std::string &host, int port = 2947);

    void begin(unsigned long baud) override { begin(baud, 0); }
    void begin(unsigned long baud, uint16_t config) override;
    void end() override;

    int available() override;
    int peek() override;
    int read() override;
    void flush() override {}
    size_t write(uint8_t) override { return 1; } // gpsd controls the hardware
    using Print::write;
    operator bool() override { return _sockfd >= 0; }
};

extern GpsdSerial gpsdSerial;

} // namespace arduino

#endif // ARCH_PORTDUINO
