#pragma once

#ifdef SENSECAP_INDICATOR

#include "../generated/meshtastic/interdevice.pb.h"
#include <Wire.h>

/**
 * TwoWire implementation that tunnels bus traffic to the RP2040 over the
 * interdevice serial link, making the sensors attached to the secondary MCU
 * usable by the regular sensor drivers of the main firmware.
 *
 * beginTransmission()/write() buffer an outgoing write. The buffered write is
 * executed as a single serial round trip on endTransmission(true). A write
 * followed by endTransmission(false) and requestFrom() is combined into one
 * write+read transaction with repeated start, matching driver access
 * patterns.
 *
 * Constructed on bus number 0: TwoWire::begin() is final and cannot be
 * intercepted, but it returns without touching hardware when the bus is
 * already initialized, which is always true for bus 0 (local touch panel).
 *
 * Thread safety: the bus is shared between the main loop (sensor drivers)
 * and the UI task (keyboard scanner), and TwoWire has no transaction
 * bracket a lock could safely span - drivers drain the read buffer with
 * available()/read() long after requestFrom() returned. Each calling task
 * therefore gets its own staging and read buffers; the tunneled round trip
 * itself is serialized by the link. Tasks beyond MAX_TASKS share the last
 * context, which is only correct if they do not run concurrently.
 */
class I2CProxy : public TwoWire
{
  public:
    I2CProxy() : TwoWire(0) {}

    bool end() override { return true; }
    bool setClock(uint32_t) override { return true; }

    void beginTransmission(uint8_t address) override;
    uint8_t endTransmission(bool stopBit) override;
    uint8_t endTransmission() override { return endTransmission(true); }

    size_t requestFrom(uint8_t address, size_t len, bool stopBit) override;
    size_t requestFrom(uint8_t address, size_t len) override { return requestFrom(address, len, true); }

    void onReceive(const std::function<void(int)> &) override {}
    void onRequest(const std::function<void()> &) override {}

    size_t write(uint8_t data) override;
    size_t write(const uint8_t *data, size_t len) override;
    int available() override;
    int read() override;
    int peek() override;
    void flush() override {}

  private:
    // Derived from the generated protobuf limits (interdevice.options)
    static const size_t MAX_WRITE = sizeof(meshtastic_I2CTransaction{}.write_data.bytes);
    static const size_t MAX_READ = sizeof(meshtastic_I2CResult{}.read_data.bytes);

    // main loop, UI task, and one spare
    static const size_t MAX_TASKS = 3;

    struct Context {
        TaskHandle_t task = nullptr;
        uint8_t txAddress = 0;
        uint8_t txBuf[MAX_WRITE];
        size_t txLen = 0;
        bool txPending = false;
        uint8_t rxBuf[MAX_READ];
        size_t rxLen = 0;
        size_t rxPos = 0;
    };
    Context _ctx[MAX_TASKS];

    // per-task transaction state, claimed on first use
    Context &ctx();
    uint8_t transact(Context &c, uint8_t address, size_t rlen);
};

extern I2CProxy *i2cProxy;

#endif // SENSECAP_INDICATOR
