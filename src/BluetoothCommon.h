#pragma once

/**
 * Common lib functions for all platforms that have bluetooth
 */

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level);