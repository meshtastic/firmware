#include "SerialConsole.h"
#include "configuration.h"
#include <Arduino.h>

#define Port Serial

SerialConsole console;

SerialConsole::SerialConsole() : StreamAPI(&Port), RedirectablePrint(&Port)
{
    canWrite = false; // We don't send packets to our port until it has talked to us first
    // setDestination(&noopPrint);
}

/// Do late init that can't happen at constructor time
void SerialConsole::init()
{
    Port.begin(SERIAL_BAUD);
    StreamAPI::init();
}

/**
 * we override this to notice when we've received a protobuf over the serial stream.  Then we shunt off
 * debug serial output.
 */
void SerialConsole::handleToRadio(const uint8_t *buf, size_t len)
{
    setDestination(&noopPrint);
    canWrite = true;

    StreamAPI::handleToRadio(buf, len);
}