#include "platform/portduino/SerialHal.h"

// termios + poll: POSIX hosts only. The Windows build has neither, and RadioInterface never constructs it there.
#ifndef _WIN32

#include "mesh/SerialHalFraming.h"
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
using serialhal::FRAME_HEADER_LEN;
using serialhal::FRAME_MAGIC;
using serialhal::FRAME_START1;
using serialhal::FRAME_START2;

// A read error on a tty that means the device is gone rather than a transient condition.
bool isDisconnectErrno(int err)
{
    return err == EIO || err == ENXIO || err == ENODEV || err == EBADF;
}

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
#ifdef B460800 // macOS termios stops at B230400
    case 460800:
        return B460800;
#endif
#ifdef B921600
    case 921600:
        return B921600;
#endif
    default:
        return B115200;
    }
}
} // namespace

SerialHal::SerialHal(const std::string &devicePath, uint32_t baudRate, uint32_t opTimeoutMs)
    : RadioLibHal(serialhal::PIN_INPUT, serialhal::PIN_OUTPUT, serialhal::PIN_LOW, serialhal::PIN_HIGH, serialhal::EDGE_RISING,
                  serialhal::EDGE_FALLING),
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

    // Force raw mode to avoid line discipline byte mangling on binary frames.
    cfmakeraw(&tty);

    cfsetospeed(&tty, toTermiosBaud(baud));
    cfsetispeed(&tty, toTermiosBaud(baud));

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
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
    portLost = false;
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
    if (ret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) && !(pfd.revents & POLLIN)) {
        // The tty went away (USB unplug). poll() keeps returning immediately from here on, so flag it
        // rather than let the reader spin.
        portLost = true;
        return false;
    }
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
            if (isDisconnectErrno(errno)) {
                portLost = true;
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

uint16_t SerialHal::nextTransactionId()
{
    uint16_t id = txId.fetch_add(1);
    if (id == serialhal::INTERRUPT_TRANSACTION_ID) {
        // The counter wrapped onto the id reserved for interrupt events; a request carrying it would be
        // dispatched as an interrupt and never answered.
        id = txId.fetch_add(1);
    }
    return id;
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

    // Build frame with StreamAPI canonical framing: START1 MAGIC LEN_H LEN_L [payload]
    std::vector<uint8_t> frame;
    frame.resize(FRAME_HEADER_LEN + payloadLen);

    frame[0] = FRAME_START1;
    frame[1] = FRAME_MAGIC;
    frame[2] = (uint8_t)((payloadLen >> 8) & 0xFF); // LEN_H (big-endian)
    frame[3] = (uint8_t)(payloadLen & 0xFF);        // LEN_L
    memcpy(frame.data() + FRAME_HEADER_LEN, encoded, payloadLen);

    // Register before writing so the reader cannot drop a fast reply as unsolicited.
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        pendingResponses.erase(cmd.transaction_id);
        inFlight.insert(cmd.transaction_id);
    }

    bool written;
    {
        std::lock_guard<std::mutex> writeGuard(writeMutex);
        written = writeAll(frame.data(), frame.size());
    }

    meshtastic_SerialHalResponse got = meshtastic_SerialHalResponse_init_zero;
    bool arrived = false;
    {
        std::unique_lock<std::mutex> lock(stateMutex);
        if (written) {
            const auto timeout = std::chrono::milliseconds(timeoutMs);
            arrived = responseCv.wait_for(lock, timeout,
                                          [&]() { return portLost.load() || pendingResponses.count(cmd.transaction_id) > 0; });
            arrived = arrived && pendingResponses.count(cmd.transaction_id) > 0;
            if (arrived) {
                got = pendingResponses[cmd.transaction_id];
            }
        }
        // Whatever happened, this id no longer has a waiter: a late reply must not satisfy a future request.
        inFlight.erase(cmd.transaction_id);
        pendingResponses.erase(cmd.transaction_id);
    }

    if (!written) {
        setTransportError("serial write failed");
        return false;
    }
    if (!arrived) {
        setTransportError(portLost.load() ? "serial device disconnected" : "serial response timeout");
        LOG_WARN("SerialHal: no response for transaction_id %u, cmd type %u", cmd.transaction_id, cmd.type);
        return false;
    }

    if (got.result != meshtastic_SerialHalResponse_Result_OK) {
        // The device answered, so the link is healthy: report the failed operation without tearing down the radio.
        LOG_WARN("SerialHal: device rejected cmd type %u (result %u): %s", cmd.type, got.result, got.error);
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
    cmd.transaction_id = nextTransactionId();
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
    cmd.transaction_id = nextTransactionId();
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
    cmd.transaction_id = nextTransactionId();
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
    cmd.transaction_id = nextTransactionId();
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
    cmd.transaction_id = nextTransactionId();
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

    // RadioLib drives chip select through digitalWrite(), so CS stays asserted across chunks and a
    // transfer larger than one frame's data field can be split without changing what the radio sees.
    const size_t maxChunk = sizeof(meshtastic_SerialHalCommand{}.data.bytes);
    size_t off = 0;
    while (off < len) {
        const size_t chunk = (len - off) < maxChunk ? (len - off) : maxChunk;

        meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
        cmd.transaction_id = nextTransactionId();
        cmd.type = meshtastic_SerialHalCommand_Type_SPI_TRANSFER;
        cmd.data.size = chunk;
        if (out != nullptr) {
            memcpy(cmd.data.bytes, out + off, chunk);
        }

        meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
        const bool ok = sendRequest(cmd, &response);
        if (ok && response.data.size != chunk) {
            LOG_WARN("SerialHal: SPI transfer returned %u bytes, expected %u", (unsigned)response.data.size, (unsigned)chunk);
        }
        if (in != nullptr) {
            const size_t copyLen = ok ? (response.data.size < chunk ? response.data.size : chunk) : 0;
            memcpy(in + off, response.data.bytes, copyLen);
            memset(in + off + copyLen, 0, chunk - copyLen);
        }
        if (!ok) {
            if (in != nullptr) {
                memset(in + off, 0, len - off);
            }
            return;
        }
        off += chunk;
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

bool SerialHal::readFrame(std::vector<uint8_t> &payload)
{
    payload.clear();

    // Loop so that normal FromRadio frames (START1 START2 ...) emitted by the
    // device on the same serial port are drained and discarded rather than
    // causing the byte stream to desync.
    for (;;) {
        uint8_t hdr[FRAME_HEADER_LEN] = {0};
        for (;;) {
            ssize_t rc = ::read(fd, &hdr[0], 1);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (isDisconnectErrno(errno)) {
                    portLost = true;
                }
                return false;
            }
            if (rc == 0) {
                return false;
            }
            if (hdr[0] == FRAME_START1) {
                break;
            }
        }

        // A byte that fails the second-byte test can itself be the START1 of the real frame: re-test it.
        do {
            if (!readExact(hdr + 1, 1)) {
                return false;
            }
        } while (hdr[1] == FRAME_START1);

        if (hdr[1] != FRAME_MAGIC && hdr[1] != FRAME_START2) {
            continue; // not a frame start, resume the search for START1
        }
        if (!readExact(hdr + 2, FRAME_HEADER_LEN - 2)) {
            return false;
        }

        const uint16_t len = ((uint16_t)hdr[2] << 8) | (uint16_t)hdr[3];

        if (hdr[1] == FRAME_MAGIC) {
            // SerialHal response frame - this is what we want.
            if (len > meshtastic_SerialHalResponse_size) {
                return false;
            }
            payload.resize(len);
            if (len > 0 && !readExact(payload.data(), len)) {
                payload.clear();
                return false;
            }
            return true;
        }

        // Normal FromRadio frame emitted by the device - drain and discard
        // its payload so we stay in sync, then loop to find a SerialHal frame.
        if (len > 0) {
            std::vector<uint8_t> discard(len);
            if (!readExact(discard.data(), len)) {
                return false;
            }
        }
    }
}

void SerialHal::readerLoop()
{
    readerRunning = true;
    std::vector<uint8_t> payload;
    while (!readerStopRequested.load()) {
        if (fd < 0) {
            break;
        }

        if (!waitForReadable(100) || !readFrame(payload)) {
            if (portLost.load()) {
                setTransportError("serial device disconnected");
                {
                    // Take the lock so a waiter between its predicate check and its sleep cannot miss the wakeup.
                    std::lock_guard<std::mutex> lock(stateMutex);
                }
                responseCv.notify_all(); // fail any waiter now instead of at its timeout
                break;
            }
            continue;
        }

        meshtastic_SerialHalResponse resp = meshtastic_SerialHalResponse_init_zero;
        // An empty payload is valid: it is the all-defaults message, i.e. an interrupt event for pin 0.
        if (!pb_decode_from_bytes(payload.data(), payload.size(), &meshtastic_SerialHalResponse_msg, &resp)) {
            continue;
        }

        if (resp.transaction_id == serialhal::INTERRUPT_TRANSACTION_ID) {
            if (resp.result != meshtastic_SerialHalResponse_Result_OK) {
                LOG_WARN("SerialHal: device reported an error without a transaction: %s", resp.error);
                continue;
            }
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
            if (inFlight.count(resp.transaction_id) == 0) {
                continue; // late reply to a request that already timed out
            }
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
#endif // !_WIN32
