#include "DeviceMetricsHelper.h"
#include "configuration.h"

#ifdef ARCH_ESP32
#include <esp_heap_caps.h>
#endif

// Static member initialization
size_t DeviceMetricsHelper::lastFreeHeap = 0;
int DeviceMetricsHelper::lastMemoryPercent = -1;
size_t DeviceMetricsHelper::minFreeHeapSeen = SIZE_MAX;
bool DeviceMetricsHelper::initialized = false;

void DeviceMetricsHelper::init() {
    if (initialized) return;
    
    lastFreeHeap = getFreeHeap();
    lastMemoryPercent = getMemoryUtilization();
    minFreeHeapSeen = lastFreeHeap;
    initialized = true;
    
    LOG_INFO("DeviceMetricsHelper initialized - Free heap: %zu bytes", lastFreeHeap);
}

size_t DeviceMetricsHelper::getFreeHeap() {
#ifdef ARCH_ESP32
    return ESP.getFreeHeap();
#else
    // For other architectures, try to use available memory functions
    // This is a fallback implementation
    return 0;
#endif
}

size_t DeviceMetricsHelper::getTotalHeap() {
#ifdef ARCH_ESP32
    return ESP.getHeapSize();
#else
    // For other architectures, return a reasonable default
    return 320000; // 320KB typical for ESP32
#endif
}

int DeviceMetricsHelper::getMemoryUtilization() {
    size_t freeHeap = getFreeHeap();
    size_t totalHeap = getTotalHeap();
    
    if (totalHeap == 0) return 0;
    
    size_t usedHeap = totalHeap - freeHeap;
    int utilization = (int)((usedHeap * 100) / totalHeap);
    
    // Ensure utilization is within 0-100 range
    if (utilization < 0) utilization = 0;
    if (utilization > 100) utilization = 100;
    
    return utilization;
}

bool DeviceMetricsHelper::hasChanged() {
    if (!initialized) {
        init();
        return true;
    }
    
    size_t currentFreeHeap = getFreeHeap();
    int currentMemoryPercent = getMemoryUtilization();
    
    // Check if there's a significant change in memory
    bool changed = false;
    
    // Check for significant heap change
    if (abs((long)(currentFreeHeap - lastFreeHeap)) > MEMORY_CHANGE_THRESHOLD) {
        changed = true;
    }
    
    // Check for percentage change
    if (abs(currentMemoryPercent - lastMemoryPercent) > 2) { // 2% threshold
        changed = true;
    }
    
    if (changed) {
        lastFreeHeap = currentFreeHeap;
        lastMemoryPercent = currentMemoryPercent;
    }
    
    // Update minimum heap tracking
    if (currentFreeHeap < minFreeHeapSeen) {
        minFreeHeapSeen = currentFreeHeap;
    }
    
    return changed;
}

String DeviceMetricsHelper::getMemoryString() {
    int utilization = getMemoryUtilization();
    return String(utilization) + "%";
}

String DeviceMetricsHelper::getDetailedMemoryString() {
    size_t freeHeap = getFreeHeap();
    size_t totalHeap = getTotalHeap();
    size_t usedHeap = totalHeap - freeHeap;
    int utilization = getMemoryUtilization();
    
    String result = "Used: ";
    
    // Format used memory
    if (usedHeap >= 1024) {
        result += String(usedHeap / 1024) + "KB";
    } else {
        result += String(usedHeap) + "B";
    }
    
    result += "/";
    
    // Format total memory
    if (totalHeap >= 1024*1024) {
        result += String(totalHeap / (1024*1024)) + "MB";
    } else if (totalHeap >= 1024) {
        result += String(totalHeap / 1024) + "KB";
    } else {
        result += String(totalHeap) + "B";
    }
    
    result += " (" + String(utilization) + "%)";
    
#if defined(CONFIG_SPIRAM_SUPPORT) && defined(BOARD_HAS_PSRAM)
    if (ESP.getPsramSize() > 0) {
        result += " +PSRAM";
    }
#endif
    
    return result;
}

size_t DeviceMetricsHelper::getMinFreeHeap() {
#ifdef ARCH_ESP32
    return ESP.getMinFreeHeap();
#else
    return minFreeHeapSeen;
#endif
}