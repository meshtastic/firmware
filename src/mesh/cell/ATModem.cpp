#include "mesh/cell/ATModem.h"

#if HAS_CELLULAR

ATModem *cellModem = nullptr;

// Poll fast enough that a 115200 baud line doesn't sit in the UART FIFO.
#define AT_TICK_MS 10

#define AT_PWRKEY_SETTLE_MS 100

#define AT_PROBE_INTERVAL_MS 500

// A binary capture that stalls this long is abandoned so line framing resumes.
#define AT_BINARY_TIMEOUT_MS 5000

// Settle time after the power-down command before checking whether the module
// really went quiet.
#define AT_POWERDOWN_SETTLE_MS 2000

ATModem::ATModem() : concurrency::OSThread("ATModem") {}

void ATModem::start()
{
#if defined(PIN_MODEM_TX) && defined(PIN_MODEM_RX) && defined(ARCH_ESP32)
    MODEM_SERIAL.begin(MODEM_BAUD, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
#else
    MODEM_SERIAL.begin(MODEM_BAUD);
#endif

    powerOn();
    setInterval(AT_TICK_MS);
}

void ATModem::powerOn()
{
#ifdef PIN_MODEM_EN
    // Idle PWRKEY before cycling the rail, so a warm reboot doesn't leave it
    // asserted once the rail comes back.
#ifdef PIN_MODEM_PWRKEY
    pinMode(PIN_MODEM_PWRKEY, OUTPUT);
    digitalWrite(PIN_MODEM_PWRKEY, !MODEM_PWRKEY_VALUE);
#endif
    pinMode(PIN_MODEM_EN, OUTPUT);
    digitalWrite(PIN_MODEM_EN, !MODEM_EN_VALUE);
    setState(EN_OFF_HOLD);
#elif defined(PIN_MODEM_PWRKEY)
    // Drive the idle level first so a warm reboot doesn't leave PWRKEY asserted.
    pinMode(PIN_MODEM_PWRKEY, OUTPUT);
    digitalWrite(PIN_MODEM_PWRKEY, !MODEM_PWRKEY_VALUE);
    setState(PWRKEY_IDLE);
#else
    // No PWRKEY wired: the board is assumed to auto-boot the modem.
    setState(PROBE);
#endif
}

void ATModem::enqueue(const char *cmd, uint32_t timeoutMs, ATCallback cb)
{
    Command c;
    c.text = cmd;
    c.timeoutMs = timeoutMs;
    c.cb = cb;
    queue.push_back(c);
}

// Shared teardown for restart() and expectReboot().
void ATModem::clearSession()
{
    // Every submitted command's callback runs exactly once, even across a restart:
    // drain the queue first so each of its callbacks still gets notified below,
    // rather than being silently dropped by the queue.clear() that follows
    // cancelActive() - which stays, to discard anything submit() adds from the
    // active callback, since the module hasn't finished restarting.
    std::vector<Command> dropped;
    dropped.swap(queue);
    cancelActive();
    queue.clear();
    commandActive = false;
    awaitingFinalUrc = false;
    active = Command();
    response = "";
    rxLine = "";
    binaryRemaining = 0;
    binaryDiscard = false;
    binarySink = nullptr;
    binaryBuf.clear();
    model = "";
    version = "";
    pulseToOff = false;

    for (auto &c : dropped)
        if (c.cb)
            c.cb(ATResult::Error, "");
}

void ATModem::restart()
{
    LOG_WARN("Cell modem restarting");
    clearSession();
    powerOn();
}

void ATModem::powerOff()
{
    if (state == OFF)
        return;

    LOG_INFO("Cell modem powering down");
    clearSession();
    setState(POWERDOWN_WAIT);
    // Either result is expected - the module may power off mid-response. With no
    // power-down command the PWRKEY pulse in POWERDOWN_VERIFY does all the work.
    if (!powerDown([this](ATResult, const String &) { setState(POWERDOWN_VERIFY); }))
        setState(POWERDOWN_VERIFY);
}

void ATModem::powerUp()
{
    if (state != OFF)
        return;

    LOG_INFO("Cell modem powering up");
    clearSession();
    powerOn();
}

void ATModem::expectReboot()
{
    LOG_INFO("Cell modem rebooting, waiting for AT");
    clearSession();
    setState(PROBE);
}

void ATModem::setState(State next)
{
    state = next;
    uint32_t now = millis();

    switch (next) {
    case OFF:
#ifdef PIN_MODEM_EN
        // Cut the rail last, after PWRKEY has already done its work.
        digitalWrite(PIN_MODEM_EN, !MODEM_EN_VALUE);
#endif
        break;
    case EN_OFF_HOLD:
        stateDeadline = now + MODEM_EN_OFF_MS;
        break;
    case EN_ON:
        stateDeadline = now + MODEM_EN_SETTLE_MS;
        break;
    case PWRKEY_IDLE:
        stateDeadline = now + AT_PWRKEY_SETTLE_MS;
        break;
    case PWRKEY_ASSERT:
        stateDeadline = now + MODEM_PWRKEY_PULSE_MS;
        break;
    case POWERDOWN_VERIFY:
        stateDeadline = now + AT_POWERDOWN_SETTLE_MS;
        break;
    case PROBE:
        stateDeadline = now + MODEM_BOOT_TIMEOUT_MS;
        probeStart = now;
        nextProbe = now;
        break;
    default:
        break;
    }
}

bool ATModem::submit(const char *cmd, uint32_t timeoutMs, ATCallback cb, const char *finalUrc, bool bareResponse)
{
    if (state == OFF || state == FAILED)
        return false;
    // Once powering down, a caller's command would only run against a module
    // that is already going away, and its timeout would stall the sequence.
    if (state == POWERDOWN_WAIT || state == POWERDOWN_VERIFY)
        return false;

    Command c;
    c.text = cmd;
    c.timeoutMs = timeoutMs;
    c.cb = cb;
    c.bareResponse = bareResponse;
    if (finalUrc)
        c.finalUrc = finalUrc;
    queue.push_back(c);
    return true;
}

bool ATModem::submitWithPayload(const char *cmd, const uint8_t *payload, size_t len, uint32_t timeoutMs, ATCallback cb)
{
    if (state == OFF || state == FAILED)
        return false;
    if (state == POWERDOWN_WAIT || state == POWERDOWN_VERIFY)
        return false;

    Command c;
    c.text = cmd;
    c.timeoutMs = timeoutMs;
    c.cb = cb;
    c.usePrompt = true;
    c.payload.assign(payload, payload + len);
    queue.push_back(c);
    return true;
}

bool ATModem::captureBinary(size_t n, ATBinarySink sink)
{
    if (binaryRemaining || !n)
        return false;

    binarySink = sink;
    binaryBuf.clear();
    binaryBuf.reserve(n);
    binaryRemaining = n;
    binaryDeadline = millis() + AT_BINARY_TIMEOUT_MS;
    return true;
}

bool ATModem::discardBinary(size_t n)
{
    if (binaryRemaining || !n)
        return false;

    binarySink = nullptr;
    binaryDiscard = true;
    binaryRemaining = n;
    binaryDeadline = millis() + AT_BINARY_TIMEOUT_MS;
    return true;
}

void ATModem::cancelActive()
{
    if (commandActive || awaitingFinalUrc)
        finish(ATResult::Error);
}

void ATModem::registerUrc(const char *prefix, ATUrcHandler handler)
{
    urcHandlers.push_back(std::make_pair(String(prefix), handler));
}

void ATModem::startNextCommand()
{
    if (commandActive || awaitingFinalUrc || queue.empty())
        return;

    active = queue.front();
    queue.erase(queue.begin());
    response = "";
    commandActive = true;
    promptPending = active.usePrompt;
    commandDeadline = millis() + active.timeoutMs;

    LOG_DEBUG("AT tx: %s", active.text.c_str());
    MODEM_SERIAL.print(active.text);
    MODEM_SERIAL.print("\r\n");
}

void ATModem::finish(ATResult result)
{
    commandActive = false;
    awaitingFinalUrc = false;
    promptPending = false;

    ATCallback cb = active.cb;
    String r = response;
    active = Command();
    response = "";

    if (cb)
        cb(result, r);
}

// Looks up and runs the handler for line's prefix. Returns false, without
// logging, when nothing matches - the mid-command caller in handleLine() uses
// that to fall through to response accumulation rather than treat every
// non-URC response line as an unrecognized URC.
bool ATModem::tryDispatchUrc(const String &line)
{
    for (auto &h : urcHandlers) {
        if (line.startsWith(h.first)) {
            h.second(line);
            return true;
        }
    }
    return false;
}

void ATModem::dispatchUrc(const String &line)
{
    if (!tryDispatchUrc(line))
        LOG_DEBUG("AT urc: %s", line.c_str());
}

// 3GPP TS 27.007 final result codes that report failure; +CME/+CMS ERROR both
// carry a diagnostic code after the colon that response accumulation would
// otherwise capture as an ordinary line.
bool ATModem::isErrorLine(const String &line)
{
    return line == "ERROR" || line.startsWith("+CME ERROR:") || line.startsWith("+CMS ERROR:");
}

void ATModem::handleLine(const String &line)
{
    if (line.length() == 0)
        return;

#ifdef CELL_DEBUG
    LOG_DEBUG("AT rx: %s", line.c_str());
#endif

    if (awaitingFinalUrc) {
        if (line.startsWith(active.finalUrc)) {
            response = line;
            finish(ATResult::Ok);
        } else {
            dispatchUrc(line);
        }
        return;
    }

    if (!commandActive) {
        dispatchUrc(line);
        return;
    }

    // Echo of our own command, before ATE0 takes effect.
    if (line == active.text)
        return;

    // Some commands answer with one line and no OK/ERROR at all.
    if (active.bareResponse && line != "OK") {
        response = line;
        finish(isErrorLine(line) ? ATResult::Error : ATResult::Ok);
        return;
    }

    // Data-mode result codes, which replace OK/ERROR entirely for a payload send.
    if (line == "SEND OK") {
        finish(ATResult::Ok);
        return;
    }
    if (line == "SEND FAIL") {
        response = line;
        finish(ATResult::Error);
        return;
    }

    // A payload command's OK is only the prompt's precursor, never completion.
    if (active.usePrompt && line == "OK")
        return;

    // "SHUT OK" is accepted alongside OK so a dialect that terminates a
    // command with its own success code still completes here.
    if (line == "OK" || line == "SHUT OK") {
        if (active.finalUrc.length()) {
            awaitingFinalUrc = true; // OK is acceptance, not completion
            commandActive = false;
        } else {
            finish(ATResult::Ok);
        }
        return;
    }

    if (isErrorLine(line)) {
        response = line;
        finish(ATResult::Error);
        return;
    }

    // A URC can arrive in the middle of a command; route it rather than let it
    // corrupt the response.
    if (tryDispatchUrc(line))
        return;

    if (response.length())
        response += "\n";
    response += line;
}

void ATModem::pumpSerial()
{
    while (MODEM_SERIAL.available()) {
        char c = (char)MODEM_SERIAL.read();

        // A binary capture owns the stream until it is satisfied - payload
        // bytes are not lines and must not touch the line buffer.
        if (binaryRemaining) {
            if (!binaryDiscard)
                binaryBuf.push_back((uint8_t)c);
            if (--binaryRemaining == 0) {
                binaryDiscard = false;
                ATBinarySink sink = binarySink;
                binarySink = nullptr;
                if (sink)
                    sink(binaryBuf.data(), binaryBuf.size());
                binaryBuf.clear();
            }
            continue;
        }

        // The prompt has no line terminator, so it is matched on the raw stream.
        if (promptPending && c == '>') {
            promptPending = false;
            rxLine = "";
            if (!active.payload.empty())
                MODEM_SERIAL.write(active.payload.data(), active.payload.size());
            continue;
        }

        if (c == '\r')
            continue;
        if (c == '\n') {
            String line = rxLine;
            rxLine = "";
            line.trim();
            handleLine(line);
            continue;
        }
        if (rxLine.length() < AT_LINE_MAX)
            rxLine += c;
    }
}

String ATModem::stripPrefix(const String &line, const char *prefix)
{
    String s = line;
    if (s.startsWith(prefix))
        s = s.substring(strlen(prefix));
    s.trim();
    if (s.startsWith(":"))
        s = s.substring(1);
    s.trim();
    if (s.length() >= 2 && s.startsWith("\"") && s.endsWith("\""))
        s = s.substring(1, s.length() - 1);
    return s;
}

void ATModem::sessionInit()
{
    submit("ATE0");
    submit("AT+CMEE=2");
    submit("AT+CGMM", 5000, [this](ATResult r, const String &resp) {
        if (r == ATResult::Ok)
            model = stripPrefix(resp, "+CGMM");
    });
    queryVersion();
}

void ATModem::queryVersion()
{
    submit("AT+CGMR", 5000, [this](ATResult r, const String &resp) {
        if (r == ATResult::Ok)
            version = stripPrefix(resp, "+CGMR");
    });
}

void ATModem::resetModule(ATCallback cb)
{
    submit("AT+CFUN=1,1", 5000, cb);
}

bool ATModem::powerDown(ATCallback cb)
{
    (void)cb;
    return false;
}

bool ATModem::querySimDetect(ATCallback cb)
{
    (void)cb;
    return false;
}

bool ATModem::queryBand(ATBandCallback cb)
{
    (void)cb;
    return false;
}

bool ATModem::queryBatteryMv(ATBatteryCallback cb)
{
    (void)cb;
    return false;
}

void ATModem::socketSetTls(bool enabled, ATCallback cb)
{
    (void)enabled;
    if (cb)
        cb(ATResult::Error, "");
}

void ATModem::onReady()
{
    LOG_INFO("Cell modem ready - model '%s' version '%s'", model.c_str(), version.c_str());
}

int32_t ATModem::runOnce()
{
    service();
    return AT_TICK_MS;
}

void ATModem::service()
{
    if (state == OFF)
        return;

    pumpSerial();
    uint32_t now = millis();

    if (binaryRemaining && (int32_t)(now - binaryDeadline) >= 0) {
        LOG_WARN("AT binary capture short by %u bytes", (unsigned)binaryRemaining);
        binaryRemaining = 0;
        binaryDiscard = false;
        ATBinarySink sink = binarySink;
        binarySink = nullptr;
        if (sink)
            sink(binaryBuf.data(), binaryBuf.size());
        binaryBuf.clear();
    }

    switch (state) {
#ifdef PIN_MODEM_EN
    case EN_OFF_HOLD:
        if ((int32_t)(now - stateDeadline) >= 0) {
            digitalWrite(PIN_MODEM_EN, MODEM_EN_VALUE);
            setState(EN_ON);
        }
        break;

    case EN_ON:
        if ((int32_t)(now - stateDeadline) >= 0) {
#ifdef PIN_MODEM_PWRKEY
            setState(PWRKEY_IDLE);
#else
            setState(PROBE);
#endif
        }
        break;
#endif

#ifdef PIN_MODEM_PWRKEY
    case PWRKEY_IDLE:
        if ((int32_t)(now - stateDeadline) >= 0) {
            digitalWrite(PIN_MODEM_PWRKEY, MODEM_PWRKEY_VALUE);
            setState(PWRKEY_ASSERT);
        }
        break;

    case PWRKEY_ASSERT:
        if ((int32_t)(now - stateDeadline) >= 0) {
            digitalWrite(PIN_MODEM_PWRKEY, !MODEM_PWRKEY_VALUE);
            if (pulseToOff) {
                pulseToOff = false;
                LOG_INFO("Cell modem PWRKEY pulsed to power off");
                setState(OFF);
            } else {
                LOG_INFO("Cell modem PWRKEY pulsed, waiting for AT");
                setState(PROBE);
            }
        }
        break;
#endif

    case POWERDOWN_VERIFY:
        // Power-down has answered; a module that still replies to AT needs the pulse.
        if (commandActive || awaitingFinalUrc)
            break;
        if ((int32_t)(now - stateDeadline) >= 0) {
            enqueue("AT", AT_PROBE_INTERVAL_MS, [this](ATResult r, const String &) {
                if (r != ATResult::Ok) {
                    LOG_INFO("Cell modem powered down");
                    setState(OFF);
                    return;
                }
#ifdef PIN_MODEM_PWRKEY
                LOG_WARN("Cell modem still answering after power-down, pulsing PWRKEY");
                pulseToOff = true;
                pinMode(PIN_MODEM_PWRKEY, OUTPUT);
                digitalWrite(PIN_MODEM_PWRKEY, !MODEM_PWRKEY_VALUE);
                setState(PWRKEY_IDLE);
#else
                // Nothing left to try; treat the modem as off so no further
                // commands are issued to it.
                LOG_WARN("Cell modem still answering after power-down and no PWRKEY wired");
                setState(OFF);
#endif
            });
        }
        break;

    case PROBE:
        if (commandActive || awaitingFinalUrc)
            break;
        if ((int32_t)(now - stateDeadline) >= 0) {
            LOG_ERROR("Cell modem did not answer AT within %u ms", (unsigned)MODEM_BOOT_TIMEOUT_MS);
            setState(FAILED);
            break;
        }
        if ((int32_t)(now - nextProbe) >= 0) {
            nextProbe = now + AT_PROBE_INTERVAL_MS;
            submit("AT", AT_PROBE_INTERVAL_MS, [this](ATResult r, const String &) {
                if (r == ATResult::Ok && state == PROBE) {
                    LOG_INFO("Cell modem answered AT after %u ms", (unsigned)(millis() - probeStart));
                    setState(SESSION);
                    sessionInit();
                }
            });
        }
        break;

    case SESSION:
        if (!commandActive && !awaitingFinalUrc && queue.empty()) {
            setState(READY);
            onReady();
        }
        break;

    default:
        break;
    }

    if ((commandActive || awaitingFinalUrc) && (int32_t)(now - commandDeadline) >= 0) {
        LOG_WARN("AT timeout: %s", active.text.c_str());
        finish(ATResult::Timeout);
    }

    startNextCommand();
}

#endif
