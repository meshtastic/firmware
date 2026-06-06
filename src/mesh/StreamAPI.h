#pragma once

#include "PhoneAPI.h"
#include "Stream.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include <cstdarg>

// A To/FromRadio packet + our 32 bit header
#define MAX_STREAM_BUF_SIZE (MAX_TO_FROM_RADIO_SIZE + sizeof(uint32_t))

/**
 * A version of our 'phone' API that talks over a Stream.  So therefore well suited to use with serial links
 * or TCP connections.
 *
 * ## Wire encoding

When sending protobuf packets over serial or TCP each packet is preceded by uint32 sent in network byte order (big endian).
The upper 16 bits must be 0x94C3. The lower 16 bits are packet length (this encoding gives room to eventually allow quite large
packets).

Implementations validate length against the maximum possible size of a BLE packet (our lowest common denominator) of 512 bytes. If
the length provided is larger than that we assume the packet is corrupted and begin again looking for 0x4403 framing.

The packets flowing towards the device are ToRadio protobufs, the packets flowing from the device are FromRadio protobufs.
The 0x94C3 marker can be used as framing to (eventually) resync if packets are corrupted over the wire.

Note: the 0x94C3 framing was chosen to prevent confusion with the 7 bit ascii character set. It also doesn't collide with any
valid utf8 encoding. This makes it a bit easier to start a device outputting regular debug output on its serial port and then only
after it has received a valid packet from the PC, turn off unencoded debug printing and switch to this packet encoding.

 */
class StreamAPI : public PhoneAPI
{
    /**
     * The stream we read/write from
     */
    Stream *stream;

    uint8_t rxBuf[MAX_STREAM_BUF_SIZE] = {0};
    size_t rxPtr = 0;

    /// time of last rx, used, to slow down our polling if we haven't heard from anyone
    uint32_t lastRxMsec = 0;

  public:
    StreamAPI(Stream *_stream) : stream(_stream) {}

    /**
     * Currently we require frequent invocation from loop() to check for arrived serial packets and to send new packets to the
     * phone.
     */
    virtual int32_t runOncePart();
    virtual int32_t runOncePart(char *buf, uint16_t bufLen);

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override = 0;

  private:
    /**
     * Read any rx chars from the link and call handleToRadio
     */
    int32_t readStream();
    int32_t readStream(const char *buf, uint16_t bufLen);
    int32_t handleRecStream(const char *buf, uint16_t bufLen);

    /**
     * call getFromRadio() and deliver encapsulated packets to the Stream
     */
    void writeStream();

  protected:
    /**
     * Send a FromRadio.rebooted = true packet to the phone
     */
    void emitRebooted();

    virtual void onConnectionChanged(bool connected) override;

    /**
     * Send the current txBuffer over our stream
     */
    void emitTxBuffer(size_t len);

    /// Are we allowed to write packets to our output stream (subclasses can turn this off - i.e. SerialConsole)
    bool canWrite = true;

    /// Subclasses can use this scratch buffer if they wish
    uint8_t txBuf[MAX_STREAM_BUF_SIZE] = {0};

    /// Low level function to emit a protobuf encapsulated log record
    void emitLogRecord(meshtastic_LogRecord_Level level, const char *src, const char *format, va_list arg);

  private:
    /// Dedicated scratch + tx buffer for LogRecord emission.
    ///
    /// The main packet emission path (`writeStream` -> `getFromRadio` ->
    /// `emitTxBuffer`) holds `fromRadioScratch` (from PhoneAPI) and `txBuf`
    /// from the moment `getFromRadio` starts encoding until `emitTxBuffer`
    /// finishes pushing bytes to the stream. If a `LOG_` macro fires during
    /// that window and we emit through the API, the old implementation
    /// re-used `fromRadioScratch` / `txBuf` and corrupted whatever the main
    /// path had already encoded. Symptoms on the host were
    /// `google.protobuf.message.DecodeError: Error parsing message with type
    /// 'meshtastic.protobuf.FromRadio'` — any tool with
    /// `config.security.debug_log_api_enabled=true` under traffic would see
    /// torn frames every few messages.
    ///
    /// Giving the log path its own scratch + txBuf means the main path is
    /// never clobbered. We still need `streamLock` to serialize the actual
    /// `stream->write` call so a log emission and a packet emission don't
    /// interleave on the wire.
    meshtastic_FromRadio fromRadioScratchLog = {};
    uint8_t txBufLog[MAX_STREAM_BUF_SIZE] = {0};
    concurrency::Lock streamLock;
};