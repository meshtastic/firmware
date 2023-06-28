#pragma once

class VoltageSensor
{
  public:
    virtual uint16_t getBusVoltageMv() = 0;
};