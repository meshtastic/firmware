#pragma once

#include "RedirectablePrint.h"
#include "StreamAPI.h"
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
    SerialConsole();

    /**
     * we override this to notice when we've received a protobuf over the serial stream.  Then we shunt off
     * debug serial output.
     */
    virtual bool handleToRadio(const uint8_t *buf, size_t len) override;

    virtual size_t write(uint8_t c) override
    {
        if (c == '\n') // prefix any newlines with carriage return
            RedirectablePrint::write('\r');
        return RedirectablePrint::write(c);
    }

    virtual int32_t runOnce() override;

    void flush();
    void rxInt();

  protected:
    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;

    virtual void onNowHasData(uint32_t fromRadioNum) override;

    /// Track serial API connect/disconnect so we can make console writes
    /// non-blocking while no host is listening (see setHostDraining()).
    virtual void onConnectionChanged(bool connected) override;

    /// Possibly switch to protobufs if we see a valid protobuf message
    virtual void log_to_serial(const char *logLevel, const char *format, va_list arg);

  private:
    /// On USB CDC targets, keep console TX non-blocking unless a host is draining the
    /// port, so a dead host can't stall the main loop and trip the task watchdog.
    void setHostDraining(bool draining);
};

// A simple wrapper to allow non class aware code write to the console
void consolePrintf(const char *format, ...);
void consoleInit();

extern SerialConsole *console;
