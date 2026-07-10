#pragma once

#include "RedirectablePrint.h"
#include "StreamAPI.h"
#include "mesh/StreamFrameWriter.h"
/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
class SerialConsole : public StreamAPI, public RedirectablePrint, private concurrency::OSThread
{
    /**
     * If true we are talking to a smart host and all messages (including log messages) must be framed as protobufs.
     */
    bool usingProtobufs = false;

  public:
    /// Initialize the shared serial stream for console and protobuf traffic.
    SerialConsole();

    /**
     * we override this to notice when we've received a protobuf over the serial stream.  Then we shunt off
     * debug serial output.
     */
    virtual bool handleToRadio(const uint8_t *buf, size_t len) override;

    /// Write a raw console byte unless the stream is in protobuf mode.
    virtual size_t write(uint8_t c) override;

    /// Service serial input, pending output, and connection state.
    virtual int32_t runOnce() override;

    /// Flush raw console output when explicitly requested.
    void flush();
    /// Wake the serial thread after receive activity.
    void rxInt();

  protected:
    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;

    /// Wake the serial thread when PhoneAPI queues output.
    virtual void onNowHasData(uint32_t fromRadioNum) override;

    /// Track serial API connect/disconnect so we can make console writes
    /// non-blocking while no host is listening (see setHostDraining()).
    virtual void onConnectionChanged(bool connected) override;

    /// Emit a framed API log when enabled, or raw output before protobuf mode.
    virtual void log_to_serial(const char *logLevel, const char *format, va_list arg);

    /// Continue retained USB CDC output before PhoneAPI advances.
    virtual bool finishPendingFrame() override;
    /// Return whether the dedicated log buffer can be safely overwritten.
    virtual bool canEncodeLogRecord() override;
    /// Write or retain one framed USB CDC message.
    virtual bool writeFrame(uint8_t *buf, size_t len, bool bestEffort) override;

  private:
    /// On USB CDC targets, keep console TX non-blocking unless a host is draining the
    /// port, so a dead host can't stall the main loop and trip the task watchdog.
    void setHostDraining(bool draining);

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    StreamFrameWriter frameWriter;
#endif
};

// A simple wrapper to allow non class aware code write to the console
void consolePrintf(const char *format, ...);
void consoleInit();

extern SerialConsole *console;