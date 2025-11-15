#include "UIDataState.h"
#include "NodeDB.h"
#include "configuration.h"
#include "PowerFSM.h"
#include "PowerStatus.h"  // For PowerStatus class and extern powerStatus
#include "power.h"         // For PMU direct access
#include <Arduino.h>

using namespace meshtastic;

// Battery voltage to percentage lookup table (Li-ion curve)
// Voltage values in mV for 0%, 10%, 20%... 100%
static const int batteryVoltageLookup[] = {
    3000,  // 0%
    3400,  // 10%
    3550,  // 20%
    3650,  // 30%
    3700,  // 40%
    3750,  // 50%
    3800,  // 60%
    3850,  // 70%
    3920,  // 80%
    4000,  // 90%
    4100   // 100%
};

// Calculate battery percentage from voltage (more reliable than AXP2101 gauge)
static uint8_t __attribute__((used)) voltageToPercent(int voltageMv) {
    // Clamp to valid range
    if (voltageMv >= 4100) return 100;  // Full charge (even if higher when charging)
    if (voltageMv <= 3000) return 0;    // Dead battery
    
    // Linear interpolation between lookup points
    for (int i = 0; i < 10; i++) {
        if (voltageMv <= batteryVoltageLookup[i + 1]) {
            int v1 = batteryVoltageLookup[i];
            int v2 = batteryVoltageLookup[i + 1];
            int pct1 = i * 10;
            int pct2 = (i + 1) * 10;
            return pct1 + ((voltageMv - v1) * (pct2 - pct1)) / (v2 - v1);
        }
    }
    return 100;
}

UIDataState::UIDataState() : systemDataValid(false), nodesDataValid(false) {
    memset(&currentSystemData, 0, sizeof(currentSystemData));
    memset(&currentNodesData, 0, sizeof(currentNodesData));
}

bool UIDataState::updateSystemData() {
    SystemData newData;
    memset(&newData, 0, sizeof(newData));
    
    // Get node information
    auto nodedbp = nodeDB;
    if (nodedbp) {
        meshtastic_NodeInfoLite *myNode = nodedbp->getMeshNodeByIndex(0);
        if (myNode) {
            newData.nodeId = myNode->num;
            strncpy(newData.shortName, myNode->user.short_name, sizeof(newData.shortName) - 1);
            strncpy(newData.longName, myNode->user.long_name, sizeof(newData.longName) - 1);
        }
        newData.nodeCount = nodedbp->getNumMeshNodes();
        newData.isConnected = newData.nodeCount > 1;
    }
    
    // System info
    newData.uptime = millis() / 1000;
    newData.freeHeapKB = ESP.getFreeHeap() / 1024;
    
    // LoRa config
    newData.loraRegion = config.lora.region;
    newData.loraPreset = config.lora.modem_preset;
    
    // Battery info
    updateBatteryInfo(newData);
    
    newData.lastUpdate = millis();
    
    // Check if data has actually changed
    if (!systemDataValid || newData != currentSystemData) {
        currentSystemData = newData;
        systemDataValid = false; // Mark as dirty
        return true; // Data changed
    }
    
    return false; // No change
}

bool UIDataState::updateNodesData() {
    NodesData newData;
    memset(&newData, 0, sizeof(newData));
    
    auto nodedbp = nodeDB;
    if (nodedbp) {
        newData.nodeCount = nodedbp->getNumMeshNodes();
        
        // Temporary storage for filtering
        struct TempNodeInfo {
            uint32_t nodeId;
            uint32_t lastHeard;
            int8_t signalStrength;
            char name[32];
        };
        
        TempNodeInfo tempNodes[33];
        size_t tempCount = 0;
        uint32_t currentTime = millis();
        const uint32_t THREE_HOURS_MS = 3 * 60 * 60 * 1000;
        
        // Collect and filter nodes (last 3 hours)
        for (size_t i = 0; i < newData.nodeCount && tempCount < 33; i++) {
            meshtastic_NodeInfoLite *node = nodedbp->getMeshNodeByIndex(i);
            if (node) {
                // Filter by time (3 hours) or if no RTC, include all
                bool includeNode = true;
                if (node->last_heard > 0) {
                    uint32_t timeSince = (currentTime > node->last_heard) ? 
                                       (currentTime - node->last_heard) : 0;
                    if (timeSince > THREE_HOURS_MS) {
                        includeNode = false;
                    }
                }
                
                if (includeNode) {
                    tempNodes[tempCount].nodeId = node->num;
                    tempNodes[tempCount].lastHeard = node->last_heard;
                    tempNodes[tempCount].signalStrength = node->snr; // Use SNR as signal strength
                    
                    if (strlen(node->user.short_name) > 0) {
                        strncpy(tempNodes[tempCount].name, node->user.short_name, 31);
                    } else {
                        snprintf(tempNodes[tempCount].name, 32, "%08X", node->num);
                    }
                    tempCount++;
                }
            }
        }
        
        // Sort by signal strength (highest first) if we have signal data
        // Otherwise sort by last heard time (most recent first)
        for (size_t i = 0; i < tempCount - 1; i++) {
            for (size_t j = i + 1; j < tempCount; j++) {
                bool shouldSwap = false;
                
                // Primary sort: signal strength (if available)
                if (tempNodes[i].signalStrength != 0 || tempNodes[j].signalStrength != 0) {
                    shouldSwap = tempNodes[i].signalStrength < tempNodes[j].signalStrength;
                } else {
                    // Secondary sort: most recent last heard
                    shouldSwap = tempNodes[i].lastHeard < tempNodes[j].lastHeard;
                }
                
                if (shouldSwap) {
                    TempNodeInfo temp = tempNodes[i];
                    tempNodes[i] = tempNodes[j];
                    tempNodes[j] = temp;
                }
            }
        }
        
        // Copy sorted data to final structure
        newData.filteredNodeCount = tempCount;
        for (size_t i = 0; i < tempCount; i++) {
            newData.nodeIds[i] = tempNodes[i].nodeId;
            newData.lastHeard[i] = tempNodes[i].lastHeard;
            newData.signalStrength[i] = tempNodes[i].signalStrength;
            strncpy(newData.nodeList[i], tempNodes[i].name, 31);
        }
    }
    
    newData.lastNodeUpdate = millis();
    
    // Check if data has actually changed
    if (!nodesDataValid || newData != currentNodesData) {
        currentNodesData = newData;
        nodesDataValid = false; // Mark as dirty
        return true; // Data changed
    }
    
    return false; // No change
}

bool UIDataState::isSystemDataChanged() const {
    return !systemDataValid;
}

bool UIDataState::isNodesDataChanged() const {
    return !nodesDataValid;
}

void UIDataState::invalidateAll() {
    systemDataValid = false;
    nodesDataValid = false;
}

void UIDataState::updateBatteryInfo(SystemData& data) {
    // Use Meshtastic's power management system (powerStatus is in meshtastic namespace)
    // Using the same approach as stock Meshtastic UI (SharedUIDisplay.cpp)
    data.hasBattery = false;
    data.batteryPercent = 0;
    data.batteryVoltageMv = 0;
    data.hasUSB = false;
    data.isCharging = false;
    
    if (powerStatus) {
        // Read values directly from powerStatus (same as stock UI)
        int chargePercent = powerStatus->getBatteryChargePercent();
        data.hasUSB = powerStatus->getHasUSB();
        data.isCharging = powerStatus->getIsCharging();
        data.batteryVoltageMv = powerStatus->getBatteryVoltageMv();
        
        // Handle special case: 101% means external power on boards without USB detection
        if (chargePercent == 101) {
            data.hasUSB = true;
            data.hasBattery = false;
            data.batteryPercent = 0;
        } else if (chargePercent >= 0 && chargePercent <= 100) {
            // Valid battery percentage
            data.hasBattery = powerStatus->getHasBattery();
            data.batteryPercent = chargePercent;
            
            // Don't override charging status - trust the power management system
            // Even at 100%, device may still be trickle charging when USB connected
        } else {
            // Invalid percentage, no battery
            data.hasBattery = false;
            data.batteryPercent = 0;
        }
        
        // Sanity check on voltage - if it's way off, try direct PMU reading
        if (data.batteryVoltageMv < 2500 || data.batteryVoltageMv > 4500) {
            #ifdef HAS_PMU
            if (PMU) {
                int pmuVoltage = PMU->getBattVoltage();
                if (pmuVoltage >= 2500 && pmuVoltage <= 4500) {
                    data.batteryVoltageMv = pmuVoltage;
                    // If percentage is also stuck at 100, calculate from voltage
                    if (data.hasBattery && data.batteryPercent == 100 && pmuVoltage < 4050) {
                        data.batteryPercent = voltageToPercent(pmuVoltage);
                    }
                }
            }
            #endif
        }
        
        // Debug logging to see actual values
        static unsigned long lastLogTime = 0;
        if (millis() - lastLogTime > 30000) { // Log every 30 seconds
            LOG_INFO("🔋 CUSTOM UI Battery: hasBat=%d, hasUSB=%d, charging=%d, pct=%d, mV=%d",
                     data.hasBattery, data.hasUSB, data.isCharging, 
                     data.batteryPercent, data.batteryVoltageMv);
            lastLogTime = millis();
        }
    } else {
        LOG_WARN("🔋 CUSTOM UI: powerStatus is NULL!");
    }
}