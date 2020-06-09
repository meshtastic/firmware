#include "SerialConsole.h"
#include "PowerFSM.h"
#include "configuration.h"
#include <Arduino.h>

#define Port Serial

SerialConsole console;

SerialConsole::SerialConsole() : StreamAPI(&Port), RedirectablePrint(&Port)
{
    canWrite = false; // We don't send packets to our port until it has talked to us first
    // setDestination(&noopPrint); for testing, try turning off 'all' debug output and see what leaks
}

/// Do late init that can't happen at constructor time
void SerialConsole::init()
{
    Port.begin(SERIAL_BAUD);
    StreamAPI::init();
    emitRebooted();
}

/**
 * we override this to notice when we've received a protobuf over the serial
 * stream.  Then we shunt off debug serial output.
 */
void SerialConsole::handleToRadio(const uint8_t *buf, size_t len)
{
    // Turn off debug serial printing once the API is activated, because other threads could print and corrupt packets
    setDestination(&noopPrint);
    canWrite = true;

    StreamAPI::handleToRadio(buf, len);
}

/// Hookable to find out when connection changes
void SerialConsole::onConnectionChanged(bool connected)
{
    if (connected) { // To prevent user confusion, turn off bluetooth while using the serial port api
        powerFSM.trigger(EVENT_SERIAL_CONNECTED);
    } else {
        powerFSM.trigger(EVENT_SERIAL_DISCONNECTED);
    }
}