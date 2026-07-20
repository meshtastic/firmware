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

#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
// Last-seen USB-CDC host link (DTR/mount) state, sampled each runOnce() so a
// physical unplug/replug re-locks the per-connection admin auth (see runOnce()).
// Kept at file scope rather than as a member both because there is exactly one
// console singleton and because adding per-instance members to the PhoneAPI
// hierarchy has historically perturbed nRF52 USB-CDC enumeration (see PhoneAPI.h).
// Only compiled on lockdown (nRF52) builds.
static bool s_serialLinkUp = false;
#endif

/// Create the shared serial console once and register receive wakeups.
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

/// Initialize console, protobuf transport, serial port, and worker thread state.
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

/// Service one serial API iteration and select the next polling interval.
int32_t SerialConsole::runOnce()
{
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
    // Lockdown (nRF52) builds only. The SerialConsole is a process-lifetime
    // singleton, so its inherited PhoneAPI object - and therefore its entry in
    // the per-connection admin-auth slot table (keyed by PhoneAPI*) - is reused
    // for every USB/serial client for the whole boot. Nothing re-locks that slot
    // when the operator unplugs and a different client plugs in before the
    // 15-minute inactivity timeout fires, so a fresh client would inherit the
    // prior operator's admin authorization. Re-lock when the physical USB-CDC link
    // drops - the serial analog of the BLE onDisconnect() -> close() session reset.
    //
    // On the nRF52 TinyUSB (Adafruit) core, (bool)Port == tud_cdc_n_connected():
    // it goes false on cable unplug or host port-close (DTR de-assert). close()
    // frees the auth slot and resets PhoneAPI state, so whoever connects next
    // re-locks via handleStartConfig()'s !isConnected() branch on their first
    // want_config - the same physical-link boundary BLE enforces in onConnect().
    // Console transports without a real DTR line (e.g. a UART USER_DEBUG_PORT) hold
    // this constant, so no edge fires and we fall back to the existing inactivity
    // timeout - no worse than the pre-fix behavior.
    const bool linkUp = static_cast<bool>(Port);
    if (s_serialLinkUp && !linkUp)
        close();
    s_serialLinkUp = linkUp;
#endif

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

/// Flush raw output while preserving queued protobuf frames.
void SerialConsole::flush()
{
    // HWCDC::flush()'s no-progress path discards queued TX bytes, which would tear a
    // framed protobuf stream; framed output is drained by the TX interrupt instead.
    if (usingProtobufs)
        return;

    Port.flush();
}

/// Write raw console data only before protobuf framing becomes active.
size_t SerialConsole::write(uint8_t c)
{
    // Once a protobuf client is active, unframed bytes would corrupt its stream.
    if (usingProtobufs)
        return 1;

    if (c == '\n')
        RedirectablePrint::write('\r');
    return RedirectablePrint::write(c);
}

/// Wake the serial worker when PhoneAPI queues output.
void SerialConsole::onNowHasData(uint32_t fromRadioNum)
{
    setIntervalFromNow(0);
}

/// Wake the serial worker when receive activity is signaled.
void SerialConsole::rxInt()
{
    setIntervalFromNow(0);
}

/// Infer serial client connectivity from recent API contact.
bool SerialConsole::checkIsConnected()
{
    return Throttle::isWithinTimespanMs(lastContactMsec, SERIAL_CONNECTION_TIMEOUT);
}

/// Select bounded or non-blocking HWCDC writes based on host liveness.
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

/// Update HWCDC timeout mode around generic connection handling.
void SerialConsole::onConnectionChanged(bool connected)
{
    // Order matters on disconnect: make console TX non-blocking *before* the
    // PowerFSM/close handling below emits more log lines to a dead port.
    if (!connected) {
        setHostDraining(false);
        // Keep any retained tail: HWCDC may still hold its prefix, and dropping metadata
        // would let the next frame header land inside that frame's declared payload.
    }
    StreamAPI::onConnectionChanged(connected);
    if (connected)
        setHostDraining(true);
}

/// Continue retained USB CDC output under the shared stream lock.
bool SerialConsole::finishPendingFrame()
{
#ifdef IS_USB_SERIAL
    concurrency::LockGuard guard(&streamLock);
    return frameWriter.finishPendingFrame(Port);
#else
    return true;
#endif
}

/// Protect the retained log buffer from being overwritten.
bool SerialConsole::canEncodeLogRecord()
{
#ifdef IS_USB_SERIAL
    concurrency::LockGuard guard(&streamLock);
    return frameWriter.isIdle();
#else
    return true;
#endif
}

/// Frame USB CDC output and retain any unwritten tail.
bool SerialConsole::writeFrame(uint8_t *buf, size_t len, bool bestEffort)
{
#ifdef IS_USB_SERIAL
    if (len == 0 || !canWrite)
        return false;

    const size_t totalLen = buildFrameHeader(buf, len);

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

/// Route logs without allowing raw bytes into an active protobuf stream.
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
