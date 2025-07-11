#pragma once

class BRCAddress
{
  public:
    BRCAddress(int32_t lat, int32_t lon);

    int radial(char *buf, size_t len);
    int annular(char *buf, size_t len);
    int full(char *buf, size_t len);

  private:
    float bearing;
    float distance;
};
