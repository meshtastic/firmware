#pragma once

#include "configuration.h"

#if HAS_CELLULAR

#include "concurrency/OSThread.h"
#include <Arduino.h>
#include <functional>
#include <vector>

// UART framing and the 3GPP TS 27.007 command set every AT modem implements. Dialect-specific
// behavior lives behind the hooks below, in a driver subclass (see EC618Modem).

#ifndef MODEM_SERIAL
#define MODEM_SERIAL Serial1
#endif

#ifndef MODEM_BAUD
#define MODEM_BAUD 115200
#endif

#ifndef MODEM_PWRKEY_PULSE_MS
#define MODEM_PWRKEY_PULSE_MS 1000
#endif

// How long the EN rail is held inactive before re-enabling it, so the output cap
// (these modules typically carry a tantalum on the rail) has time to fully drain.
#ifndef MODEM_EN_OFF_MS
#define MODEM_EN_OFF_MS 5000
#endif

// Settle time on the rail before PWRKEY is touched.
#ifndef MODEM_EN_SETTLE_MS
#define MODEM_EN_SETTLE_MS 100
#endif

#ifndef MODEM_BOOT_TIMEOUT_MS
#define MODEM_BOOT_TIMEOUT_MS 10000
#endif

// Longest single line accepted from the modem; longer lines are truncated.
#define AT_LINE_MAX 256

// Longest wait for the power-down command to answer before falling back to
// PWRKEY. Used by drivers implementing powerDown().
#define AT_POWERDOWN_TIMEOUT_MS 10000

enum class ATResult { Ok, Error, Timeout };

// Terminal result of a command, with the informational lines it produced
// (final OK/ERROR excluded, newline separated).
typedef std::function<void(ATResult, const String &)> ATCallback;
typedef std::function<void(const String &)> ATUrcHandler;

// Receives raw bytes captured outside line framing. Called once per capture,
// with fewer bytes than requested only if the capture timed out.
typedef std::function<void(const uint8_t *, size_t)> ATBinarySink;

// Results of the diagnostic hooks, parsed by the driver so the sweep never sees a vendor
// response format; valid is false when the module answered but the reading could not be parsed.
typedef std::function<void(ATResult, bool valid, uint8_t band, uint8_t act)> ATBandCallback;
typedef std::function<void(ATResult, bool valid, uint16_t millivolts)> ATBatteryCallback;

// Socket events the driver raises from its own URC handlers, so the Client
// implementation never matches a dialect's URC strings itself.
struct ATSocketEvents {
    std::function<void(const uint8_t *, size_t)> onData;
    std::function<void(const char *why)> onClosed;
    std::function<void()> onConnected;
    std::function<void()> onConnectFail;
    // Whether the module is holding unread bytes for us to fetch.
    std::function<void(bool)> onRxPending;
};

class ATModem : public concurrency::OSThread
{
  public:
    enum State {
        OFF,
        EN_OFF_HOLD,
        EN_ON,
        PWRKEY_IDLE,
        PWRKEY_ASSERT,
        PROBE,
        SESSION,
        READY,
        FAILED,
        POWERDOWN_WAIT,
        POWERDOWN_VERIFY
    };

    // Bring-up sub-steps the bearer walks; the driver supplies the commands.
    enum BearerStep { BEARER_SHUT, BEARER_CSTT, BEARER_CIICR, BEARER_CIFSR };

    ATModem();
    virtual ~ATModem() {}

    void start();

    // Drop any queued work and re-run the power-on sequence. Used when the
    // modem has reset or gone silent, and after powerOff() leaves it off.
    void restart();

    // Drop any queued work and wait for the module to answer AT again, without touching
    // PWRKEY - pulsing it mid-boot during an in-place AT+RESET could power the module off.
    void expectReboot();

    // Power the module down via powerDown(), falling back to a PWRKEY pulse if
    // it keeps answering. Ends in OFF, where submit() rejects everything.
    void powerOff();

    // Bring a module powered down by powerOff() back up from OFF.
    void powerUp();

    State getState() const { return state; }
    bool isOff() const { return state == OFF; }
    bool isReady() const { return state == READY; }

    // Queue an AT command. finalUrc defers cb from OK to a later URC with that prefix;
    // bareResponse completes on the first line received, for commands with no OK/ERROR at all.
    bool submit(const char *cmd, uint32_t timeoutMs = 5000, ATCallback cb = nullptr, const char *finalUrc = nullptr,
                bool bareResponse = false);

    // Queue a command that answers with a '>' prompt, after which payload is
    // written as raw bytes. Completes on SEND OK / SEND FAIL.
    bool submitWithPayload(const char *cmd, const uint8_t *payload, size_t len, uint32_t timeoutMs = 20000,
                           ATCallback cb = nullptr);

    void registerUrc(const char *prefix, ATUrcHandler handler);

    // Divert the next n bytes off the UART straight to sink, bypassing line framing. Call
    // from a URC handler whose header line is immediately followed by binary payload.
    bool captureBinary(size_t n, ATBinarySink sink);

    // Divert and drop the next n bytes off the UART without buffering them - for a payload
    // the driver has decided not to keep (oversized), so line framing still resyncs after it
    // without the unbounded allocation captureBinary() would need to hold all of it at once.
    bool discardBinary(size_t n);

    // Abandon the command in flight so the queue moves on - for a deferred-result command
    // whose outcome arrived as a URC the caller recognised but the registered final URC did not.
    void cancelActive();

    // Run one service pass. The scheduler calls this via runOnce(); callers that
    // must block on a modem result (the Client API) pump it directly.
    void service();

    // Drop a leading "<prefix>: " and any surrounding quotes from a response line.
    static String stripPrefix(const String &line, const char *prefix);

    const String &getModel() const { return model; }
    const String &getVersion() const { return version; }

    // --- Driver hooks --- defaults are 3GPP TS 27.007, or a no-op returning false; a
    // driver overrides only what its module differs on.

    // Reboot the module in place, without touching PWRKEY.
    virtual void resetModule(ATCallback cb);

    // Ask the module to power itself down, via enqueue() since state is already
    // POWERDOWN_WAIT. False leaves powerOff() to fall straight through to the PWRKEY pulse.
    virtual bool powerDown(ATCallback cb);

    // Report why the module cannot read the SIM. False when unsupported.
    virtual bool querySimDetect(ATCallback cb);

    // Whether the module reports SIM insertion/removal on its own; when it does not, it
    // only reads the SIM at boot, so a card inserted later is invisible until reset.
    virtual bool hasSimHotplug() const { return false; }

    // Serving band and access technology. False when unsupported, and the
    // diagnostic sweep skips the step.
    virtual bool queryBand(ATBandCallback cb);

    // Module supply voltage. False when unsupported.
    virtual bool queryBatteryMv(ATBatteryCallback cb);

    // Run one step of the bearer bring-up. For BEARER_CIFSR the callback's
    // response is the local address alone, with any dialect prefix removed.
    virtual void bringUpBearer(BearerStep step, ATCallback cb) = 0;

    // Install the socket event callbacks and register the driver's URC handlers.
    virtual void socketAttach(const ATSocketEvents &events) = 0;

    // Put the socket into manual-receive mode, so payload bytes never arrive
    // interleaved with command responses.
    virtual void socketRxMode(ATCallback cb) = 0;

    // Open a TCP connection. OK from the callback means connected; drivers whose
    // command only acknowledges must defer the callback to the outcome URC.
    virtual void socketOpen(const char *host, uint16_t port, ATCallback cb) = 0;
    virtual void socketSend(const uint8_t *buf, size_t len, ATCallback cb) = 0;

    // Fetch up to want buffered bytes, delivered through ATSocketEvents::onData.
    virtual void socketRecv(size_t want, uint32_t timeoutMs, ATCallback cb) = 0;
    virtual void socketClose(ATCallback cb) = 0;

    // Largest single send and receive the dialect's commands accept.
    virtual size_t socketSendMax() const = 0;
    virtual size_t socketRecvMax() const = 0;

    // How long socketOpen() may take to settle, which the caller also waits on.
    virtual uint32_t socketConnectTimeoutMs() const = 0;

  protected:
    int32_t runOnce() override;

    // Commands run once the modem answers AT. Subclasses extend this.
    virtual void sessionInit();

    // Called when sessionInit()'s commands have all completed.
    virtual void onReady();

    // Firmware revision, stored into version. Overridden where the module does
    // not implement the 3GPP command.
    virtual void queryVersion();

    // Queue a command without the state checks submit() applies, for the
    // power-down sequence's own commands.
    void enqueue(const char *cmd, uint32_t timeoutMs, ATCallback cb);

    String model, version;

  private:
    struct Command {
        String text;
        uint32_t timeoutMs;
        ATCallback cb;
        String finalUrc;
        bool bareResponse = false;
        bool usePrompt = false;
        std::vector<uint8_t> payload;
    };

    void powerOn();
    void clearSession();
    void setState(State next);
    void pumpSerial();
    void handleLine(const String &line);
    void dispatchUrc(const String &line);
    bool tryDispatchUrc(const String &line);
    static bool isErrorLine(const String &line);
    void finish(ATResult result);
    void startNextCommand();

    State state = OFF;
    bool pulseToOff = false; // the pulse in flight is powering the module down
    std::vector<Command> queue;
    std::vector<std::pair<String, ATUrcHandler>> urcHandlers;

    bool commandActive = false;    // a command has been written, awaiting terminator
    bool awaitingFinalUrc = false; // terminator seen, waiting on the deferred URC
    Command active;
    String response;
    uint32_t commandDeadline = 0;

    bool promptPending = false; // command written, awaiting '>' before the payload

    ATBinarySink binarySink;
    std::vector<uint8_t> binaryBuf;
    size_t binaryRemaining = 0;
    uint32_t binaryDeadline = 0;
    bool binaryDiscard = false; // true: drop the diverted bytes instead of buffering them

    String rxLine;
    uint32_t stateDeadline = 0; // pulse/boot timeout, per state
    uint32_t nextProbe = 0;
    uint32_t probeStart = 0; // when probing began, for the time-to-first-AT log
};

extern ATModem *cellModem;

#endif
