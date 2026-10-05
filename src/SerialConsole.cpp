// First, in its own block so the include sorter keeps it there: configuration.h supplies the
// variant defines mesh-pb-constants.h needs (portduino resolves MAX_NUM_NODES at runtime).
#include "configuration.h"

#include "Default.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "SerialConsole.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"
#include "main.h"
#include "time.h"

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#define IS_USB_SERIAL
#ifdef SERIAL_HAS_ON_RECEIVE
#undef SERIAL_HAS_ON_RECEIVE
#endif
// Port is HWCDC only in hardware USB-Serial/JTAG mode. With ARDUINO_USB_MODE=0 it is TinyUSB
// USBCDC, the PHY is routed away from the USJ peripheral, and isPlugged() would never see a SOF.
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
#define IS_USB_HWCDC
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

#ifdef MESHTASTIC_LOG_USB_STATS
// Bench: how much console output the port refuses, and what the USB link looks like while it does
#if defined(IS_USB_HWCDC) && defined(CONFIG_IDF_TARGET_ESP32S3)
#include "soc/usb_serial_jtag_struct.h"
#define USB_STALL_HW
#if defined(MESHTASTIC_HWCDC_INDUCE) && defined(MESHTASTIC_RADIO_TASK)
#include "concurrency/RadioTask.h"
#endif
#endif
#include <atomic>
namespace
{
struct UsbStats {
    uint32_t frames;                      // required frames (packets) written
    uint32_t logs;                        // best-effort log records offered to the port
    uint32_t dropRoom, dropTail, dropBig; // log records dropped: ring short of room, a frame tail retained, too big
    uint32_t dropRetained;                // log records not encoded: a retained frame still owns the log buffer
    uint32_t dropBusy;                    // log records dropped rather than wait for streamLock
};
UsbStats usbStats; // framed output, under streamLock
uint32_t usbStatsAt;
// Plain-text output (no API client): bytes the port took and refused. Totals, written only under the log lock.
volatile uint32_t textBytes, textRefused;
uint32_t textBytesSeen, textRefusedSeen;
// Times the TX latch held for LATCH_MS of refusals, and times it was kicked (MESHTASTIC_HWCDC_KICK). Under the log lock.
volatile uint32_t latches, kicks;
uint32_t latchesSeen, kicksSeen;
#ifdef MESHTASTIC_HWCDC_TX_BUFFER
constexpr size_t usbTxRing = MESHTASTIC_HWCDC_TX_BUFFER;
#else
constexpr size_t usbTxRing = 256; // HWCDC::begin()'s default
#endif

// The USB serial peripheral's state, sampled during a stall
struct UsbHw {
    uint32_t atMs; // ms into the stall
    uint16_t ringFree, sof;
    uint8_t epState, epWr, epRd;
    bool fifoFree, emptyEna, emptyRaw, plugged, taken;
};

#ifdef USB_STALL_HW
void sampleHw(UsbHw &hw, uint32_t atMs)
{
    hw.atMs = atMs;
    hw.ringFree = Port.availableForWrite();
    hw.fifoFree = USB_SERIAL_JTAG.ep1_conf.serial_in_ep_data_free;
    hw.emptyEna = USB_SERIAL_JTAG.int_ena.serial_in_empty_int_ena;
    hw.emptyRaw = USB_SERIAL_JTAG.int_raw.serial_in_empty_int_raw;
    hw.epState = USB_SERIAL_JTAG.in_ep1_st.in_ep1_state;
    hw.epWr = USB_SERIAL_JTAG.in_ep1_st.in_ep1_wr_addr;
    hw.epRd = USB_SERIAL_JTAG.in_ep1_st.in_ep1_rd_addr;
    hw.sof = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    hw.plugged = HWCDC::isPlugged();
    hw.taken = true;
}
#endif

// A stall: the port refusing output, from the first refusal to the next write it takes. One of 1 s or more is reported
// when it ends, since what was logged during it never reached the host.
enum UsbHwAt { HW_ONSET, HW_100MS, HW_1S, HW_LAST, HW_END, HW_COUNT };
struct UsbStall {
    uint32_t fromMs, ms;
    uint32_t bytes;     // plain-text bytes refused
    uint32_t drops;     // framed log records refused for room
    uint32_t unplugged; // refusals that found the USB link without SOFs
    uint32_t frames;    // packet frames written during it
    uint32_t latches;   // times the TX latch held for LATCH_MS during it
    uint32_t kicks;     // times it was kicked
    uint32_t latchFromMs;
    bool endedByFrame;
    UsbHw hw[HW_COUNT];
};
UsbStall stallNow, stallDone;
std::atomic<bool> stallDoneReady;
std::atomic<uint32_t> stallsUnreported;

#ifdef USB_STALL_HW
// Round 96's stalls: data waiting in the ring, an empty IN FIFO, and the TX interrupt enabled but not raised. HWCDC's ISR
// clears the interrupt without moving data when it finds the FIFO not writable, and nothing raises it again.
constexpr uint32_t LATCH_MS = 20;
bool txLatched()
{
    return Port.availableForWrite() == 0 && USB_SERIAL_JTAG.ep1_conf.serial_in_ep_data_free &&
           USB_SERIAL_JTAG.int_ena.serial_in_empty_int_ena && !USB_SERIAL_JTAG.int_raw.serial_in_empty_int_raw;
}

// A latch that holds for LATCH_MS of refusals is counted. MESHTASTIC_HWCDC_KICK also writes the refused byte straight into
// the FIFO: when the host reads it the interrupt is raised, and the ISR drains the ring again. That byte reaches the host
// ahead of the ring's older bytes, and would otherwise have been dropped.
void checkLatch(uint32_t now, uint8_t c)
{
    if (!txLatched()) {
        stallNow.latchFromMs = 0;
        return;
    }
    if (!stallNow.latchFromMs) {
        stallNow.latchFromMs = now ? now : 1;
        return;
    }
    if (now - stallNow.latchFromMs < LATCH_MS)
        return;
    stallNow.latchFromMs = 0;
    stallNow.latches++;
    latches = latches + 1;
#ifdef MESHTASTIC_HWCDC_KICK
    USB_SERIAL_JTAG.ep1.rdwr_byte = c;
    USB_SERIAL_JTAG.ep1_conf.wr_done = 1;
    stallNow.kicks++;
    kicks = kicks + 1;
#else
    (void)c;
#endif
}
#endif

#if defined(USB_STALL_HW) && defined(MESHTASTIC_HWCDC_INDUCE)
// Bench: make the latch on demand. With IN_EMPTY masked, wait for the host to read the FIFO (the interrupt then stays
// raised), clear it and unmask it: the same state as round 96's stalls, with nothing left to raise it again.
constexpr uint32_t INDUCE_EVERY_MS = 60 * 1000;
struct Induction {
    uint32_t atMs;     // 0: none in progress
    uint32_t healMs;   // ms from induction until the ISR drained the ring again
    uint32_t refused;  // plain-text bytes refused meanwhile
    uint32_t kicks;    // kicks meanwhile
    uint32_t waitedUs; // spent waiting for the host to read
    uint16_t minFree;  // lowest ring free since induction
    uint8_t epWr, epRd;
    bool raised; // false: IN_EMPTY never came, so nothing was induced
};
Induction induced, inducedDone;
uint32_t inducedLastMs;
std::atomic<bool> inducedDoneReady;

void induceLatch()
{
    const uint32_t from = micros();
    USB_SERIAL_JTAG.int_ena.serial_in_empty_int_ena = 0;
    bool raised = false;
    while (micros() - from < 20000) {
        if (USB_SERIAL_JTAG.int_raw.serial_in_empty_int_raw && USB_SERIAL_JTAG.ep1_conf.serial_in_ep_data_free) {
            raised = true;
            break;
        }
    }
    if (raised) {
        decltype(USB_SERIAL_JTAG.int_clr) clr = {};
        clr.serial_in_empty_int_clr = 1;
        USB_SERIAL_JTAG.int_clr.val = clr.val;
    }
    USB_SERIAL_JTAG.int_ena.serial_in_empty_int_ena = 1;
    const uint32_t now = millis();
    induced = {};
    induced.atMs = now ? now : 1;
    induced.waitedUs = micros() - from;
    induced.minFree = Port.availableForWrite();
    induced.epWr = USB_SERIAL_JTAG.in_ep1_st.in_ep1_wr_addr;
    induced.epRd = USB_SERIAL_JTAG.in_ep1_st.in_ep1_rd_addr;
    induced.raised = raised;
    induced.refused = textRefused;
    induced.kicks = kicks;
    inducedLastMs = now;
}

// Before each plain-text byte, under the log lock: induce once a minute (never on the radio task, which the wait would
// delay), and watch an induction until the ISR takes from the ring again
void induceOrWatch()
{
    if (!induced.atMs) {
        if (inducedDoneReady.load(std::memory_order_acquire) || !Throttle::hasElapsed(inducedLastMs, INDUCE_EVERY_MS))
            return;
#ifdef MESHTASTIC_RADIO_TASK
        if (concurrency::inRadioTask())
            return;
#endif
        induceLatch();
        if (induced.raised)
            return;
    } else {
        const uint16_t ringFree = Port.availableForWrite();
        if (ringFree < induced.minFree) {
            induced.minFree = ringFree;
            return;
        }
        if (ringFree == induced.minFree)
            return;
    }
    induced.healMs = millis() - induced.atMs;
    induced.refused = textRefused - induced.refused;
    induced.kicks = kicks - induced.kicks;
    inducedDone = induced;
    inducedDoneReady.store(true, std::memory_order_release);
    induced.atMs = 0;
}
#endif

void noteRefused(bool text, uint8_t c = 0)
{
    const uint32_t now = millis();
    if (!stallNow.fromMs) {
        stallNow = {};
        stallNow.fromMs = now ? now : 1;
#ifdef USB_STALL_HW
        sampleHw(stallNow.hw[HW_ONSET], 0);
#endif
    }
    if (text)
        stallNow.bytes++;
    else
        stallNow.drops++;
#ifdef IS_USB_HWCDC
    if (!HWCDC::isPlugged())
        stallNow.unplugged++;
#ifdef MESHTASTIC_HWCDC_NUDGE
    // HWCDC::write() re-arms the TX interrupt through this check if the driver has marked the host gone, but a refused
    // record never reaches write()
    (void)HWCDC::isConnected();
#endif
#endif
#ifdef USB_STALL_HW
    const uint32_t at = now - stallNow.fromMs;
    if (at >= 100 && !stallNow.hw[HW_100MS].taken)
        sampleHw(stallNow.hw[HW_100MS], at);
    if (at >= 1000 && !stallNow.hw[HW_1S].taken)
        sampleHw(stallNow.hw[HW_1S], at);
    if (at >= stallNow.hw[HW_LAST].atMs + 20)
        sampleHw(stallNow.hw[HW_LAST], at);
    if (text)
        checkLatch(now, c);
#else
    (void)c;
#endif
}

void noteWritten(bool frame)
{
    if (!stallNow.fromMs)
        return;
    if (frame)
        stallNow.frames++;
    stallNow.ms = millis() - stallNow.fromMs;
    stallNow.endedByFrame = frame;
    if (stallNow.ms >= 1000) {
        if (stallDoneReady.load(std::memory_order_acquire)) {
            stallsUnreported++;
        } else {
#ifdef USB_STALL_HW
            sampleHw(stallNow.hw[HW_END], stallNow.ms);
#endif
            stallDone = stallNow;
            stallDoneReady.store(true, std::memory_order_release);
        }
    }
    stallNow.fromMs = 0;
}

void noteText(size_t written, uint8_t c)
{
    if (written) {
        textBytes = textBytes + 1;
        noteWritten(false);
    } else {
        textRefused = textRefused + 1;
        noteRefused(true, c);
    }
}

// The loop's last few radio upkeeps (main.cpp), to place a stall's onset against them
struct RadioUpkeep {
    uint32_t fromMs, lockWaitMs, tookMs;
};
RadioUpkeep upkeeps[4];
uint8_t upkeepNext;
} // namespace

void noteRadioUpkeep(uint32_t fromMs, uint32_t lockWaitMs, uint32_t tookMs)
{
    upkeeps[upkeepNext] = {fromMs ? fromMs : 1, lockWaitMs, tookMs};
    upkeepNext = (upkeepNext + 1) % (sizeof(upkeeps) / sizeof(upkeeps[0]));
}
#endif

#if defined(IS_USB_SERIAL) && defined(MESHTASTIC_LOG_NEVER_WAIT)
namespace
{
// Bench: streamLock, taken outright for a packet frame, but for a log record only if it is free now. A log record is
// dropped rather than wait behind a packet frame's write, which can block for USB drain.
class StreamGuard
{
  public:
    StreamGuard(concurrency::Lock *lock, bool logRecord) : lock(lock)
    {
        if (logRecord) {
            held = lock->lock(0);
        } else {
            lock->lock();
            held = true;
        }
    }
    ~StreamGuard()
    {
        if (held)
            lock->unlock();
    }
    StreamGuard(const StreamGuard &) = delete;
    StreamGuard &operator=(const StreamGuard &) = delete;
    bool held = false;

  private:
    concurrency::Lock *lock;
};
} // namespace
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
#if defined(IS_USB_HWCDC) && defined(MESHTASTIC_HWCDC_TX_BUFFER)
    Port.setTxBufferSize(MESHTASTIC_HWCDC_TX_BUFFER); // before begin(), which creates a 256 B ring only if none exists
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

#ifdef MESHTASTIC_LOG_USB_STATS
    if (stallDoneReady.load(std::memory_order_acquire)) {
        const UsbStall stall = stallDone;
        stallDoneReady.store(false, std::memory_order_release);
        LOG_INFO("USB stall %u ms from uptime %u ms: refused %u B text, %u records; %u with no SOF; %u frames; ended by %s",
                 stall.ms, stall.fromMs, stall.bytes, stall.drops, stall.unplugged, stall.frames,
                 stall.endedByFrame ? "a packet" : "text or a log record");
        LOG_INFO("USB stall latch: held %u times, kicked %u times", stall.latches, stall.kicks);
        // The latest radio upkeep that started at or before the onset (upkeeps run on this thread)
        const RadioUpkeep *before = nullptr;
        for (const RadioUpkeep &u : upkeeps)
            if (u.fromMs && (int32_t)(stall.fromMs - u.fromMs) >= 0 && (!before || (int32_t)(u.fromMs - before->fromMs) > 0))
                before = &u;
        if (before)
            LOG_INFO("USB stall upkeep: started %u ms before onset, took %u ms (lock wait %u ms); %u more stalls unreported",
                     stall.fromMs - before->fromMs, before->tookMs, before->lockWaitMs, stallsUnreported.exchange(0));
        else
            LOG_INFO("USB stall upkeep: none recorded before onset; %u more stalls unreported", stallsUnreported.exchange(0));
#ifdef USB_STALL_HW
        static const char *const hwAt[HW_COUNT] = {"onset", "100 ms", "1 s", "last", "end"};
        for (int i = 0; i < HW_COUNT; i++) {
            const UsbHw &hw = stall.hw[i];
            if (hw.taken)
                LOG_INFO(
                    "USB stall hw %s +%u ms: ring free %u, FIFO free %u, IN_EMPTY ena %u raw %u, EP1 st %u wr %u rd %u, SOF %u, "
                    "plugged %u",
                    hwAt[i], hw.atMs, hw.ringFree, hw.fifoFree, hw.emptyEna, hw.emptyRaw, hw.epState, hw.epWr, hw.epRd, hw.sof,
                    hw.plugged);
        }
#endif
    }
#if defined(USB_STALL_HW) && defined(MESHTASTIC_HWCDC_INDUCE)
    if (inducedDoneReady.load(std::memory_order_acquire)) {
        const Induction done = inducedDone;
        inducedDoneReady.store(false, std::memory_order_release);
        if (done.raised)
            LOG_INFO("USB induce at uptime %u ms: waited %u us, EP1 wr %u rd %u; drained again after %u ms, %u B refused, "
                     "%u kicks",
                     done.atMs, done.waitedUs, done.epWr, done.epRd, done.healMs, done.refused, done.kicks);
        else
            LOG_INFO("USB induce at uptime %u ms: no IN_EMPTY in %u us, nothing induced", done.atMs, done.waitedUs);
    }
#endif
    if (Throttle::hasElapsed(usbStatsAt, 10000)) {
        usbStatsAt = millis();
        UsbStats seen;
        {
            concurrency::LockGuard guard(&streamLock);
            seen = usbStats;
            usbStats = {};
        }
        const uint32_t bytes = textBytes, refused = textRefused;
        LOG_INFO("USB 10 s: text %u B, refused %u B; frames %u; logs %u, dropped room %u tail %u big %u retained %u busy %u",
                 bytes - textBytesSeen, refused - textRefusedSeen, seen.frames, seen.logs, seen.dropRoom, seen.dropTail,
                 seen.dropBig, seen.dropRetained, seen.dropBusy);
        textBytesSeen = bytes;
        textRefusedSeen = refused;
        const uint32_t held = latches, kicked = kicks;
        if (held != latchesSeen || kicked != kicksSeen)
            LOG_INFO("USB 10 s latch: held %u times, kicked %u times", held - latchesSeen, kicked - kicksSeen);
        latchesSeen = held;
        kicksSeen = kicked;
    }
#endif
    int32_t delay = runOncePart();
#if defined(SERIAL_HAS_ON_RECEIVE) || defined(CONFIG_IDF_TARGET_ESP32S2)
    // Nothing wakes the idle sleep for "TX space freed" or a bounded-drain remainder
    // (#11164), so keep polling while the API holds undelivered output.
    if (hasPendingOutput())
        return delay < 25 ? delay : 25; // 0 continues a budget slice; else short-poll TX drain
    return Port.available() ? delay : INT32_MAX;
#elif defined(IS_USB_HWCDC)
    // isPlugged() is a SOF watchdog that flaps false while USB is fine (#11864), and nothing wakes
    // this thread on RX, so cap the idle sleep at the rate readStream() already idles at.
    return HWCDC::isPlugged() ? delay : 250;
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

#ifdef MESHTASTIC_LOG_USB_STATS
#if defined(USB_STALL_HW) && defined(MESHTASTIC_HWCDC_INDUCE)
    induceOrWatch();
#endif
    if (c == '\n') {
        RedirectablePrint::write('\r');
        noteText(destWritten, '\r');
    }
    RedirectablePrint::write(c);
    noteText(destWritten, c);
    return 1;
#else
    if (c == '\n')
        RedirectablePrint::write('\r');
    return RedirectablePrint::write(c);
#endif
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

/// Report a retained USB CDC frame awaiting TX space.
bool SerialConsole::hasRetainedFrame()
{
#ifdef IS_USB_SERIAL
    concurrency::LockGuard guard(&streamLock);
    return !frameWriter.isIdle();
#else
    return false;
#endif
}

/// Protect the retained log buffer from being overwritten.
bool SerialConsole::canEncodeLogRecord()
{
#ifdef IS_USB_SERIAL
#ifdef MESHTASTIC_LOG_NEVER_WAIT
    StreamGuard guard(&streamLock, true);
    if (!guard.held) {
#ifdef MESHTASTIC_LOG_USB_STATS
        usbStats.dropBusy++; // not under the lock: approximate
#endif
        return false;
    }
#else
    concurrency::LockGuard guard(&streamLock);
#endif
#ifdef MESHTASTIC_LOG_USB_STATS
    if (!frameWriter.isIdle()) {
        usbStats.dropRetained++;
        return false;
    }
    return true;
#else
    return frameWriter.isIdle();
#endif
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

#ifdef MESHTASTIC_LOG_NEVER_WAIT
    StreamGuard guard(&streamLock, bestEffort);
    if (!guard.held) {
#ifdef MESHTASTIC_LOG_USB_STATS
        usbStats.dropBusy++; // not under the lock: approximate
#endif
        return false;
    }
#else
    concurrency::LockGuard guard(&streamLock);
#endif
#ifdef MESHTASTIC_LOG_USB_STATS
    const bool written = frameWriter.writeFrame(Port, buf, totalLen, bestEffort);
    if (bestEffort) {
        usbStats.logs++;
        if (written) {
            noteWritten(false);
        } else if (!frameWriter.isIdle()) {
            usbStats.dropTail++;
        } else if (totalLen > usbTxRing) {
            usbStats.dropBig++;
        } else {
            usbStats.dropRoom++;
            noteRefused(false);
        }
    } else {
        usbStats.frames++;
        if (written)
            noteWritten(true);
    }
    return written;
#else
    return frameWriter.writeFrame(Port, buf, totalLen, bestEffort);
#endif
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
            auto thread = concurrency::OSThread::current();
            emitLogRecord(ll, thread ? thread->ThreadName.c_str() : "", format, arg);
        }
        return;
    }

    RedirectablePrint::log_to_serial(logLevel, format, arg);
}
