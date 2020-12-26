#pragma once

#include "configuration.h"
#include <Arduino.h>
#include <functional>

enum reportTypes { TX_LOG, RX_LOG, RX_ALL_LOG };

void logAirtime(reportTypes reportType, uint32_t airtime_ms);

void currentHourIndexReset();
uint8_t currentHourIndex();

uint32_t secondsSinceBoot();

uint16_t *airtimeReport(reportTypes reportType);