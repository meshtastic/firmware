#pragma once

#include "configuration.h"
#include <Arduino.h>

#if HAS_CELLULAR

// Optional: with no CELL_APN the bearer sends an empty AT+CSTT and the network
// picks the subscription default. Belongs in NetworkConfig once protobufs land.
#ifdef CELL_APN
#ifndef CELL_APN_USER
#define CELL_APN_USER ""
#endif
#ifndef CELL_APN_PASS
#define CELL_APN_PASS ""
#endif
#endif

enum CellState { CELL_OFF, CELL_BOOTING, CELL_SIM_WAIT, CELL_REG_WAIT, CELL_BEARER_UP, CELL_IP_UP, CELL_FAILED };

// Power up the modem and start the bearer state machine.
bool initCellular();

// True once a PDP context is active and an IPv4 address is assigned.
bool isCellularAvailable();

CellState getCellState();
const char *getCellStateName(CellState s);

// Assigned IPv4 address, empty string until the bearer is up.
const String &getCellLocalIP();

// Runtime power state of the modem, not persisted: a reboot brings it back up. Only meaningful
// with PIN_MODEM_PWRKEY or PIN_MODEM_EN wired - without either, setCellularEnabled() refuses to act.
bool isCellularEnabled();
void setCellularEnabled(bool enabled);

#endif
