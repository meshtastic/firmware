#include "UIDataState.h"
#include "NodeDB.h"
#include "configuration.h"
#include "PowerFSM.h"
#include "PowerStatus.h"  // For PowerStatus class and extern powerStatus
#include <Arduino.h>

// Note: powerStatus is declared extern in PowerStatus.h in meshtastic namespace

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
    data.hasBattery = false;
    data.batteryPercent = 0;
    
    if (powerStatus) {
        data.hasBattery = powerStatus->getHasBattery();
        if (data.hasBattery) {
            data.batteryPercent = powerStatus->getBatteryChargePercent();
            // Ensure we have a valid percentage
            if (data.batteryPercent > 100) {
                data.batteryPercent = 100;
            }
        }
    }
    
    // If powerStatus not available or no battery detected, assume external power
    if (!data.hasBattery) {
        data.hasBattery = false;
        data.batteryPercent = 0; // External power, no battery percentage
    }
}