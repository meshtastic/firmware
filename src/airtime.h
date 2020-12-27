#pragma once

#include "configuration.h"
#include <Arduino.h>
#include <functional>

enum reportTypes { TX_LOG, RX_LOG, RX_ALL_LOG };

void logAirtime(reportTypes reportType, uint32_t airtime_ms);

void airtimeCalculator();

uint8_t currentHourIndex();
uint8_t getHoursToLog();

uint32_t getSecondsSinceBoot();

uint16_t *airtimeReport(reportTypes reportType);