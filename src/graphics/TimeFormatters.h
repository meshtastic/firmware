#pragma once

#include "configuration.h"
#include "gps/RTC.h"
#include <airtime.h>
#include <cstdint>

/**
 * Convert a delta in seconds ago to timestamp information (hours, minutes, days ago).
 *
 * @param secondsAgo Number of seconds ago to convert
 * @param hours Pointer to store the hours (0-23)
 * @param minutes Pointer to store the minutes (0-59)
 * @param daysAgo Pointer to store the number of days ago
 * @return true if conversion was successful, false if invalid input or time not available
 */
bool deltaToTimestamp(uint32_t secondsAgo, uint8_t *hours, uint8_t *minutes, int32_t *daysAgo);

/**
 * Get a human-readable string representing the time ago in a format like "2 days, 3 hours, 15 minutes".
 *
 * @param agoSecs Number of seconds ago to convert
 * @param timeStr Pointer to store the resulting string
 * @param maxLength Maximum length of the resulting string buffer
 */
void getTimeAgoStr(uint32_t agoSecs, char *timeStr, uint8_t maxLength);
