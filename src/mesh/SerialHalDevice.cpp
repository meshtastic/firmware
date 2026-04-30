#include "mesh/SerialHalDevice.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "concurrency/Periodic.h"
#include "configuration.h"
#include "mesh/StreamAPI.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include <Arduino.h>
#include <SPI.h>
#include <cstring>
#include <mutex>
#include <stdint.h>

#if defined(ARCH_ESP32) && defined(HW_SPI1_DEVICE)
extern SPIClass SPI1;
#endif

namespace
{
constexpr uint32_t SERIAL_PI_RISING = 1;
constexpr uint32_t SERIAL_PI_FALLING = 2;
constexpr uint32_t SERIAL_PI_INPUT = 0;
constexpr uint32_t SERIAL_PI_OUTPUT = 1;
constexpr size_t MAX_INTERRUPT_SLOTS = 8;
constexpr int32_t INTERRUPT_POLL_MS = 5;

struct InterruptSlot {
    bool used = false;
    uint32_t pin = 0;
    uint32_t mode = 0;
    volatile bool pending = false;
};

std::mutex interruptMutex;
InterruptSlot interruptSlots[MAX_INTERRUPT_SLOTS];
StreamAPI *interruptStreamApi = nullptr;
concurrency::Periodic *interruptEmitter = nullptr;

int findSlotByPinLocked(uint32_t pin)
{
    for (size_t i = 0; i < MAX_INTERRUPT_SLOTS; ++i) {
        if (interruptSlots[i].used && interruptSlots[i].pin == pin) {
            return (int)i;
        }
    }
    return -1;
}

int allocateSlotLocked()
{
    for (size_t i = 0; i < MAX_INTERRUPT_SLOTS; ++i) {
        if (!interruptSlots[i].used) {
            return (int)i;
        }
    }
    return -1;
}

#ifdef ARCH_PORTDUINO
PinStatus toInterruptMode(uint32_t serialMode)
{
    if (serialMode == SERIAL_PI_RISING) {
        return PinStatus::RISING;
    }
    if (serialMode == SERIAL_PI_FALLING) {
        return PinStatus::FALLING;
    }
    return PinStatus::CHANGE;
}
#else
int toInterruptMode(uint32_t serialMode)
{
    if (serialMode == SERIAL_PI_RISING) {
        return RISING;
    }
    if (serialMode == SERIAL_PI_FALLING) {
        return FALLING;
    }
    return CHANGE;
}
#endif

int32_t pumpInterruptEvents();

void ensureInterruptEmitter()
{
    if (!interruptEmitter) {
        interruptEmitter = new concurrency::Periodic("SerialHalIrqEmitter", pumpInterruptEvents);
    }
}

void emitInterruptEvent(uint32_t pin, StreamAPI *streamApi)
{
    if (streamApi == nullptr) {
        return;
    }

    meshtastic_SerialHalResponse event = meshtastic_SerialHalResponse_init_zero;
    event.transaction_id = 0; // asynchronous interrupt notification
    event.result = meshtastic_SerialHalResponse_Result_OK;
    event.value = pin; // host-side SerialHal treats value as interrupt pin
    SerialHalDevice::emitResponse(event, streamApi);
}

void markPendingBySlot(size_t slot)
{
    if (slot < MAX_INTERRUPT_SLOTS && interruptSlots[slot].used) {
        interruptSlots[slot].pending = true;
    }
}

void isr0()
{
    markPendingBySlot(0);
}
void isr1()
{
    markPendingBySlot(1);
}
void isr2()
{
    markPendingBySlot(2);
}
void isr3()
{
    markPendingBySlot(3);
}
void isr4()
{
    markPendingBySlot(4);
}
void isr5()
{
    markPendingBySlot(5);
}
void isr6()
{
    markPendingBySlot(6);
}
void isr7()
{
    markPendingBySlot(7);
}

void (*const isrTable[MAX_INTERRUPT_SLOTS])() = {isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7};

int32_t pumpInterruptEvents()
{
    uint32_t toEmit[MAX_INTERRUPT_SLOTS] = {0};
    size_t emitCount = 0;
    StreamAPI *streamApi = nullptr;

    {
        std::lock_guard<std::mutex> lock(interruptMutex);
        streamApi = interruptStreamApi;
        for (size_t i = 0; i < MAX_INTERRUPT_SLOTS; ++i) {
            if (interruptSlots[i].used && interruptSlots[i].pending) {
                interruptSlots[i].pending = false;
                toEmit[emitCount++] = interruptSlots[i].pin;
            }
        }
    }

    for (size_t i = 0; i < emitCount; ++i) {
        emitInterruptEvent(toEmit[i], streamApi);
    }

    return INTERRUPT_POLL_MS;
}
} // namespace

// Helper to safely set response result
static inline void setResponseError(meshtastic_SerialHalResponse &response, meshtastic_SerialHalResponse_Result result,
                                    const char *error = nullptr)
{
    response.result = result;
    if (error != nullptr) {
        snprintf(response.error, sizeof(response.error), "%s", error);
    }
}

void SerialHalDevice::handleCommand(const uint8_t *buf, size_t len, StreamAPI *streamApi)
{
    if (buf == nullptr || streamApi == nullptr) {
        return;
    }

    // Validate role - SerialHal commands only handled when config.lora.serial_hal_only
    /*if (!config.lora.serial_hal_only) {
        meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
        response.result = meshtastic_SerialHalResponse_Result_UNSUPPORTED;
        snprintf(response.error, sizeof(response.error), "SerialHal not enabled for this role");
        emitResponse(response, streamApi);
        return;
    }*/

    // Decode the command
    meshtastic_SerialHalCommand cmd = meshtastic_SerialHalCommand_init_zero;
    if (!pb_decode_from_bytes(buf, len, &meshtastic_SerialHalCommand_msg, &cmd)) {
        meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
        response.result = meshtastic_SerialHalResponse_Result_BAD_REQUEST;
        snprintf(response.error, sizeof(response.error), "Failed to decode SerialHalCommand");
        emitResponse(response, streamApi);
        return;
    }

    // Initialize response with matching transaction_id
    meshtastic_SerialHalResponse response = meshtastic_SerialHalResponse_init_zero;
    response.transaction_id = cmd.transaction_id;
    response.result = meshtastic_SerialHalResponse_Result_OK;

    // Dispatch to operation handler
    switch (cmd.type) {
    case meshtastic_SerialHalCommand_Type_PIN_MODE:
        handlePinMode(cmd, response);
        break;
    case meshtastic_SerialHalCommand_Type_DIGITAL_WRITE:
        handleDigitalWrite(cmd, response);
        break;
    case meshtastic_SerialHalCommand_Type_DIGITAL_READ:
        handleDigitalRead(cmd, response);
        break;
    case meshtastic_SerialHalCommand_Type_ATTACH_INTERRUPT:
        handleAttachInterrupt(cmd, response);
        break;
    case meshtastic_SerialHalCommand_Type_DETACH_INTERRUPT:
        handleDetachInterrupt(cmd, response);
        break;
    case meshtastic_SerialHalCommand_Type_SPI_TRANSFER:
        handleSpiTransfer(cmd, response);
        break;
    case meshtastic_SerialHalCommand_Type_NOOP:
        // NOOP: just return OK
        break;
    default:
        response.result = meshtastic_SerialHalResponse_Result_UNSUPPORTED;
        snprintf(response.error, sizeof(response.error), "Unknown SerialHal operation type");
        break;
    }

    emitResponse(response, streamApi);
}

void SerialHalDevice::handlePinMode(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response)
{
    // LOG_DEBUG("SerialHalDevice: pinMode pin=%u mode=%u", cmd.pin, cmd.mode);
    if (cmd.mode == SERIAL_PI_INPUT) {
        pinMode((int)cmd.pin, INPUT);
    } else if (cmd.mode == SERIAL_PI_OUTPUT) {
        pinMode((int)cmd.pin, OUTPUT);
    } else {
        setResponseError(response, meshtastic_SerialHalResponse_Result_BAD_REQUEST, "Unsupported pin mode");
    }
}

void SerialHalDevice::handleDigitalWrite(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response)
{
    // LOG_DEBUG("SerialHalDevice: digitalWrite pin=%u value=%u", cmd.pin, cmd.value);
    digitalWrite((int)cmd.pin, cmd.value ? HIGH : LOW);
}

void SerialHalDevice::handleDigitalRead(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response)
{
    // LOG_DEBUG("SerialHalDevice: digitalRead pin=%u", cmd.pin);
    response.value = (uint32_t)digitalRead((int)cmd.pin);
}

void SerialHalDevice::handleAttachInterrupt(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response)
{
    // LOG_DEBUG("SerialHalDevice: attachInterrupt pin=%u mode=%u", cmd.pin, cmd.mode);

    ensureInterruptEmitter();

    int slot = -1;
    {
        std::lock_guard<std::mutex> lock(interruptMutex);
        slot = findSlotByPinLocked(cmd.pin);
        if (slot < 0) {
            slot = allocateSlotLocked();
        }

        if (slot >= 0) {
            interruptSlots[slot].used = true;
            interruptSlots[slot].pin = cmd.pin;
            interruptSlots[slot].mode = cmd.mode;
            interruptSlots[slot].pending = false;
        }
    }

    if (slot < 0) {
        setResponseError(response, meshtastic_SerialHalResponse_Result_ERROR, "No interrupt slots available");
        return;
    }

    ::attachInterrupt((int)cmd.pin, isrTable[slot], toInterruptMode(cmd.mode));
}

void SerialHalDevice::handleDetachInterrupt(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response)
{
    // LOG_DEBUG("SerialHalDevice: detachInterrupt pin=%u", cmd.pin);

    ::detachInterrupt((int)cmd.pin);

    {
        std::lock_guard<std::mutex> lock(interruptMutex);
        const int slot = findSlotByPinLocked(cmd.pin);
        if (slot >= 0) {
            interruptSlots[slot] = InterruptSlot{};
        }
    }
}

void SerialHalDevice::handleSpiTransfer(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response)
{
    if (cmd.data.size == 0) {
        return;
    }

#if defined(ARCH_ESP32)
    if (spiLock == nullptr) {
        setResponseError(response, meshtastic_SerialHalResponse_Result_ERROR, "SPI lock not initialized");
        return;
    }

#if defined(HW_SPI1_DEVICE)
    SPIClass &spiBus = SPI1;
#else
    SPIClass &spiBus = SPI;
#endif

    response.data.size = cmd.data.size;

    {
        concurrency::LockGuard guard(spiLock);
        spiBus.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        spiBus.transferBytes(cmd.data.bytes, response.data.bytes, cmd.data.size);
        spiBus.endTransaction();
    }
#else
    // SPI wiring is board/radio-specific; keep this explicit for now.
    response.result = meshtastic_SerialHalResponse_Result_UNSUPPORTED;
    snprintf(response.error, sizeof(response.error), "SPI not supported on this platform");
#endif
}

void SerialHalDevice::emitResponse(const meshtastic_SerialHalResponse &response, StreamAPI *streamApi)
{
    if (streamApi == nullptr) {
        return;
    }

    // Encode the response
    uint8_t encoded[meshtastic_SerialHalResponse_size] = {0};
    const size_t responseLen =
        pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_SerialHalResponse_msg, static_cast<const void *>(&response));

    if (responseLen == 0 || responseLen > 0xFFFF) {
        LOG_ERROR("SerialHalDevice: Failed to encode response (len=%zu)", responseLen);
        return;
    }

    // Build frame with StreamAPI framing: START1 SERIALHAL_MAGIC LEN_H LEN_L [payload]
    constexpr uint8_t START1 = 0x94;
    constexpr uint8_t SERIALHAL_MAGIC = 0xA5;

    uint8_t hdr[4];
    hdr[0] = START1;
    hdr[1] = SERIALHAL_MAGIC;
    hdr[2] = (uint8_t)((responseLen >> 8) & 0xFF); // LEN_H
    hdr[3] = (uint8_t)(responseLen & 0xFF);        // LEN_L

    // Emit via StreamAPI (this uses the internal txBuf + framing)
    streamApi->emitSerialHalResponse(hdr, sizeof(hdr), encoded, responseLen);

    // Keep a recent stream instance so async interrupt events can be emitted.
    {
        std::lock_guard<std::mutex> lock(interruptMutex);
        interruptStreamApi = streamApi;
    }
}
