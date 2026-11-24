#pragma once

#include "mesh/generated/meshtastic/serial_hal.pb.h"
#include <cstdint>

/**
 * @brief Device-side handler for SerialHal GPIO/SPI operations over StreamAPI framing.
 *
 * This module decodes SerialHalCommand protobufs received from a host and executes
 * the requested GPIO (pinMode, digitalWrite, digitalRead, attach/detachInterrupt) or
 * SPI operations, then returns results via SerialHalResponse.
 *
 * Usage:
 *  1. Override StreamAPI::handleSerialHalCommand() in a subclass
 *  2. Call SerialHalDevice::handleCommand(buf, len, streamApi)
 *  3. SerialHalDevice will decode, execute, and emit the response
 *
 * The handler is only active when config.lora.serial_hal_only is true.
 */

class StreamAPI; // forward declaration

class SerialHalDevice
{
  public:
    /**
     * @brief Process a SerialHalCommand and emit a response.
     *
     * Decodes the protobuf, validates the operation, executes it on the device,
     * and writes the response back via the StreamAPI instance.
     *
     * @param buf     Pointer to the encoded SerialHalCommand protobuf payload (not including framing)
     * @param len     Length of the encoded payload
     * @param streamApi Pointer to the StreamAPI instance (used for emitting responses)
     */
    static void handleCommand(const uint8_t *buf, size_t len, StreamAPI *streamApi);

    /**
     * @brief Emit a SerialHalResponse back to the host via StreamAPI framing.
     *
     * Encodes the response protobuf and sends it with proper framing (START1 SERIALHAL_MAGIC LEN_H LEN_L payload).
     *
     * @param response The response to send
     * @param streamApi Pointer to the StreamAPI instance
     */
    static void emitResponse(const meshtastic_SerialHalResponse &response, StreamAPI *streamApi);

  private:
    /**
     * @brief Execute a GPIO pinMode operation.
     * @param cmd Decoded SerialHalCommand with PIN_MODE type
     * @param response Response object to fill with result
     */
    static void handlePinMode(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response);

    /**
     * @brief Execute a GPIO digitalWrite operation.
     * @param cmd Decoded SerialHalCommand with DIGITAL_WRITE type
     * @param response Response object to fill with result
     */
    static void handleDigitalWrite(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response);

    /**
     * @brief Execute a GPIO digitalRead operation.
     * @param cmd Decoded SerialHalCommand with DIGITAL_READ type
     * @param response Response object to fill with result (value field contains read result)
     */
    static void handleDigitalRead(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response);

    /**
     * @brief Execute an attachInterrupt operation.
     * @param cmd Decoded SerialHalCommand with ATTACH_INTERRUPT type
     * @param response Response object to fill with result
     */
    static void handleAttachInterrupt(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response);

    /**
     * @brief Execute a detachInterrupt operation.
     * @param cmd Decoded SerialHalCommand with DETACH_INTERRUPT type
     * @param response Response object to fill with result
     */
    static void handleDetachInterrupt(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response);

    /**
     * @brief Execute an SPI transfer operation.
     * @param cmd Decoded SerialHalCommand with SPI_TRANSFER type and data to send
     * @param response Response object to fill with result (data field contains received bytes)
     */
    static void handleSpiTransfer(const meshtastic_SerialHalCommand &cmd, meshtastic_SerialHalResponse &response);
};
