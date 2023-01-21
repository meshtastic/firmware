#pragma once

#include "RedirectablePrint.h"
#include "StreamAPI.h"
/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
class SerialConsole : public StreamAPI, public RedirectablePrint, private concurrency::OSThread
{
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

  protected:
    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;
};

// A simple wrapper to allow non class aware code write to the console
void consolePrintf(const char *format, ...);
void consoleInit();

extern SerialConsole *console;
