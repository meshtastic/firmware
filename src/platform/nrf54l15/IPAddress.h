// IPAddress.h — stub for nRF54L15/Zephyr
// MQTT.cpp includes this for IPv4 address representation.
// Phase 2: compile-only stub.
#pragma once
#include <stdint.h>

class IPAddress {
  public:
    IPAddress() : _addr(0) {}
    explicit IPAddress(uint32_t addr) : _addr(addr) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : _addr(((uint32_t)a) | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24)) {}

    uint8_t operator[](int i) const { return reinterpret_cast<const uint8_t*>(&_addr)[i]; }
    operator uint32_t() const { return _addr; }
    bool operator==(const IPAddress &o) const { return _addr == o._addr; }
    bool operator!=(const IPAddress &o) const { return _addr != o._addr; }

    bool fromString(const char *addr) {
        unsigned a, b, c, d;
        if (sscanf(addr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 &&
            a <= 255 && b <= 255 && c <= 255 && d <= 255) {
            _addr = a | (b << 8) | (c << 16) | (d << 24);
            return true;
        }
        return false;
    }

  private:
    uint32_t _addr;
};
