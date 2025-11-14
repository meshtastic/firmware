#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * UI Data container that tracks changes for efficient screen updates
 * Only updates display when actual data changes are detected
 */
class UIDataState {
public:
    struct SystemData {
        uint32_t nodeId;
        char shortName[32];
        char longName[64];
        size_t nodeCount;
        uint32_t uptime;
        uint8_t batteryPercent;
        bool hasBattery;
        uint8_t loraRegion;
        uint8_t loraPreset;
        uint32_t freeHeapKB;
        bool isConnected;
        uint32_t lastUpdate;
        
        bool operator!=(const SystemData& other) const {
            return nodeId != other.nodeId ||
                   strcmp(shortName, other.shortName) != 0 ||
                   nodeCount != other.nodeCount ||
                   batteryPercent != other.batteryPercent ||
                   hasBattery != other.hasBattery ||
                   loraRegion != other.loraRegion ||
                   loraPreset != other.loraPreset ||
                   isConnected != other.isConnected ||
                   ((uptime / 60) != (other.uptime / 60)) || // Only update if minute changed
                   (abs((int)freeHeapKB - (int)other.freeHeapKB) > 10); // Only if significant heap change
        }
    };
    
    struct NodesData {
        size_t nodeCount;
        uint32_t lastNodeUpdate;
        char nodeList[16][32]; // Cache up to 16 node names
        uint32_t nodeIds[16];
        uint32_t lastHeard[16];
        
        bool operator!=(const NodesData& other) const {
            if (nodeCount != other.nodeCount) return true;
            
            for (size_t i = 0; i < nodeCount && i < 16; i++) {
                if (nodeIds[i] != other.nodeIds[i] ||
                    strcmp(nodeList[i], other.nodeList[i]) != 0 ||
                    lastHeard[i] != other.lastHeard[i]) {
                    return true;
                }
            }
            return false;
        }
    };
    
private:
    SystemData currentSystemData;
    NodesData currentNodesData;
    bool systemDataValid;
    bool nodesDataValid;
    
public:
    UIDataState();
    
    // System data management
    bool updateSystemData();
    const SystemData& getSystemData() const { return currentSystemData; }
    bool isSystemDataChanged() const;
    void markSystemDataProcessed() { systemDataValid = true; }
    
    // Nodes data management  
    bool updateNodesData();
    const NodesData& getNodesData() const { return currentNodesData; }
    bool isNodesDataChanged() const;
    void markNodesDataProcessed() { nodesDataValid = true; }
    
    // Force refresh
    void invalidateAll();
    
private:
    void updateBatteryInfo(SystemData& data);
};