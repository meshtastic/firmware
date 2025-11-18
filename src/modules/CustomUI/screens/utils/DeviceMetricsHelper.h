#pragma once

#include <Arduino.h>

/**
 * Device metrics utility helper for CustomUI screens
 * Provides memory utilization and system performance information
 */
class DeviceMetricsHelper {
public:
    static void init();
    
    /**
     * Get current free heap memory in bytes
     * @return free heap size in bytes
     */
    static size_t getFreeHeap();
    
    /**
     * Get total heap size in bytes
     * @return total heap size in bytes
     */
    static size_t getTotalHeap();
    
    /**
     * Get memory utilization percentage (0-100)
     * @return memory usage percentage
     */
    static int getMemoryUtilization();
    
    /**
     * Check if memory metrics have changed significantly since last call
     * @return true if memory info changed
     */
    static bool hasChanged();
    
    /**
     * Get formatted memory utilization string for display
     * @return formatted string like "65%" or "Free: 32KB"
     */
    static String getMemoryString();
    
    /**
     * Get detailed memory information string
     * @return detailed string like "Used: 128KB/256KB (50%)"
     */
    static String getDetailedMemoryString();
    
    /**
     * Get minimum free heap size since boot
     * @return minimum free heap in bytes
     */
    static size_t getMinFreeHeap();

private:
    static size_t lastFreeHeap;
    static int lastMemoryPercent;
    static size_t minFreeHeapSeen;
    static bool initialized;
    
    // Threshold for considering memory change significant (in bytes)
    static const size_t MEMORY_CHANGE_THRESHOLD = 1024;
};