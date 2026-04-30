#include "platform/portduino/SerialHal.h"

#include "mesh/mesh-pb-constants.h"
#include "platform/portduino/PortduinoGlue.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace
{
constexpr uint8_t START1 = 0x94;
constexpr uint8_t SERIALHAL_MAGIC = 0xA5;
constexpr size_t HEADER_SIZE = 4; // START1 + SERIALHAL_MAGIC + LEN_H + LEN_L
constexpr uint8_t START2 = 0xC3;  // second byte of a normal FromRadio frame

speed_t toTermiosBaud(uint32_t baud)
{
    switch (baud) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    case 460800:
        return B460800;
    case 921600:
        return B921600;
    default:
        return B115200;
    }
}
} // namespace

SerialHal::SerialHal(const std::string &devicePath, uint32_t baudRate, uint32_t opTimeoutMs)
    : RadioLibHal(SERIAL_PI_INPUT, SERIAL_PI_OUTPUT, SERIAL_PI_LOW, SERIAL_PI_HIGH, SERIAL_PI_RISING, SERIAL_PI_FALLING),
      device(devicePath), baud(baudRate), timeoutMs(opTimeoutMs)
{
    if (!openPort()) {
        setTransportError("unable to open serial device");
    }
}

SerialHal::~SerialHal()
{
    closePort();
}

bool SerialHal::openPort()
{
    closePort();
    fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        return false;
    }

    termios tty = {};
    if (tcgetattr(fd, &tty) != 0) {
        closePort();
        return false;
    }

    cfsetospeed(&tty, toTermiosBaud(baud));
    cfsetispeed(&tty, toTermiosBaud(baud));

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~(IGNBRK | IXON | IXOFF | IXANY);
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        closePort();
        return false;
    }

    tcflush(fd, TCIOFLUSH);
    inError = false;
    startReaderThread();
    return true;
}

void SerialHal::closePort()
{
    stopReaderThread();
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void SerialHal::setTransportError(const char *msg)
{
    if (!inError.load() || !hasWarned) {
        LOG_ERROR("SerialHal: %s (%s)", msg, device.c_str());
    }
    inError = true;
    hasWarned = true;
    portduino_status.LoRa_in_error = true;
}

bool SerialHal::waitForReadable(int timeout)
{
    if (fd < 0) {
        return false;
    }
    pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout);
    return ret > 0 && (pfd.revents & POLLIN);
}

bool SerialHal::writeAll(const uint8_t *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t rc = ::write(fd, data + off, len - off);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        off += (size_t)rc;
    }
    return true;
}

bool SerialHal::readExact(uint8_t *data, size_t len)
{
    size_t off = 0;
    auto start = std::chrono::steady_clock::now();
    while (off < len) {
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        int remaining = (int)timeoutMs - elapsed;
        if (remaining <= 0 || !waitForReadable(remaining)) {
            return false;
        }
        ssize_t rc = ::read(fd, data + off, len - off);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rc == 0) {
            return false;
        }
        off += (size_t)rc;
    }
    return true;
}

uint16_t SerialHal::crc16(const uint8_t *data, size_t len) const
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= ((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

bool SerialHal::sendRequest(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse *response)
{
    if (fd < 0 && !openPort()) {
        setTransportError("serial open failed");
        return false;
    }

    uint8_t encoded[meshtastic_SerialHalCommand_size] = {0};
    const size_t payloadLen =
        pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_SerialHalCommand_msg, static_cast<const void *>(&cmd));
    if (payloadLen == 0 || payloadLen > 0xFFFF) {
        setTransportError("serial command encode failed");
        return false;
    }

    // Build frame with StreamAPI canonical framing: START1 SERIALHAL_MAGIC LEN_H LEN_L [payload]
    std::vector<uint8_t> frame;
    frame.resize(HEADER_SIZE + payloadLen);

    frame[0] = START1;
    frame[1] = SERIALHAL_MAGIC;
    frame[2] = (uint8_t)((payloadLen >> 8) & 0xFF); // LEN_H (big-endian)
    frame[3] = (uint8_t)(payloadLen & 0xFF);        // LEN_L
    memcpy(frame.data() + HEADER_SIZE, encoded, payloadLen);

    {
        std::lock_guard<std::mutex> writeGuard(writeMutex);
        if (!writeAll(frame.data(), frame.size())) {
            setTransportError("serial write failed");
            return false;
        }
    }

    meshtastic_SerialHalResponse got = meshtastic_SerialHalResponse_init_zero;
    {
        std::unique_lock<std::mutex> lock(stateMutex);
        const auto timeout = std::chrono::milliseconds(timeoutMs);
        const bool arrived = responseCv.wait_for(lock, timeout, [&]() { return pendingResponses.count(cmd.transaction_id) > 0; });
        if (!arrived) {
            setTransportError("serial response timeout");
            LOG_WARN("SerialHal: response timeout for transaction_id %u, cmd type %u", cmd.transaction_id, cmd.type);
            return false;
        }

        got = pendingResponses[cmd.transaction_id];
        pendingResponses.erase(cmd.transaction_id);
    }

    if (got.result != meshtastic_SerialHalResponse_Result_OK) {
        setTransportError("serial response reported error");
        LOG_WARN("SerialHal: response error: %s, %u, %u", got.error, cmd.type, cmd.data.size);

        return false;
    }

    if (response != nullptr) {
        *response = got;
    }

    inError = false;
    hasWarned = false;
    return true;
}

void SerialHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (checkError() || pin == RADIOLIB_NC) {
        return;
    }

    meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
    cmd.transaction_id = txId.fetch_add(1);
    cmd.type = meshtastic_SerialHalCommand_Type_PIN_MODE;
    cmd.pin = pin;
    cmd.mode = mode;

    meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
    sendRequest(cmd, &response);
}

void SerialHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (checkError() || pin == RADIOLIB_NC) {
        return;
    }

    meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
    cmd.transaction_id = txId.fetch_add(1);
    cmd.type = meshtastic_SerialHalCommand_Type_DIGITAL_WRITE;
    cmd.pin = pin;
    cmd.value = value;

    meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
    sendRequest(cmd, &response);
}

uint32_t SerialHal::digitalRead(uint32_t pin)
{
    if (checkError() || pin == RADIOLIB_NC) {
        return 0;
    }

    meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
    cmd.transaction_id = txId.fetch_add(1);
    cmd.type = meshtastic_SerialHalCommand_Type_DIGITAL_READ;
    cmd.pin = pin;

    meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
    if (!sendRequest(cmd, &response)) {
        return 0;
    }
    return response.value;
}

void SerialHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode)
{
    if (checkError() || interruptNum == RADIOLIB_NC) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        interruptCallbacks[interruptNum] = interruptCb;
    }

    meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
    cmd.transaction_id = txId.fetch_add(1);
    cmd.type = meshtastic_SerialHalCommand_Type_ATTACH_INTERRUPT;
    cmd.pin = interruptNum;
    cmd.mode = mode;

    meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
    sendRequest(cmd, &response);
}

void SerialHal::detachInterrupt(uint32_t interruptNum)
{
    if (checkError() || interruptNum == RADIOLIB_NC) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        interruptCallbacks.erase(interruptNum);
    }

    meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
    cmd.transaction_id = txId.fetch_add(1);
    cmd.type = meshtastic_SerialHalCommand_Type_DETACH_INTERRUPT;
    cmd.pin = interruptNum;

    meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
    sendRequest(cmd, &response);
}

void SerialHal::delay(unsigned long ms)
{
    delayMicroseconds(ms * 1000);
}

void SerialHal::delayMicroseconds(unsigned long us)
{
    if (us == 0) {
        sched_yield();
        return;
    }
    usleep(us);
}

void SerialHal::yield()
{
    sched_yield();
}

unsigned long SerialHal::millis()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000ULL);
}

unsigned long SerialHal::micros()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (tv.tv_sec * 1000000ULL) + tv.tv_usec;
}

long SerialHal::pulseIn(uint32_t pin, uint32_t state, unsigned long timeout)
{
    (void)pin;
    (void)state;
    (void)timeout;
    LOG_WARN("SerialHal pulseIn is not supported");
    return 0;
}

void SerialHal::spiTransfer(uint8_t *out, size_t len, uint8_t *in)
{
    if (checkError()) {
        return;
    }

    if (len == 0) {
        return;
    }

    meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
    cmd.transaction_id = txId.fetch_add(1);
    cmd.type = meshtastic_SerialHalCommand_Type_SPI_TRANSFER;

    const size_t maxTx = sizeof(cmd.data.bytes);
    const size_t txLen = len < maxTx ? len : maxTx;
    cmd.data.size = txLen;
    if (out != nullptr) {
        memcpy(cmd.data.bytes, out, txLen);
    } else {
        memset(cmd.data.bytes, 0, txLen);
    }

    meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
    if (!sendRequest(cmd, &response)) {
        return;
    }

    if (in != nullptr) {
        size_t copyLen = response.data.size < len ? response.data.size : len;
        memcpy(in, response.data.bytes, copyLen);
        if (copyLen < len) {
            memset(in + copyLen, 0, len - copyLen);
        }
    }
}

bool SerialHal::checkError()
{
    if (inError.load()) {
        if (!hasWarned) {
            LOG_ERROR("SerialHal in_error detected");
            hasWarned = true;
        }
        portduino_status.LoRa_in_error = true;
        return true;
    }
    hasWarned = false;
    return false;
}

bool SerialHal::readFrame(std::vector<uint8_t> &payload, int firstByteTimeoutMs)
{
    payload.clear();

    // Loop so that normal FromRadio frames (START1 START2 ...) emitted by the
    // device on the same serial port are drained and discarded rather than
    // causing the byte stream to desync.
    for (;;) {
        uint8_t hdr[HEADER_SIZE] = {0};
        for (;;) {

            ssize_t rc = ::read(fd, &hdr[0], 1);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (rc == 0) {
                return false;
            }
            if (hdr[0] == START1) {
                break;
            }
        }

        if (!readExact(hdr + 1, HEADER_SIZE - 1)) {
            return false;
        }

        const uint16_t len = ((uint16_t)hdr[2] << 8) | (uint16_t)hdr[3];

        if (hdr[1] == SERIALHAL_MAGIC) {
            // SerialHal response frame — this is what we want.
            if (len > meshtastic_SerialHalResponse_size) {
                return false;
            }
            payload.resize(len);
            if (len > 0 && !readExact(payload.data(), len)) {
                payload.clear();
                return false;
            }
            return true;
        } else if (hdr[1] == START2) {
            // Normal FromRadio frame emitted by the device — drain and discard
            // its payload so we stay in sync, then loop to find a SerialHal frame.
            if (len > 0) {
                std::vector<uint8_t> discard(len);
                if (!readExact(discard.data(), len)) {
                    return false;
                }
            }
            // continue looping, look for next frame
        } else {
            // Unknown second byte after START1 — restart search for framing.
            continue;
        }
    }
}

void SerialHal::readerLoop()
{
    readerRunning = true;
    while (!readerStopRequested.load()) {
        if (fd < 0) {
            break;
        }

        if (!waitForReadable(100)) {
            continue;
        }

        std::vector<uint8_t> payload;
        if (!readFrame(payload, 40)) {
            continue;
        }

        meshtastic_SerialHalResponse resp = meshtastic_SerialHalResponse_init_zero;
        if (payload.empty() || !pb_decode_from_bytes(payload.data(), payload.size(), &meshtastic_SerialHalResponse_msg, &resp)) {
            continue;
        }

        if (resp.transaction_id == 0) {
            LOG_WARN("SerialHal: received unsolicited interrupt event: pin=%u", resp.value);
            // transaction_id 0 is reserved for unsolicited interrupt events.
            // The device reports the triggered pin in resp.value instead of
            // matching one of the synchronous request/response transactions.
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (interruptCallbacks.count(resp.value) > 0) {
                    pendingInterruptPins.push_back(resp.value);
                }
            }
            interruptCv.notify_one();
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            pendingResponses[resp.transaction_id] = resp;
        }
        responseCv.notify_all();
    }
    readerRunning = false;
}

void SerialHal::interruptDispatchLoop()
{
    interruptDispatcherRunning = true;
    while (!readerStopRequested.load()) {
        uint32_t pin = 0;
        void (*cb)(void) = nullptr;

        {
            std::unique_lock<std::mutex> lock(stateMutex);
            interruptCv.wait(lock, [&]() { return readerStopRequested.load() || !pendingInterruptPins.empty(); });
            if (readerStopRequested.load()) {
                break;
            }

            pin = pendingInterruptPins.front();
            pendingInterruptPins.pop_front();

            auto it = interruptCallbacks.find(pin);
            if (it != interruptCallbacks.end()) {
                cb = it->second;
            }
        }

        if (cb != nullptr) {
            cb();
        }
    }
    interruptDispatcherRunning = false;
}

void SerialHal::startReaderThread()
{
    stopReaderThread();
    readerStopRequested = false;
    readerThread = std::thread(&SerialHal::readerLoop, this);
    interruptThread = std::thread(&SerialHal::interruptDispatchLoop, this);
}

void SerialHal::stopReaderThread()
{
    readerStopRequested = true;
    interruptCv.notify_all();
    if (readerThread.joinable()) {
        readerThread.join();
    }
    if (interruptThread.joinable()) {
        interruptThread.join();
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    pendingInterruptPins.clear();
}
