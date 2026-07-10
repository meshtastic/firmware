#include "SerialConsole.h"
#include "Default.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "main.h"
#include "time.h"

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#define IS_USB_SERIAL
#ifdef SERIAL_HAS_ON_RECEIVE
#undef SERIAL_HAS_ON_RECEIVE
#endif
#include "HWCDC.h"
#endif

#ifdef RP2040_SLOW_CLOCK
#define Port Serial2
#else
#ifdef USER_DEBUG_PORT // change by WayenWeng
#define Port USER_DEBUG_PORT
#else
#define Port Serial
#endif
#endif
// Defaulting to the formerly removed phone_timeout_secs value of 15 minutes
#define SERIAL_CONNECTION_TIMEOUT (15 * 60) * 1000UL

SerialConsole *console;

void consoleInit()
{
    if (console) {
        return;
    }
    auto sc = new SerialConsole(); // Must be dynamically allocated because we are now inheriting from thread

#if defined(SERIAL_HAS_ON_RECEIVE)
    // onReceive does only exist for HardwareSerial not for USB CDC serial
    Port.onReceive([sc]() { sc->rxInt(); });
#else
    (void)sc;
#endif
    DEBUG_PORT.rpInit(); // Simply sets up semaphore
}

void consolePrintf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    console->vprintf(nullptr, format, arg);
    va_end(arg);
    console->flush();
}

SerialConsole::SerialConsole() : StreamAPI(&Port), RedirectablePrint(&Port), concurrency::OSThread("SerialConsole")
{
    api_type = TYPE_SERIAL;
    assert(!console);
    console = this;
    canWrite = false; // We don't send packets to our port until it has talked to us first

#ifdef RP2040_SLOW_CLOCK
    Port.setTX(SERIAL2_TX);
    Port.setRX(SERIAL2_RX);
#endif
    Port.begin(SERIAL_BAUD);
    // Boot with console TX in non-blocking mode: no host is provably listening yet.
    setHostDraining(false);
    time_t timeout = millis();
    while (!Port) {
        if (Throttle::isWithinTimespanMs(timeout, FIVE_SECONDS_MS)) {
            delay(100);
        } else {
            break;
        }
    }
#if !ARCH_PORTDUINO
    emitRebooted();
#endif
}

int32_t SerialConsole::runOnce()
{
#ifdef HELTEC_MESH_SOLAR
    // After enabling the mesh solar serial port module configuration, command processing is handled by the serial port module.
    if (moduleConfig.serial.enabled && moduleConfig.serial.override_console_serial_port &&
        moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MS_CONFIG) {
        return 250;
    }
#endif

    int32_t delay = runOncePart();
#if defined(SERIAL_HAS_ON_RECEIVE) || defined(CONFIG_IDF_TARGET_ESP32S2)
    return Port.available() ? delay : INT32_MAX;
#elif defined(IS_USB_SERIAL)
    return HWCDC::isPlugged() ? delay : (1000 * 20);
#else
    return delay;
#endif
}

void SerialConsole::flush()
{
    Port.flush();
}

size_t SerialConsole::write(uint8_t c)
{
    // Once a protobuf client is active, unframed bytes would corrupt its stream.
    if (usingProtobufs)
        return 1;

    if (c == '\n')
        RedirectablePrint::write('\r');
    return RedirectablePrint::write(c);
}

// trigger tx of serial data
void SerialConsole::onNowHasData(uint32_t fromRadioNum)
{
    setIntervalFromNow(0);
}

// trigger rx of serial data
void SerialConsole::rxInt()
{
    setIntervalFromNow(0);
}

// For the serial port we can't really detect if any client is on the other side, so instead just look for recent messages
bool SerialConsole::checkIsConnected()
{
    return Throttle::isWithinTimespanMs(lastContactMsec, SERIAL_CONNECTION_TIMEOUT);
}

void SerialConsole::setHostDraining(bool draining)
{
#ifdef IS_USB_SERIAL
    // Timeout 0 makes HWCDC writes drop instead of block when the host stops draining;
    // bounded blocking is restored while an API client is connected so frames aren't truncated.
    Port.setTxTimeoutMs(draining ? 100 : 0);
#else
    (void)draining;
#endif
}

void SerialConsole::onConnectionChanged(bool connected)
{
    // Order matters on disconnect: make console TX non-blocking *before* the
    // PowerFSM/close handling below emits more log lines to a dead port.
    if (!connected)
        setHostDraining(false);
    StreamAPI::onConnectionChanged(connected);
    if (connected)
        setHostDraining(true);
}

bool SerialConsole::finishPendingFrame()
{
#ifdef IS_USB_SERIAL
    concurrency::LockGuard guard(&streamLock);
    return frameWriter.finishPendingFrame(Port);
#else
    return true;
#endif
}

bool SerialConsole::canEncodeLogRecord()
{
#ifdef IS_USB_SERIAL
    concurrency::LockGuard guard(&streamLock);
    return frameWriter.isIdle();
#else
    return true;
#endif
}

bool SerialConsole::writeFrame(uint8_t *buf, size_t len, bool bestEffort)
{
#ifdef IS_USB_SERIAL
    if (len == 0 || !canWrite)
        return false;

    constexpr size_t headerLen = 4;
    buf[0] = 0x94;
    buf[1] = 0xc3;
    buf[2] = (len >> 8) & 0xff;
    buf[3] = len & 0xff;
    const size_t totalLen = len + headerLen;

    concurrency::LockGuard guard(&streamLock);
    return frameWriter.writeFrame(Port, buf, totalLen, bestEffort);
#else
    return StreamAPI::writeFrame(buf, len, bestEffort);
#endif
}

/**
 * we override this to notice when we've received a protobuf over the serial
 * stream.  Then we shut off debug serial output.
 */
bool SerialConsole::handleToRadio(const uint8_t *buf, size_t len)
{
    // only talk to the API once the configuration has been loaded and we're sure the serial port is not disabled.
    if (config.has_lora && config.security.serial_enabled) {
        // The host just sent us bytes, so it is alive and draining the port:
        // restore normal bounded-blocking TX before any API response is written.
        setHostDraining(true);

        // Switch to protobufs for log messages
        usingProtobufs = true;
        canWrite = true;

        return StreamAPI::handleToRadio(buf, len);
    } else {
        return false;
    }
}

void SerialConsole::log_to_serial(const char *logLevel, const char *format, va_list arg)
{
    if (usingProtobufs) {
        if (config.security.debug_log_api_enabled && !pauseBluetoothLogging) {
            meshtastic_LogRecord_Level ll = RedirectablePrint::getLogLevel(logLevel);
            auto thread = concurrency::OSThread::currentThread;
            emitLogRecord(ll, thread ? thread->ThreadName.c_str() : "", format, arg);
        }
        return;
    }

    RedirectablePrint::log_to_serial(logLevel, format, arg);
}
