#include "mesh/cell/cellBearer.h"

#if HAS_CELLULAR

#include "concurrency/Periodic.h"
#include "gps/RTC.h"
#include "mesh/cell/cellDiag.h"
#include "mesh/cell/cellDiagParse.h"
#include "mesh/cell/cellModem.h"
#if HAS_SCREEN
#include "graphics/Screen.h"
#include "main.h"
#endif

using namespace concurrency;

// Bearer poll interval, matching reconnectETH().
#define CELL_POLL_MS 5000

#define CELL_BACKOFF_MAX_MS 60000

// A modem that stops answering AT for this long is power-cycled.
#define CELL_SILENCE_MS 30000

static Periodic *cellEvent;
static CellState cellState = CELL_OFF;
static String cellLocalIP;
static bool cellBusy = false;      // a bring-up command is in flight
static bool cellRtcSeeded = false; // AT+CCLK? has set the RTC this session
static bool cellEnabled = true;    // cleared by the UI toggle, not persisted
static uint8_t cellFailCount = 0;
static uint32_t cellLastResponse = 0;

// Whether the missing SIM has already been reported this session.
static bool simAbsentLogged = false;
static bool simAbsent = false;    // last CPIN? said no card is seated
static uint32_t simWaitSince = 0; // when SIM_WAIT was entered
static uint8_t simResetCount = 0; // resets already spent looking for a card

// Where CELL_BEARER_UP's entry sequence has got to.
static ATModem::BearerStep bearerStep = ATModem::BEARER_SHUT;

const char *getCellStateName(CellState s)
{
    switch (s) {
    case CELL_OFF:
        return "OFF";
    case CELL_BOOTING:
        return "BOOTING";
    case CELL_SIM_WAIT:
        return "SIM_WAIT";
    case CELL_REG_WAIT:
        return "REG_WAIT";
    case CELL_BEARER_UP:
        return "BEARER_UP";
    case CELL_IP_UP:
        return "IP_UP";
    case CELL_FAILED:
        return "FAILED";
    }
    return "?";
}

CellState getCellState()
{
    return cellState;
}

const String &getCellLocalIP()
{
    return cellLocalIP;
}

bool isCellularAvailable()
{
    return cellState == CELL_IP_UP;
}

static void setCellState(CellState next)
{
    if (next == cellState)
        return;
    LOG_INFO("Cell state %s -> %s", getCellStateName(cellState), getCellStateName(next));
    cellState = next;

    if (next == CELL_SIM_WAIT) {
        simWaitSince = millis();
        simAbsent = false; // until the next CPIN? says otherwise
    }

    // No sweep runs below REG_WAIT, so the readings would otherwise keep
    // rendering as though current after a SIM pull or a modem power-cycle.
    if (next != CELL_REG_WAIT && next != CELL_BEARER_UP && next != CELL_IP_UP)
        cellDiagInvalidate();

#if HAS_SCREEN
    // The Cellular frame redraws on the UI's own cadence, so without this a transition sits
    // unrendered - most visibly the toggle, where the frame keeps claiming BEARER_UP after power-down.
    if (screen)
        screen->forceDisplay();
#endif
}

// Common tail for every bring-up callback: note liveness and clear the in-flight
// flag. Shared with the diagnostic sweep, which takes part in both.
void cellCommandDone(ATResult r)
{
    cellBusy = false;
    if (r != ATResult::Timeout)
        cellLastResponse = millis();
}

static void cellFail(const char *why, const String &detail = String())
{
    LOG_WARN("Cell bring-up failed: %s%s%s", why, detail.length() ? " - " : "", detail.c_str());
    if (cellFailCount < 255)
        cellFailCount++;
    cellLocalIP = "";
    setCellState(CELL_FAILED);
}

// +CCLK: "yy/MM/dd,hh:mm:ss+zz" - zz is the local offset in quarter hours.
static bool setRtcFromCclk(const String &resp)
{
    String s = ATModem::stripPrefix(resp, "+CCLK");
    if (s.length() < 17)
        return false;

    struct tm t = {};
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0, tz = 0;
    if (sscanf(s.c_str(), "%d/%d/%d,%d:%d:%d%d", &year, &mon, &day, &hour, &min, &sec, &tz) < 6)
        return false;

    t.tm_year = year + 100; // two digit year, 20xx
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    // Shift to UTC; mktime normalises the out-of-range seconds.
    t.tm_sec = sec - tz * 15 * 60;

    // Operator network time, not NTP, but it is the same order of accuracy and
    // there is no distinct quality level for it.
    return perhapsSetRTC(RTCQualityNTP, t) == RTCSetResultSuccess;
}

// How long to sit in SIM_WAIT with no card before resetting the module. A working SIM reaches
// READY a few seconds in, so this must stay clear of that: resetting mid-init keeps a slow card from ever coming up.
#ifndef MODEM_SIM_RESET_MS
#define MODEM_SIM_RESET_MS 30000
#endif

// Wait before the next reset, growing to the bring-up backoff ceiling so a
// board simply running without a SIM settles down instead of resetting forever.
static uint32_t simResetBackoff()
{
    uint32_t backoff = MODEM_SIM_RESET_MS * (simResetCount + 1);
    return backoff > CELL_BACKOFF_MAX_MS ? CELL_BACKOFF_MAX_MS : backoff;
}

// Without USIM_CD wired, a card inserted after boot goes unnoticed and re-pulsing PWRKEY does nothing while running.
// With EN wired, a rail cycle forces a SIM re-read; without it, only an in-place reboot is available.
static void stepSimReset()
{
    LOG_INFO("No SIM for %ums, resetting modem to look again", (unsigned)simResetBackoff());
    if (simResetCount < 255)
        simResetCount++;
#ifdef PIN_MODEM_EN
    // A direct, synchronous power cycle, same as reconnectCell()'s silence handling.
    cellModem->restart();
    cellLocalIP = "";
    cellRtcSeeded = false;
    cellLastResponse = millis();
    simAbsentLogged = false;
    setCellState(CELL_BOOTING);
#else
    cellBusy = true;
    cellModem->resetModule([](ATResult, const String &) {
        // Either result is expected: the module may reboot before answering.
        cellCommandDone(ATResult::Ok);
        cellModem->expectReboot();
        setCellState(CELL_BOOTING);
    });
#endif
}

// Follows the detect result, so the diagnosis is logged before the advice.
static void warnNoSimHotplug()
{
    if (cellModem->hasSimHotplug())
        return;
    // The module only reads the SIM at boot, so say that a card inserted now is
    // picked up by the reset rather than immediately.
    LOG_WARN("No SIM detect on this board; a card inserted now is picked up at the next modem reset");
}

static void stepSimWait()
{
    cellBusy = true;
    cellModem->submit("AT+CPIN?", 5000, [](ATResult r, const String &resp) {
        cellCommandDone(r);
        if (r == ATResult::Ok && resp.indexOf("READY") >= 0) {
            simAbsentLogged = false;
            simAbsent = false;
            simResetCount = 0;
            setCellState(CELL_REG_WAIT);
            return;
        }
        if (r == ATResult::Ok) {
            // Present but not READY - PIN required, or still initialising.
            // Resetting would not help either case.
            simAbsent = false;
            LOG_WARN("SIM not ready: %s", resp.c_str());
            return; // retry next tick, the SIM may still be initialising
        }
        simAbsent = true;
        // Distinguish "no SIM seated" from "SIM present, not READY" - a user error vs a provisioning
        // error - and report it once, the way initEthernet() reports a missing shield.
        if (simAbsentLogged)
            return;
        simAbsentLogged = true;
        cellBusy = true;
        bool asked = cellModem->querySimDetect([](ATResult r2, const String &resp2) {
            cellCommandDone(r2);
            if (r2 == ATResult::Ok)
                LOG_WARN("SIM detect: %s", resp2.c_str());
            else
                LOG_WARN("SIM not readable and detect unsupported");
            warnNoSimHotplug();
        });
        if (!asked) {
            cellBusy = false;
            LOG_WARN("SIM not readable and detect unsupported");
            warnNoSimHotplug();
        }
    });
}

static void stepRegWait()
{
    cellBusy = true;
    cellModem->submit("AT+CEREG?", 5000, [](ATResult r, const String &resp) {
        cellCommandDone(r);
        if (r != ATResult::Ok)
            return;
        int stat = parseCeregStat(resp.c_str());
        if (stat == 1 || stat == 5) {
            LOG_INFO("Cell registered (%s)", stat == 5 ? "roaming" : "home");
            bearerStep = ATModem::BEARER_SHUT;
            cellFailCount = 0;
            setCellState(CELL_BEARER_UP); // provisional; cleared if bring-up fails
            cellLocalIP = "";
        }
    });
}

static void stepBearer()
{
    cellBusy = true;

    switch (bearerStep) {
    case ATModem::BEARER_SHUT:
        // Clearing a context left active by a previous session is best effort,
        // so either result moves on.
        cellModem->bringUpBearer(bearerStep, [](ATResult, const String &) {
            cellCommandDone(ATResult::Ok);
            bearerStep = ATModem::BEARER_CSTT;
        });
        break;

    case ATModem::BEARER_CSTT:
        cellModem->bringUpBearer(bearerStep, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            if (r == ATResult::Ok)
                bearerStep = ATModem::BEARER_CIICR;
            else
                cellFail("APN setup rejected", resp);
        });
        break;

    case ATModem::BEARER_CIICR:
        cellModem->bringUpBearer(bearerStep, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            if (r == ATResult::Ok)
                bearerStep = ATModem::BEARER_CIFSR;
            else
                cellFail("context did not activate", resp);
        });
        break;

    case ATModem::BEARER_CIFSR:
        cellModem->bringUpBearer(bearerStep, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            if (r != ATResult::Ok || resp.length() == 0) {
                cellFail("no local address", resp);
                return;
            }
            cellLocalIP = resp;
            LOG_INFO("Cell bearer up - IP %s", cellLocalIP.c_str());
            setCellState(CELL_IP_UP);
        });
        break;
    }
}

bool isCellularEnabled()
{
    return cellEnabled;
}

void setCellularEnabled(bool enabled)
{
#if !defined(PIN_MODEM_PWRKEY) && !defined(PIN_MODEM_EN)
    // Neither PWRKEY nor EN wired means a powered-down module could never be brought back
    // up, so the toggle is compiled out of the menu; reached only by another caller.
    (void)enabled;
    LOG_WARN("Cell toggle ignored: no PWRKEY or EN wired");
#else
    if (enabled == cellEnabled)
        return;
    cellEnabled = enabled;

    if (!cellModem) // toggled before initCellular(), nothing to power
        return;

    cellLocalIP = "";
    cellRtcSeeded = false;
    cellBusy = false;

    if (enabled) {
        cellModem->powerUp();
        cellLastResponse = millis();
        cellFailCount = 0;
        simAbsentLogged = false;
        simResetCount = 0;
        setCellState(CELL_BOOTING);
    } else {
        cellModem->powerOff();
        setCellState(CELL_OFF);
    }
#endif
}

static int32_t reconnectCell()
{
    if (!cellModem || !cellEnabled)
        return CELL_POLL_MS;

    if (cellBusy)
        return CELL_POLL_MS;

    uint32_t now = millis();

    // A modem that has reset or gone silent gets a fresh PWRKEY pulse - the
    // pulse is also mandatory after powerOff(), which leaves the module off.
    bool silent =
        (cellState == CELL_SIM_WAIT || cellState == CELL_REG_WAIT || cellState == CELL_BEARER_UP || cellState == CELL_IP_UP) &&
        (int32_t)(now - cellLastResponse) > (int32_t)CELL_SILENCE_MS;
    if (cellModem->getState() == ATModem::FAILED || silent) {
        cellModem->restart();
        cellLocalIP = "";
        cellRtcSeeded = false;
        cellLastResponse = now;
        simAbsentLogged = false;
        setCellState(CELL_BOOTING);
        return CELL_POLL_MS;
    }

    if (!cellModem->isReady()) {
        setCellState(CELL_BOOTING);
        return CELL_POLL_MS;
    }

    switch (cellState) {
    case CELL_OFF:
    case CELL_BOOTING:
        cellLastResponse = now;
        setCellState(CELL_SIM_WAIT);
        break;

    case CELL_FAILED: {
        // Linear backoff before retrying from registration.
        uint32_t backoff = CELL_POLL_MS * cellFailCount;
        if (backoff > CELL_BACKOFF_MAX_MS)
            backoff = CELL_BACKOFF_MAX_MS;
        cellLastResponse = now;
        setCellState(CELL_REG_WAIT);
        return backoff;
    }

    case CELL_SIM_WAIT:
        // With USIM_CD wired the module reports insertion itself, so waiting
        // costs nothing and a reset would only throw away a working session.
        if (!cellModem->hasSimHotplug() && simAbsent && (uint32_t)(now - simWaitSince) >= simResetBackoff())
            stepSimReset();
        else
            stepSimWait();
        break;

    case CELL_REG_WAIT:
        // Signal readings matter most while registration is not happening, so the sweep
        // gets a tick here too - at the cost of noticing registration one poll later.
        if (cellDiagDue()) {
            cellBusy = true;
            serviceCellDiag();
        } else {
            stepRegWait();
        }
        break;

    case CELL_BEARER_UP:
        // Sub-steps bearerStep through SHUT/CSTT/CIICR/CIFSR; a successful CIFSR
        // moves cellState on to CELL_IP_UP.
        stepBearer();
        break;

    case CELL_IP_UP:
        if (!cellRtcSeeded) {
            cellBusy = true;
            cellModem->submit("AT+CCLK?", 5000, [](ATResult r, const String &resp) {
                cellCommandDone(r);
                if (r == ATResult::Ok && setRtcFromCclk(resp))
                    cellRtcSeeded = true;
            });
        } else if (cellDiagDue()) {
            // One diagnostic command per tick, so only ever one is in flight.
            cellBusy = true;
            serviceCellDiag();
        } else {
            // Liveness plus registration drop detection.
            cellBusy = true;
            cellModem->submit("AT+CEREG?", 5000, [](ATResult r, const String &resp) {
                cellCommandDone(r);
                if (r != ATResult::Ok)
                    return;
                int stat = parseCeregStat(resp.c_str());
                if (stat != 1 && stat != 5) {
                    LOG_WARN("Cell registration lost (CEREG %d)", stat);
                    cellLocalIP = "";
                    setCellState(CELL_REG_WAIT);
                }
            });
        }
        break;
    }

    return CELL_POLL_MS;
}

bool initCellular()
{
    if (cellEvent)
        return true;

    initCellModem();
    cellLastResponse = millis();
    setCellState(CELL_BOOTING);
    cellEvent = new Periodic("cellConnect", reconnectCell);
    return true;
}

#endif
