#include "mesh/cell/cellDiag.h"

#if HAS_CELLULAR

#include "mesh/Throttle.h"
#include "mesh/cell/cellDiagParse.h"
#include <string.h>

#define CELL_DIAG_INTERVAL_MS 30000

// Smallest RSRP change worth a log line, so a parked modem stays quiet.
#define CELL_DIAG_RSRP_DELTA_DB 3

static CellDiag diag;
static CellDiag lastLogged;
static bool everLogged = false;

// 0 when no sweep is in progress, otherwise the current step. Fetching the operator takes up
// to two round trips: a name is tried first, falling back to the numeric MCC/MNC only on an empty result.
enum {
    STEP_CESQ = 1,
    STEP_COPS_SET_NAME,
    STEP_COPS_QUERY_NAME,
    STEP_COPS_SET_NUMERIC,
    STEP_COPS_QUERY_NUMERIC,
    STEP_BAND,
    STEP_VBAT,
};
static uint8_t sweepStep = 0;
static uint32_t lastSweepMs = 0; // 0 means "never swept", which is always due

const CellDiag &getCellDiag()
{
    return diag;
}

bool cellDiagDue()
{
    return sweepStep != 0 || lastSweepMs == 0 || !Throttle::isWithinTimespanMs(lastSweepMs, CELL_DIAG_INTERVAL_MS);
}

void cellDiagInvalidate()
{
    diag = CellDiag();
    sweepStep = 0;
    lastSweepMs = 0; // sweep as soon as a state that polls is reached
}

static bool worthLogging()
{
    if (!everLogged)
        return true;
    if (diag.rsrpValid != lastLogged.rsrpValid || diag.bandValid != lastLogged.bandValid)
        return true;
    if (diag.rsrpValid && abs(diag.rsrp - lastLogged.rsrp) >= CELL_DIAG_RSRP_DELTA_DB)
        return true;
    if (diag.bandValid && (diag.band != lastLogged.band || diag.act != lastLogged.act))
        return true;
    return strcmp(diag.operatorName, lastLogged.operatorName) != 0;
}

static void finishSweep()
{
    diag.lastUpdatedMs = millis();
    sweepStep = 0;
    lastSweepMs = diag.lastUpdatedMs;

    if (!worthLogging())
        return;

    char signal[32] = "signal n/a";
    if (diag.rsrpValid)
        snprintf(signal, sizeof(signal), "RSRP %d dBm RSRQ %.1f dB", diag.rsrp, diag.rsrq);

    char band[24] = "";
    if (diag.bandValid)
        snprintf(band, sizeof(band), " band %u AcT %u", (unsigned)diag.band, (unsigned)diag.act);

    char vbat[24] = "";
    if (diag.vbatValid)
        snprintf(vbat, sizeof(vbat), " VBAT %umV", (unsigned)diag.vbatMv);

    LOG_INFO("Cell diag: %s%s%s operator %s", signal, band, vbat, diag.operatorName[0] ? diag.operatorName : "unknown");

    lastLogged = diag;
    everLogged = true;
}

void serviceCellDiag()
{
    if (sweepStep == 0)
        sweepStep = STEP_CESQ;

    switch (sweepStep) {
    case STEP_CESQ:
        cellModem->submit("AT+CESQ", 5000, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            diag.rsrpValid = (r == ATResult::Ok) && parseCesq(resp.c_str(), &diag.rsrp, &diag.rsrq);
            sweepStep = STEP_COPS_SET_NAME;
        });
        break;

    case STEP_COPS_SET_NAME:
        cellModem->submit("AT+COPS=3,0", 5000, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            sweepStep = STEP_COPS_QUERY_NAME;
        });
        break;

    case STEP_COPS_QUERY_NAME:
        cellModem->submit("AT+COPS?", 5000, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            // A network absent from the modem's PLMN table answers with an empty name;
            // fall back to the numeric MCC/MNC rather than showing nothing.
            if (r == ATResult::Ok && parseCopsName(resp.c_str(), diag.operatorName, sizeof(diag.operatorName)))
                sweepStep = STEP_BAND;
            else
                sweepStep = STEP_COPS_SET_NUMERIC;
        });
        break;

    case STEP_COPS_SET_NUMERIC:
        cellModem->submit("AT+COPS=3,2", 5000, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            sweepStep = STEP_COPS_QUERY_NUMERIC;
        });
        break;

    case STEP_COPS_QUERY_NUMERIC:
        cellModem->submit("AT+COPS?", 5000, [](ATResult r, const String &resp) {
            cellCommandDone(r);
            if (r != ATResult::Ok || !parseCopsNumeric(resp.c_str(), diag.operatorName, sizeof(diag.operatorName)))
                diag.operatorName[0] = '\0';
            sweepStep = STEP_BAND;
        });
        break;

    case STEP_BAND: {
        bool asked = cellModem->queryBand([](ATResult r, bool valid, uint8_t band, uint8_t act) {
            cellCommandDone(r);
            diag.bandValid = valid;
            diag.band = band;
            diag.act = act;
            sweepStep = STEP_VBAT;
        });
        // A modem with no band reporting skips the step rather than stalling.
        if (!asked) {
            diag.bandValid = false;
            sweepStep = STEP_VBAT;
            serviceCellDiag();
        }
        break;
    }

    case STEP_VBAT: {
        bool asked = cellModem->queryBatteryMv([](ATResult r, bool valid, uint16_t mv) {
            cellCommandDone(r);
            diag.vbatValid = valid;
            diag.vbatMv = mv;
            finishSweep();
        });
        if (!asked) {
            diag.vbatValid = false;
            finishSweep();
        }
        break;
    }
    }
}

#endif
