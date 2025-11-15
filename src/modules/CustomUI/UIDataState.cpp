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
static uint8_t voltageToPercent(int voltageMv) {
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
        
        // Cache node information for up to 16 nodes
        size_t maxNodes = min(newData.nodeCount, (size_t)16);
        for (size_t i = 0; i < maxNodes; i++) {
            meshtastic_NodeInfoLite *node = nodedbp->getMeshNodeByIndex(i);
            if (node) {
                newData.nodeIds[i] = node->num;
                newData.lastHeard[i] = node->last_heard;
                
                if (strlen(node->user.short_name) > 0) {
                    strncpy(newData.nodeList[i], node->user.short_name, 31);
                } else {
                    snprintf(newData.nodeList[i], 32, "%08X", node->num);
                }
            }
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
            
            // Stop charging indicator at 100%
            if (chargePercent >= 100) {
                data.isCharging = false;
            }
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