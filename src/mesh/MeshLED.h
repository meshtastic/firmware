#pragma once

#include <memory>

/**
 * Interface for a "mesh activity" LED that lights while the LoRa radio is transmitting.
 * The default implementation is a no-op; device variants may replace the global instance
 * with a concrete subclass.
 */
class MeshLED
{
  public:
    virtual ~MeshLED() = default;
    virtual void init() {}
    virtual void on() {}
    virtual void off() {}
};

extern std::shared_ptr<MeshLED> meshLED;
