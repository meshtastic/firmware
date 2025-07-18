#pragma once

// For size_t/int32_t types on some platforms.
#include <cstdint>
// For size_t
#include <cstddef>

class BRCAddress
{
  public:
    BRCAddress(int32_t lat, int32_t lon);

    int radial(char *buf, size_t len);
    int annular(char *buf, size_t len, bool noUnit);
    int full(char *buf, size_t len);
    int compact(char *buf, size_t len);

  private:
    float bearing;
    float distance;
};
