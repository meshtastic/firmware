#include "mesh/cell/EC618Modem.h"

#if HAS_CELLULAR && defined(USE_EC618)

#include "NodeDB.h"
#include "mesh/cell/cellBearer.h"
#include "mesh/cell/cellDiagParse.h"

// Strip quotes and line terminators before embedding values in AT arguments.
static String sanitizeAtArg(const char *in)
{
    String out;
    if (!in)
        return out;
    for (const char *p = in; *p; p++) {
        if (*p == '"' || *p == '\r' || *p == '\n')
            continue;
        out += *p;
    }
    return out;
}

void EC618Modem::sessionInit()
{
    ATModem::sessionInit();

    // Boot reason, EC618 specific.
    submit("AT*EXINFO?", 5000, [this](ATResult r, const String &resp) {
        if (r == ATResult::Ok)
            bootReason = stripPrefix(resp, "*EXINFO");
    });

    // Auto-FOTA defaults to on, so modem firmware could change underneath the
    // application if a ProductKey is ever set. Make it an explicit choice.
    submit("AT+UPGRADE=\"AUTO\",0", 5000, [](ATResult r, const String &resp) {
        if (r != ATResult::Ok)
            LOG_WARN("Disabling modem auto-FOTA rejected: %s", resp.c_str());
    });

    // Roaming preference: read mode/acqorder/srvdomain back first so the rewrite below only
    // ever touches <roam>, not RAT selection or CS/PS domain.
    submit("AT^SYSCONFIG?", 5000, [this](ATResult r, const String &resp) {
        uint8_t mode = 0, acqorder = 0, srvdomain = 0;
        if (r != ATResult::Ok || !parseSysConfig(resp.c_str(), &mode, &acqorder, &srvdomain)) {
            LOG_WARN("Reading ^SYSCONFIG for roaming preference failed: %s", resp.c_str());
            return;
        }
        String cmd = String("AT^SYSCONFIG=") + mode + "," + acqorder + "," + (config.network.cell_roaming_enabled ? 1 : 0) + "," +
                     srvdomain;
        submit(cmd.c_str(), 5000, [](ATResult r2, const String &resp2) {
            if (r2 != ATResult::Ok)
                LOG_WARN("Setting roaming preference rejected: %s", resp2.c_str());
        });
    });

#if MODEM_SIM_HOTPLUG
    // Drive USIM_CD, so insertion and removal arrive as +CPIN URCs instead of
    // being invisible until the module is reset.
    String csdt = String("AT+CSDT=1,") + MODEM_SIM_HOTPLUG_EDGE;
    submit(csdt.c_str(), 5000, [](ATResult r, const String &resp) {
        if (r != ATResult::Ok)
            LOG_WARN("SIM hotplug detect rejected: %s", resp.c_str());
    });
#endif
}

void EC618Modem::onReady()
{
    LOG_INFO("EC618 modem ready - model '%s' version '%s' boot '%s'", getModel().c_str(), getVersion().c_str(),
             bootReason.c_str());
}

void EC618Modem::queryVersion()
{
    // The platform answers AT+VER, not the 3GPP AT+CGMR.
    submit("AT+VER", 5000, [this](ATResult r, const String &resp) {
        if (r == ATResult::Ok)
            version = resp;
    });
}

void EC618Modem::resetModule(ATCallback cb)
{
    submit("AT+RESET", 5000, cb);
}

bool EC618Modem::powerDown(ATCallback cb)
{
    enqueue("AT+CPOWD=1", AT_POWERDOWN_TIMEOUT_MS, cb);
    return true;
}

bool EC618Modem::querySimDetect(ATCallback cb)
{
    return submit("AT*SIMDETEC=0", 5000, cb);
}

bool EC618Modem::queryBand(ATBandCallback cb)
{
    return submit("AT*BANDIND?", 5000, [cb](ATResult r, const String &resp) {
        uint8_t band = 0, act = 0;
        bool valid = (r == ATResult::Ok) && parseBandInd(resp.c_str(), &band, &act);
        cb(r, valid, band, act);
    });
}

bool EC618Modem::queryBatteryMv(ATBatteryCallback cb)
{
    return submit("AT+CBC", 5000, [cb](ATResult r, const String &resp) {
        uint16_t mv = 0;
        bool valid = (r == ATResult::Ok) && parseCbcMillivolts(resp.c_str(), &mv);
        cb(r, valid, mv);
    });
}

void EC618Modem::bringUpBearer(BearerStep step, ATCallback cb)
{
    switch (step) {
    case BEARER_SHUT:
        // CSTT is only accepted from IP INITIAL, and a context left active by a
        // previous session survives an MCU reboot.
        submit("AT+CIPSHUT", 15000, cb);
        break;

    case BEARER_CSTT: {
        // CSTT is not optional even with no APN: it moves the module from IP INITIAL to
        // IP START, and CIICR is only valid from IP START. An empty APN asks for the subscription default.
        String cmd = String("AT+CSTT=\"") + sanitizeAtArg(config.network.cell_apn) + "\",\"" +
                     sanitizeAtArg(config.network.cell_apn_user) + "\",\"" + sanitizeAtArg(config.network.cell_apn_pass) + "\"";
        submit(cmd.c_str(), 10000, cb);
        break;
    }

    case BEARER_CIICR:
        // Context activation can take most of a minute on a cold attach.
        submit("AT+CIICR", 60000, cb);
        break;

    case BEARER_CIFSR:
        // CIFSR answers with the bare address and no OK.
        submit(
            "AT+CIFSR", 10000, [cb](ATResult r, const String &resp) { cb(r, stripPrefix(resp, "+CIFSR")); }, nullptr, true);
        break;
    }
}

// +CIPRXGET: 1 announces data; +CIPRXGET: 2,<len>,<remaining> heads a payload
// of exactly <len> raw bytes, which line framing must not see.
void EC618Modem::onCipRxGet(const String &line)
{
    String s = stripPrefix(line, "+CIPRXGET");
    int mode = s.toInt();

    if (mode == 1) {
        socketEvents.onRxPending(true);
        return;
    }
    if (mode != 2)
        return;

    int firstComma = s.indexOf(',');
    if (firstComma < 0)
        return;
    int secondComma = s.indexOf(',', firstComma + 1);

    int len = s.substring(firstComma + 1, secondComma < 0 ? s.length() : secondComma).toInt();
    int remaining = secondComma < 0 ? 0 : s.substring(secondComma + 1).toInt();
    socketEvents.onRxPending(remaining > 0);

    if (len > 0) {
        if ((size_t)len > socketRecvMax()) {
            // Still divert the announced bytes - just without buffering them - so line
            // framing resyncs afterward instead of parsing raw payload as AT text.
            LOG_WARN("CIPRXGET len %d exceeds %u, discarding", len, (unsigned)socketRecvMax());
            discardBinary(len);
            return;
        }
        auto onData = socketEvents.onData;
        if (!captureBinary(len, [onData](const uint8_t *data, size_t n) { onData(data, n); })) {
            // A capture is already occupying the only diversion slot ATModem tracks, so these
            // bytes cannot be diverted either; line framing will desync no matter what happens
            // next. Resync by re-probing the session rather than let corrupt AT text propagate.
            LOG_ERROR("CIPRXGET capture rejected, %d payload bytes will desync framing - resyncing", len);
            expectReboot();
        }
    }
}

// The module's own power-on banner - seeing it while a command is in flight means the
// modem reset itself mid-command, so fail that command now rather than wait out its
// full timeout. Text verified on Air780E/EX only; a sibling EC618 module should confirm
// it prints the same lines before relying on this.
void EC618Modem::onUnexpectedReboot()
{
    LOG_WARN("Modem rebooted unexpectedly mid-command");
    // Session state (ATE0, roaming config, CIPRXGET mode, ...) is gone with the
    // reboot; re-probe so sessionInit() runs again once AT answers.
    expectReboot();
}

void EC618Modem::socketAttach(const ATSocketEvents &events)
{
    socketEvents = events;

    registerUrc("+CIPRXGET", [this](const String &line) { onCipRxGet(line); });
    registerUrc("ALREADY CONNECT", [this](const String &) { socketEvents.onConnected(); });
    registerUrc("CONNECT FAIL", [this](const String &) { socketEvents.onConnectFail(); });
    registerUrc("CLOSED", [this](const String &) { socketEvents.onClosed("peer"); });
    registerUrc("+PDP: DEACT", [this](const String &) { socketEvents.onClosed("PDP deactivated"); });
    registerUrc("^boot.rom", [this](const String &) { onUnexpectedReboot(); });
    registerUrc("RDY", [this](const String &) { onUnexpectedReboot(); });
}

void EC618Modem::socketRxMode(ATCallback cb)
{
    submit("AT+CIPRXGET=1", 5000, cb);
}

void EC618Modem::socketSetTls(bool enabled, ATCallback cb)
{
    String cmd = String("AT+CIPSSL=") + (enabled ? 1 : 0);
    submit(cmd.c_str(), 5000, cb);
}

void EC618Modem::socketOpen(const char *host, uint16_t port, ATCallback cb)
{
    String cmd = String("AT+CIPSTART=\"TCP\",\"") + sanitizeAtArg(host) + "\"," + port;
    // OK from CIPSTART is acceptance only; the outcome arrives as a later URC.
    submit(cmd.c_str(), socketConnectTimeoutMs(), cb, "CONNECT OK");
}

void EC618Modem::socketSend(const uint8_t *buf, size_t len, ATCallback cb)
{
    String cmd = String("AT+CIPSEND=") + len;
    submitWithPayload(cmd.c_str(), buf, len, 20000, cb);
}

void EC618Modem::socketRecv(size_t want, uint32_t timeoutMs, ATCallback cb)
{
    String cmd = String("AT+CIPRXGET=2,") + want;
    submit(cmd.c_str(), timeoutMs, cb);
}

void EC618Modem::socketClose(ATCallback cb)
{
    submit("AT+CIPCLOSE", 10000, cb);
}

#endif
