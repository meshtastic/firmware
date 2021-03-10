#include "SerialConsole.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include <Arduino.h>

#define Port Serial

SerialConsole console;

void consolePrintf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    console.vprintf(format, arg);
    va_end(arg);
}

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
    if (!radioConfig.preferences.debug_log_enabled)
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
        // FIXME, we get no notice of serial going away, we should instead automatically generate this event if we haven't
        // received a packet in a while
        powerFSM.trigger(EVENT_SERIAL_DISCONNECTED);
    }
}