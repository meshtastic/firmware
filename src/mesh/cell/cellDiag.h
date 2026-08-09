#pragma once

#include "configuration.h"

#if HAS_CELLULAR

#include "mesh/cell/ATModem.h"
#include <Arduino.h>

// Modem health, refreshed by a periodic sweep of the diagnostic AT commands. Every field carries
// its own validity flag: a modem that is registered but answers CBC with ERROR still has a usable RSRP.
struct CellDiag {
    int16_t rsrp = 0; // dBm
    float rsrq = 0;   // dB
    uint8_t band = 0; // E-UTRA band number
    uint8_t act = 0;  // access technology, 7 = E-UTRAN
    uint16_t vbatMv = 0;
    char operatorName[24] = {0}; // PLMN name, or the numeric MCC/MNC when unresolved
    bool rsrpValid = false;
    bool bandValid = false;
    bool vbatValid = false;
    uint32_t lastUpdatedMs = 0;
};

// Latest readings, whatever the sweep has gathered so far.
const CellDiag &getCellDiag();

// Advance the diagnostic sweep by one command. Called from reconnectCell() where a bring-up
// command would otherwise be issued, so only one AT command is ever in flight.
void serviceCellDiag();

// True when a sweep is due; the caller uses this to decide whether to spend
// this tick on diagnostics instead of its own liveness poll.
bool cellDiagDue();

// Discard every reading and abandon any sweep in progress. Called when the link drops below
// REG_WAIT, where no sweep runs: without this the last readings would keep rendering as though current.
void cellDiagInvalidate();

// Implemented in cellBearer.cpp: notes modem liveness and releases the
// single-command-in-flight flag. Shared so diagnostics take part in both.
void cellCommandDone(ATResult r);

#endif
