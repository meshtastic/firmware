#ifndef PI_HAL_SERIAL_H
#define PI_HAL_SERIAL_H

#include <RadioLib.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mesh/generated/meshtastic/serial_hal.pb.h>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define SERIAL_PI_INPUT (0)
#define SERIAL_PI_OUTPUT (1)
#define SERIAL_PI_LOW (0)
#define SERIAL_PI_HIGH (1)
#define SERIAL_PI_RISING (1)
#define SERIAL_PI_FALLING (2)

class SerialHal : public RadioLibHal
{
  public:
    explicit SerialHal(const std::string &device, uint32_t baud = 115200, uint32_t timeoutMs = 500);
    ~SerialHal() override;

    void init() override {}
    void term() override {}

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;

    void delay(unsigned long ms) override;
    void delayMicroseconds(unsigned long us) override;
    void yield() override;

    unsigned long millis() override;
    unsigned long micros() override;

    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override;

    void spiBegin() override {}
    void spiBeginTransaction() override {}
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override;
    void spiEndTransaction() override {}
    void spiEnd() override {}

    bool checkError();

  private:
    bool openPort();
    void closePort();
    bool sendRequest(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse *response);
    bool writeAll(const uint8_t *data, size_t len);
    bool readExact(uint8_t *data, size_t len);
    bool waitForReadable(int timeoutMs);
    bool readFrame(std::vector<uint8_t> &payload, int firstByteTimeoutMs);
    void readerLoop();
    void interruptDispatchLoop();
    void startReaderThread();
    void stopReaderThread();

    uint16_t crc16(const uint8_t *data, size_t len) const;
    void setTransportError(const char *msg);

    std::string device;
    uint32_t baud;
    uint32_t timeoutMs;
    int fd = -1;
    bool hasWarned = false;
    std::atomic<bool> inError{false};
    std::atomic<uint16_t> txId{1};

    std::mutex fdMutex;
    std::mutex writeMutex;
    std::mutex stateMutex;
    std::condition_variable responseCv;

    std::thread readerThread;
    std::thread interruptThread;
    std::atomic<bool> readerStopRequested{false};
    std::atomic<bool> readerRunning{false};
    std::atomic<bool> interruptDispatcherRunning{false};

    std::condition_variable interruptCv;
    std::deque<uint32_t> pendingInterruptPins;

    std::unordered_map<uint32_t, void (*)(void)> interruptCallbacks;
    std::unordered_map<uint16_t, meshtastic_SerialHalResponse> pendingResponses;
};

#endif
