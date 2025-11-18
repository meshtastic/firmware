#pragma once

#include <Arduino.h>

/**
 * Battery utility helper for CustomUI screens
 * Provides battery percentage and status information
 */
class BatteryHelper {
public:
    static void init();
    
    /**
     * Get current battery percentage (0-100)
     * @return battery percentage, -1 if unavailable
     */
    static int getBatteryPercent();
    
    /**
     * Check if battery information has changed since last call
     * @return true if battery info changed
     */
    static bool hasChanged();
    
    /**
     * Get formatted battery string for display
     * @return formatted string like "85%" or "N/A"
     */
    static String getBatteryString();
    
    /**
     * Check if device is charging
     * @return true if charging
     */
    static bool isCharging();

private:
    static int lastBatteryPercent;
    static bool lastChargingState;
    static bool initialized;
};