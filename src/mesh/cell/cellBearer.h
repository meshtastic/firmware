#pragma once

#include "configuration.h"
#include <Arduino.h>

#if HAS_CELLULAR

enum CellState { CELL_OFF, CELL_BOOTING, CELL_SIM_WAIT, CELL_REG_WAIT, CELL_BEARER_UP, CELL_IP_UP, CELL_FAILED };

// Power up the modem and start the bearer state machine.
bool initCellular();

// True once a PDP context is active and an IPv4 address is assigned.
bool isCellularAvailable();

CellState getCellState();
const char *getCellStateName(CellState s);

// Assigned IPv4 address, empty string until the bearer is up.
const String &getCellLocalIP();

// Backed by config.network.cell_enabled; setCellularEnabled() persists the change. Only
// meaningful with PIN_MODEM_PWRKEY or PIN_MODEM_EN wired - without either, it refuses to act.
bool isCellularEnabled();
void setCellularEnabled(bool enabled);

#endif
