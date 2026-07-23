#include "StreamAPI.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "RedirectablePrint.h"
#include "SerialHalDevice.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "gps/RTC.h"

#define START1 0x94
#define START2 0xc3
#define SERIALHAL_MAGIC 0xa5 // second framing byte for SerialHal frames (START1 SH_MAGIC LEN_H LEN_L PAYLOAD)
#define HEADER_LEN 4

/// Poll the underlying stream, drain output, and update connection state.
int32_t StreamAPI::runOncePart()
{
    auto result = readStream();
    writeStream();
    checkConnectionTimeout();
    return result;
}

/// Consume supplied input bytes, drain output, and update connection state.
int32_t StreamAPI::runOncePart(char *buf, uint16_t bufLen)
{
    auto result = readStream(buf, bufLen);
    writeStream();
    checkConnectionTimeout();
    return result;
}

/**
 * Read any rx chars from the link and call handleRecStream
 */
int32_t StreamAPI::readStream(const char *buf, uint16_t bufLen)
{
    if (bufLen < 1) {
        // Nothing available this time, if the computer has talked to us recently, poll often, otherwise let CPU sleep a long time
        bool recentRx = Throttle::isWithinTimespanMs(lastRxMsec, 2000);
        return recentRx ? 5 : 250;
    } else {
        handleRecStream(buf, bufLen);
        // we had bytes available this time, so assume we might have them next time also
        lastRxMsec = millis();
        return 0;
    }
}

/**
 * call getFromRadio() and deliver encapsulated packets to the Stream
 */
void StreamAPI::writeStream()
{
    if (canWrite) {
        // A transport that retained a short frame must complete it before
        // getFromRadio() advances the PhoneAPI state to the next packet.
        if (!finishPendingFrame())
            return;

        uint32_t len;
        do {
            // Send every packet we can
            len = getFromRadio(txBuf + HEADER_LEN);
            if (len != 0 && !emitTxBuffer(len))
                break;
        } while (len);
    }
}

/// Parse supplied bytes through the framed ToRadio receive state machine.
int32_t StreamAPI::handleRecStream(const char *buf, uint16_t bufLen)
{
    uint16_t index = 0;
    while (bufLen > index) { // Currently we never want to block
        int cInt = buf[index++];
        if (cInt < 0)
            break; // We ran out of characters (even though available said otherwise) - this can happen on rf52 adafruit
                   // arduino

        uint8_t c = (uint8_t)cInt;

        // Use the read pointer for a little state machine, first look for framing, then length bytes, then payload
        size_t ptr = rxPtr;

        rxPtr++;        // assume we will probably advance the rxPtr
        rxBuf[ptr] = c; // store all bytes (including framing)

        // console->printf("rxPtr %d ptr=%d c=0x%x\n", rxPtr, ptr, c);

        if (ptr == 0) { // looking for START1
            if (c != START1)
                rxPtr = 0;     // failed to find framing
        } else if (ptr == 1) { // discriminate frame type on second byte
            if (c == START2) {
                rxIsSerialHal = false; // standard ToRadio frame
                serialHalRxActive.store(false);
                RedirectablePrint::setSerialHalLogSuppressed(false);
            } else if (c == SERIALHAL_MAGIC) {
                rxIsSerialHal = true; // SerialHal command frame
                serialHalRxActive.store(true);
                RedirectablePrint::setSerialHalLogSuppressed(true);
            } else {
                rxPtr = 0; // unrecognised second byte - not our frame
                serialHalRxActive.store(false);
                RedirectablePrint::setSerialHalLogSuppressed(false);
            }
        } else if (ptr >= HEADER_LEN - 1) {            // we have at least read our 4 byte framing
            uint32_t len = (rxBuf[2] << 8) + rxBuf[3]; // big endian 16 bit length follows framing

            // console->printf("len %d\n", len);

            if (ptr == HEADER_LEN - 1) {
                // we _just_ finished our 4 byte header, validate length now
                uint32_t maxLen = rxIsSerialHal ? (uint32_t)meshtastic_SerialHalCommand_size : MAX_TO_FROM_RADIO_SIZE;
                if (len > maxLen)
                    rxPtr = 0; // length is bogus, restart search for framing
            }

            if (rxPtr != 0)                        // Is packet still considered 'good'?
                if (ptr + 1 >= len + HEADER_LEN) { // have we received all of the payload?
                    rxPtr = 0;                     // start over again on the next packet

                    // Dispatch based on which frame type we identified at byte 1
                    if (rxIsSerialHal)
                        handleSerialHalCommand(rxBuf + HEADER_LEN, len);
                    else
                        handleToRadio(rxBuf + HEADER_LEN, len);

                    if (rxIsSerialHal)
                        serialHalRxActive.store(false);
                    if (rxIsSerialHal)
                        RedirectablePrint::setSerialHalLogSuppressed(false);
                }
        }
    }
    return 0;
}

/**
 * Read any rx chars from the link and call handleToRadio
 */
int32_t StreamAPI::readStream()
{
    if (!stream->available()) {
        // Nothing available this time, if the computer has talked to us recently, poll often, otherwise let CPU sleep a long time
        bool recentRx = Throttle::isWithinTimespanMs(lastRxMsec, 2000);
        if (!recentRx)
            return 250; // Sleep a long time if we haven't heard from the computer in a while
        if (serialHalRxActive.load())
            return 0; // If we are in the middle of a SerialHal transaction, don't sleep at all because we want to be as
                      // responsive as possible to incoming SerialHal bytes
        return 5;     // Otherwise, poll frequently for new data
    } else {
        while (stream->available()) { // Currently we never want to block
            int cInt = stream->read();
            if (cInt < 0)
                break; // We ran out of characters (even though available said otherwise) - this can happen on rf52 adafruit
                       // arduino

            uint8_t c = (uint8_t)cInt;

            // Use the read pointer for a little state machine, first look for framing, then length bytes, then payload
            size_t ptr = rxPtr;

            rxPtr++;        // assume we will probably advance the rxPtr
            rxBuf[ptr] = c; // store all bytes (including framing)

            // console->printf("rxPtr %d ptr=%d c=0x%x\n", rxPtr, ptr, c);

            if (ptr == 0) { // looking for START1
                if (c != START1)
                    rxPtr = 0;     // failed to find framing
            } else if (ptr == 1) { // discriminate frame type on second byte
                if (c == START2) {
                    rxIsSerialHal = false; // standard ToRadio frame
                    serialHalRxActive.store(false);
                    RedirectablePrint::setSerialHalLogSuppressed(false);
                } else if (c == SERIALHAL_MAGIC) {
                    rxIsSerialHal = true; // SerialHal command frame
                    serialHalRxActive.store(true);
                    RedirectablePrint::setSerialHalLogSuppressed(true);
                    LOG_WARN("StreamAPI: Detected SerialHal command frame");
                } else {
                    rxPtr = 0; // unrecognised second byte - not our frame
                    serialHalRxActive.store(false);
                    RedirectablePrint::setSerialHalLogSuppressed(false);
                }
            } else if (ptr >= HEADER_LEN - 1) {            // we have at least read our 4 byte framing
                uint32_t len = (rxBuf[2] << 8) + rxBuf[3]; // big endian 16 bit length follows framing

                // console->printf("len %d\n", len);

                if (ptr == HEADER_LEN - 1) {
                    // we _just_ finished our 4 byte header, validate length now
                    uint32_t maxLen = rxIsSerialHal ? (uint32_t)meshtastic_SerialHalCommand_size : MAX_TO_FROM_RADIO_SIZE;
                    if (len > maxLen)
                        rxPtr = 0; // length is bogus, restart search for framing
                }

                if (rxPtr != 0)                        // Is packet still considered 'good'?
                    if (ptr + 1 >= len + HEADER_LEN) { // have we received all of the payload?
                        rxPtr = 0;                     // start over again on the next packet

                        // Dispatch based on which frame type we identified at byte 1
                        if (rxIsSerialHal)
                            handleSerialHalCommand(rxBuf + HEADER_LEN, len);
                        else
                            handleToRadio(rxBuf + HEADER_LEN, len);

                        if (rxIsSerialHal)
                            serialHalRxActive.store(false);
                        if (rxIsSerialHal)
                            RedirectablePrint::setSerialHalLogSuppressed(false);
                    }
            }
        }

        // we had bytes available this time, so assume we might have them next time also
        lastRxMsec = millis();
        return 0;
    }
}

/// Encode the stream marker and big-endian payload length.
size_t StreamAPI::buildFrameHeader(uint8_t *buf, size_t payloadLen)
{
    buf[0] = START1;
    buf[1] = START2;
    buf[2] = (payloadLen >> 8) & 0xff;
    buf[3] = payloadLen & 0xff;
    return payloadLen + HEADER_LEN;
}

/**
 * Send the current txBuffer over our stream
 */
/// Write one framed payload using the transport's failure semantics.
bool StreamAPI::writeFrame(uint8_t *buf, size_t len, bool bestEffort)
{
    (void)bestEffort;
    if (len == 0 || !canWrite)
        return false;

    const size_t totalLen = buildFrameHeader(buf, len);
    // Serialize write-readiness checks, writes and write-failure handling
    // against concurrent stream writes/close.
    concurrency::LockGuard guard(&streamLock);
    if (!canWriteFrame(totalLen))
        return false;

    size_t written = stream->write(buf, totalLen);
    if (written == totalLen) {
        stream->flush();
        return true;
    }

    onFrameWriteFailed(totalLen, written);
    return false;
}

/// Emit the prepared main PhoneAPI payload as required output.
bool StreamAPI::emitTxBuffer(size_t len)
{
    return writeFrame(txBuf, len, false);
}

/// Emit the initial reboot notification as a framed FromRadio payload.
void StreamAPI::emitRebooted()
{
    // In case we send a FromRadio packet
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));
    fromRadioScratch.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
    fromRadioScratch.rebooted = true;

    // LOG_DEBUG("Emitting reboot packet for serial shell");
    emitTxBuffer(pb_encode_to_bytes(txBuf + HEADER_LEN, meshtastic_FromRadio_size, &meshtastic_FromRadio_msg, &fromRadioScratch));
}

/// Encode and emit one protobuf LogRecord using the dedicated log buffers.
void StreamAPI::emitLogRecord(meshtastic_LogRecord_Level level, const char *src, const char *format, va_list arg)
{
    if (serialHalRxActive.load()) {
        return;
    }
    // A retained short log frame still points into txBufLog, so do not overwrite it.
    if (!canEncodeLogRecord())
        return;

    // IMPORTANT: do NOT touch `fromRadioScratch` or `txBuf` here - those
    // belong to the main packet-emission path and a LOG_ firing during
    // `writeStream()` would corrupt an in-flight encode. We keep a
    // dedicated `fromRadioScratchLog` + `txBufLog` for log records and
    // only serialize the actual `stream->write` call via `streamLock` so
    // a concurrent packet emission doesn't interleave bytes on the wire.
    memset(&fromRadioScratchLog, 0, sizeof(fromRadioScratchLog));
    fromRadioScratchLog.which_payload_variant = meshtastic_FromRadio_log_record_tag;
    fromRadioScratchLog.log_record.level = level;

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    fromRadioScratchLog.log_record.time = rtc_sec;
    strncpy(fromRadioScratchLog.log_record.source, src, sizeof(fromRadioScratchLog.log_record.source) - 1);

    auto num_printed =
        vsnprintf(fromRadioScratchLog.log_record.message, sizeof(fromRadioScratchLog.log_record.message) - 1, format, arg);
    if (num_printed > 0 && fromRadioScratchLog.log_record.message[num_printed - 1] ==
                               '\n') // Strip any ending newline, because we have records for framing instead.
        fromRadioScratchLog.log_record.message[num_printed - 1] = '\0';

    size_t len =
        pb_encode_to_bytes(txBufLog + HEADER_LEN, meshtastic_FromRadio_size, &meshtastic_FromRadio_msg, &fromRadioScratchLog);
    writeFrame(txBufLog, len, true);
}

/// Hookable to find out when connection changes
void StreamAPI::onConnectionChanged(bool connected)
{
    // FIXME do reference counting instead

    if (connected) { // To prevent user confusion, turn off bluetooth while using the serial port api
        powerFSM.trigger(EVENT_SERIAL_CONNECTED);
    } else {
        // FIXME, we get no notice of serial going away, we should instead automatically generate this event if we haven't
        // received a packet in a while
        powerFSM.trigger(EVENT_SERIAL_DISCONNECTED);
    }
}

void StreamAPI::handleSerialHalCommand(const uint8_t *buf, size_t len)
{
    // Default implementation: dispatch to SerialHalDevice for GPIO/SPI handling
    SerialHalDevice::handleCommand(buf, len, this);
}

void StreamAPI::emitSerialHalResponse(const uint8_t *hdr, size_t hdrLen, const uint8_t *payload, size_t payloadLen)
{
    if (hdr == nullptr || hdrLen != 4 || payload == nullptr || payloadLen > meshtastic_SerialHalResponse_size) {
        LOG_ERROR("StreamAPI: Invalid SerialHal response parameters");
        return;
    }

    // Build complete frame in a temporary buffer
    uint8_t frame[4 + meshtastic_SerialHalResponse_size];
    memcpy(frame, hdr, hdrLen);
    memcpy(frame + hdrLen, payload, payloadLen);

    size_t totalLen = hdrLen + payloadLen;

    // Serialize stream writes against other emit operations via streamLock
    concurrency::LockGuard guard(&streamLock);
    stream->write(frame, totalLen);
    stream->flush();
    LOG_WARN("StreamAPI: Emitted SerialHal response frame (len=%zu)", totalLen);
}
