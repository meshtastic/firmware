#include "SerialConsole.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"

#define Port Serial
// Defaulting to the formerly removed phone_timeout_secs value of 15 minutes
#define SERIAL_CONNECTION_TIMEOUT (15 * 60) * 1000UL

SerialConsole *console;

void consoleInit()
{
    new SerialConsole(); // Must be dynamically allocated because we are now inheriting from thread
}

void consolePrintf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    console->vprintf(format, arg);
    va_end(arg);
}

SerialConsole::SerialConsole() : StreamAPI(&Port), RedirectablePrint(&Port)
{
    assert(!console);
    console = this;
    canWrite = false; // We don't send packets to our port until it has talked to us first
                      // setDestination(&noopPrint); for testing, try turning off 'all' debug output and see what leaks

    Port.begin(SERIAL_BAUD);
#ifdef ARCH_NRF52
    time_t timeout = millis();
    while (!Port) {
        if ((millis() - timeout) < 5000) {
            delay(100);
        } else {
            break;
        }
    }
#endif
    emitRebooted();
}


// For the serial port we can't really detect if any client is on the other side, so instead just look for recent messages
bool SerialConsole::checkIsConnected()
{
    uint32_t now = millis();
    return (now - lastContactMsec) < SERIAL_CONNECTION_TIMEOUT;
}

/**
 * we override this to notice when we've received a protobuf over the serial
 * stream.  Then we shunt off debug serial output.
 */
bool SerialConsole::handleToRadio(const uint8_t *buf, size_t len)
{
    // Turn off debug serial printing once the API is activated, because other threads could print and corrupt packets
    if (!config.device.debug_log_enabled)
        setDestination(&noopPrint);
    canWrite = true;

    return StreamAPI::handleToRadio(buf, len);
}
