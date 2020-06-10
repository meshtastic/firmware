#pragma once

#include "RedirectablePrint.h"
#include "StreamAPI.h"
/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
class SerialConsole : public StreamAPI, public RedirectablePrint
{
  public:
    SerialConsole();

    /// Do late init that can't happen at constructor time
    virtual void init();

    /**
     * we override this to notice when we've received a protobuf over the serial stream.  Then we shunt off
     * debug serial output.
     */
    virtual void handleToRadio(const uint8_t *buf, size_t len);

    virtual size_t write(uint8_t c)
    {
        if (c == '\n') // prefix any newlines with carriage return
            RedirectablePrint::write('\r');
        return RedirectablePrint::write(c);
    }

  protected:
    /// Hookable to find out when connection changes
    virtual void onConnectionChanged(bool connected);
};

extern SerialConsole console;
