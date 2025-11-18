#include "BatteryHelper.h"
#include "PowerStatus.h"
#include "configuration.h"

// Static member initialization
int BatteryHelper::lastBatteryPercent = -1;
bool BatteryHelper::lastChargingState = false;
bool BatteryHelper::initialized = false;

void BatteryHelper::init() {
    // Initialize power management if available
    initialized = true;
}

int BatteryHelper::getBatteryPercent() {
    if (!initialized) {
        init();
    }
    
    // Use the global powerStatus instance
    if (powerStatus) {
        return powerStatus->getBatteryChargePercent();
    }
    
    // Fallback - return 0 if no power status available
    return 0;
}

bool BatteryHelper::hasChanged() {
    int currentPercent = getBatteryPercent();
    bool currentCharging = isCharging();
    
    bool changed = (currentPercent != lastBatteryPercent) || 
                   (currentCharging != lastChargingState);
    
    if (changed) {
        lastBatteryPercent = currentPercent;
        lastChargingState = currentCharging;
    }
    
    return changed;
}

String BatteryHelper::getBatteryString() {
    int percent = getBatteryPercent();
    
    if (percent < 0) {
        return "N/A";
    }
    
    String result = String(percent) + "%";
    
    if (isCharging()) {
        result += "+"; // Indicate charging
    }
    
    return result;
}

bool BatteryHelper::isCharging() {
    // Use the global powerStatus instance
    if (powerStatus) {
        return powerStatus->getIsCharging();
    }
    
    return false;
}