#include "LoRaHelper.h"
#include "configuration.h"
#include "NodeDB.h"
#include "mesh/MeshService.h"
#include "gps/RTC.h" // for getTime() function
#include <algorithm>

// Static member initialization
String LoRaHelper::lastLongName = "";
String LoRaHelper::lastShortName = "";
bool LoRaHelper::initialized = false;

void LoRaHelper::init() {
    initialized = true;
}

String LoRaHelper::getDeviceLongName() {
    if (!initialized) {
        init();
    }
    
    // Get device name from global owner variable
    if (strlen(owner.long_name) > 0) {
        return String(owner.long_name);
    }
    
    // Fallback to default name
    return "Meshtastic";
}

String LoRaHelper::getDeviceShortName() {
    if (!initialized) {
        init();
    }
    
    // Get device short name from global owner variable
    if (strlen(owner.short_name) > 0) {
        return String(owner.short_name);
    }
    
    // Fallback to default
    return "MT";
}

bool LoRaHelper::hasChanged() {
    String currentLong = getDeviceLongName();
    String currentShort = getDeviceShortName();
    
    bool changed = (currentLong != lastLongName) || (currentShort != lastShortName);
    
    if (changed) {
        lastLongName = currentLong;
        lastShortName = currentShort;
    }
    
    return changed;
}

int LoRaHelper::getRSSI() {
    // Get RSSI from radio interface if available
    // This would require access to the radio interface
    return 0; // TODO: Implement RSSI reading
}

int LoRaHelper::getNodeCount() {
    auto nodedbp = nodeDB;
    if (nodedbp) {
        return nodedbp->getNumOnlineMeshNodes();
    }
    return 0;
}

bool LoRaHelper::isLoRaOnline() {
    // Check if mesh service is running
    return service != nullptr;
}

std::vector<NodeInfo> LoRaHelper::getNodesList(int maxNodes, bool includeOffline) {
    std::vector<NodeInfo> nodes;
    
    if (!nodeDB) {
        return nodes; // Return empty list if nodeDB not available
    }
    
    // Get all mesh nodes
    size_t totalNodes = nodeDB->getNumMeshNodes();
    
    for (size_t i = 0; i < totalNodes && nodes.size() < maxNodes; i++) {
        auto meshNode = nodeDB->getMeshNodeByIndex(i);
        if (!meshNode || !meshNode->has_user) {
            continue; // Skip nodes without user info
        }
        
        // Skip our own node
        if (meshNode->num == nodeDB->getNodeNum()) {
            continue;
        }
        
        // Check if node is online
        bool online = isNodeOnline(meshNode->last_heard);
        if (!includeOffline && !online) {
            continue;
        }
        
        NodeInfo nodeInfo;
        nodeInfo.nodeNum = meshNode->num;
        
        // Copy strings to fixed char arrays (no dynamic allocation)
        strncpy(nodeInfo.longName, meshNode->user.long_name, sizeof(nodeInfo.longName) - 1);
        nodeInfo.longName[sizeof(nodeInfo.longName) - 1] = '\0';
        
        strncpy(nodeInfo.shortName, meshNode->user.short_name, sizeof(nodeInfo.shortName) - 1);
        nodeInfo.shortName[sizeof(nodeInfo.shortName) - 1] = '\0';
        
        nodeInfo.lastHeard = meshNode->last_heard;
        nodeInfo.snr = meshNode->snr;
        nodeInfo.signalBars = snrToSignalBars(meshNode->snr);
        nodeInfo.isOnline = online;
        nodeInfo.isFavorite = meshNode->is_favorite;
        nodeInfo.viaInternet = meshNode->via_mqtt;
        nodeInfo.hopsAway = meshNode->has_hops_away ? meshNode->hops_away : 0;
        
        // Use node number as fallback if no long name
        if (strlen(nodeInfo.longName) == 0) {
            snprintf(nodeInfo.longName, sizeof(nodeInfo.longName), "Node %08X", meshNode->num);
        }
        
        // Use first two characters as short name if empty
        if (strlen(nodeInfo.shortName) == 0) {
            if (strlen(nodeInfo.longName) >= 2) {
                strncpy(nodeInfo.shortName, nodeInfo.longName, 2);
                nodeInfo.shortName[2] = '\0';
            } else {
                snprintf(nodeInfo.shortName, sizeof(nodeInfo.shortName), "%02X", meshNode->num & 0xFF);
            }
        }
        
        nodes.push_back(nodeInfo);
    }
    
    // Sort nodes by last heard (most recent first), then by favorites
    std::sort(nodes.begin(), nodes.end(), [](const NodeInfo& a, const NodeInfo& b) {
        // Favorites first
        if (a.isFavorite && !b.isFavorite) return true;
        if (!a.isFavorite && b.isFavorite) return false;
        
        // Online nodes before offline
        if (a.isOnline && !b.isOnline) return true;
        if (!a.isOnline && b.isOnline) return false;
        
        // Then by most recently heard
        return a.lastHeard > b.lastHeard;
    });
    
    return nodes;
}

int LoRaHelper::snrToSignalBars(float snr) {
    // Convert SNR to signal bars (0-4)
    // SNR values: excellent > 10dB, good > 5dB, fair > 0dB, poor > -10dB, very poor <= -10dB
    if (snr >= 10.0f) return 4;        // Excellent
    else if (snr >= 5.0f) return 3;    // Good
    else if (snr >= 0.0f) return 2;    // Fair
    else if (snr >= -10.0f) return 1;  // Poor
    else return 0;                     // Very poor
}

bool LoRaHelper::isNodeOnline(uint32_t lastHeard) {
    if (lastHeard == 0) return false;
    
    uint32_t now = getTime();
    uint32_t elapsed = now - lastHeard;
    
    // Consider online if heard within last 2 hours (7200 seconds)
    return elapsed < 7200;
}