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
    DEBUG_PORT.rpInit(); // Simply sets up semaphore
}

void consolePrintf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    console->vprintf(format, arg);
    va_end(arg);
    console->flush();
}

SerialConsole::SerialConsole() : StreamAPI(&Port), RedirectablePrint(&Port), concurrency::OSThread("SerialConsole")
{
    assert(!console);
    console = this;
    canWrite = false; // We don't send packets to our port until it has talked to us first
                      // setDestination(&noopPrint); for testing, try turning off 'all' debug output and see what leaks

    Port.begin(SERIAL_BAUD);
#if defined(ARCH_NRF52) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARCH_RP2040)
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

int32_t SerialConsole::runOnce()
{
    return runOncePart();
}

void SerialConsole::flush()
{
    Port.flush();
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
    // only talk to the API once the configuration has been loaded and we're sure the serial port is not disabled.
    if (config.has_lora && config.device.serial_enabled) {
        // Turn off debug serial printing once the API is activated, because other threads could print and corrupt packets
        if (!config.device.debug_log_enabled)
            setDestination(&noopPrint);
        canWrite = true;

        return StreamAPI::handleToRadio(buf, len);
    } else {
        return false;
    }
}