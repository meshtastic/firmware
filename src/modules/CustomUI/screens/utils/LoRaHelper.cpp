#include "LoRaHelper.h"
#include "DataStore.h"
#include "configuration.h"
#include "NodeDB.h"
#include "mesh/MeshService.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "gps/RTC.h" // for getTime() function
#include <algorithm>

// External references with safe access patterns
extern meshtastic_DeviceState devicestate;
extern MeshService *service;

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

MessageInfo LoRaHelper::getLastReceivedMessage() {
    // Try to get the latest message from DataStore first
    MessageInfo storeMessage = DataStore::getInstance().getLatestMessage();
    if (storeMessage.isValid) {
        return storeMessage;
    }
    
    // Fallback to device state if no stored messages
    MessageInfo info;
    
    // Check if devicestate has a valid text message
    if (!devicestate.has_rx_text_message || 
        devicestate.rx_text_message.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP ||
        devicestate.rx_text_message.decoded.payload.size == 0) {
        return info; // Returns invalid message
    }
    
    const auto& packet = devicestate.rx_text_message;
    
    // Extract message text
    size_t textLen = std::min((size_t)packet.decoded.payload.size, sizeof(info.text) - 1);
    memcpy(info.text, packet.decoded.payload.bytes, textLen);
    info.text[textLen] = '\0';
    
    // Set message properties
    info.timestamp = packet.rx_time;
    info.senderNodeId = packet.from;
    info.toNodeId = packet.to;
    info.channelIndex = packet.channel;
    info.isOutgoing = (packet.from == 0 || (nodeDB && packet.from == nodeDB->getNodeNum()));
    
    // Determine if this is a direct message
    // Direct message: packet.to is our node ID (not broadcast)
    info.isDirectMessage = (nodeDB && packet.to == nodeDB->getNodeNum() && packet.to != NODENUM_BROADCAST);
    
    // Format channel name for channel messages
    if (!info.isDirectMessage) {
        if (info.channelIndex == 0) {
            strncpy(info.channelName, "Primary", sizeof(info.channelName) - 1);
        } else {
            snprintf(info.channelName, sizeof(info.channelName), "CH%d", info.channelIndex);
        }
    } else {
        strcpy(info.channelName, "DM");
    }
    info.channelName[sizeof(info.channelName) - 1] = '\0';
    
    info.isValid = true;
    
    // Format sender name
    String senderName = formatSenderName(info.senderNodeId, info.isOutgoing);
    strncpy(info.senderName, senderName.c_str(), sizeof(info.senderName) - 1);
    info.senderName[sizeof(info.senderName) - 1] = '\0';
    
    return info;
}

std::vector<MessageInfo> LoRaHelper::getRecentMessages(int maxMessages) {
    // Get messages from DataStore
    std::vector<MessageInfo> messages = DataStore::getInstance().getRecentMessages(maxMessages);
    
    LOG_DEBUG("🔧 LORAHELPER: Retrieved %d messages from DataStore", messages.size());
    
    // If we have no real messages, add a few mock messages for testing the UI
    // This can be removed in production once message flow is established
    if (messages.empty()) {
        LOG_DEBUG("🔧 LORAHELPER: No stored messages, creating mock data for UI testing");
        
        uint32_t currentTime = getTime();
        if (currentTime == 0) currentTime = millis() / 1000;
        
        // Mock DM
        MessageInfo mockDM;
        strcpy(mockDM.text, "Hey, are you there? This is a test direct message to see scrolling");
        strcpy(mockDM.senderName, "Alice");
        strcpy(mockDM.channelName, "DM");
        mockDM.timestamp = currentTime - 300; // 5 minutes ago
        mockDM.senderNodeId = 0x12345678;
        mockDM.toNodeId = (nodeDB ? nodeDB->getNodeNum() : 0);
        mockDM.channelIndex = 0;
        mockDM.isOutgoing = false;
        mockDM.isDirectMessage = true;
        mockDM.isValid = true;
        messages.push_back(mockDM);
        
        // Mock Channel message
        MessageInfo mockChannel;
        strcpy(mockChannel.text, "Anyone seen the weather report? It's looking pretty cloudy today");
        strcpy(mockChannel.senderName, "Bob");
        strcpy(mockChannel.channelName, "Primary");
        mockChannel.timestamp = currentTime - 600; // 10 minutes ago
        mockChannel.senderNodeId = 0x87654321;
        mockChannel.toNodeId = NODENUM_BROADCAST;
        mockChannel.channelIndex = 0;
        mockChannel.isOutgoing = false;
        mockChannel.isDirectMessage = false;
        mockChannel.isValid = true;
        messages.push_back(mockChannel);
        
        // Mock outgoing message
        MessageInfo mockOutgoing;
        strcpy(mockOutgoing.text, "Roger that, I'll check it out. Thanks for the heads up!");
        strcpy(mockOutgoing.senderName, "You");
        strcpy(mockOutgoing.channelName, "Primary");
        mockOutgoing.timestamp = currentTime - 900; // 15 minutes ago
        mockOutgoing.senderNodeId = (nodeDB ? nodeDB->getNodeNum() : 0);
        mockOutgoing.toNodeId = NODENUM_BROADCAST;
        mockOutgoing.channelIndex = 0;
        mockOutgoing.isOutgoing = true;
        mockOutgoing.isDirectMessage = false;
        mockOutgoing.isValid = true;
        messages.push_back(mockOutgoing);
        
        // Mock old DM
        MessageInfo mockOldDM;
        strcpy(mockOldDM.text, "Short msg");
        strcpy(mockOldDM.senderName, "Charlie");
        strcpy(mockOldDM.channelName, "DM");
        mockOldDM.timestamp = currentTime - 1800; // 30 minutes ago
        mockOldDM.senderNodeId = 0xABCDEF12;
        mockOldDM.toNodeId = (nodeDB ? nodeDB->getNodeNum() : 0);
        mockOldDM.channelIndex = 0;
        mockOldDM.isOutgoing = false;
        mockOldDM.isDirectMessage = true;
        mockOldDM.isValid = true;
        messages.push_back(mockOldDM);
    }
    
    return messages;
}

String LoRaHelper::formatSenderName(uint32_t nodeId, bool isOutgoing) {
    if (isOutgoing) {
        return "You";
    }
    
    // Look up node in NodeDB
    if (nodeDB) {
        const auto* node = nodeDB->getMeshNode(nodeId);
        if (node && node->has_user) {
            // Try long name first, fallback to short name
            if (strlen(node->user.long_name) > 0) {
                return String(node->user.long_name);
            } else if (strlen(node->user.short_name) > 0) {
                return String(node->user.short_name);
            }
        }
    }
    
    // Fallback to node ID
    return String("Node !") + String(nodeId & 0xFF, HEX);
}

String LoRaHelper::formatTimeAgo(uint32_t timestamp) {
    if (timestamp == 0) {
        return "Unknown";
    }
    
    // Get current time
    uint32_t currentTime = getTime();
    if (currentTime == 0) {
        // If no valid RTC time, use millis as approximation
        currentTime = millis() / 1000;
    }
    
    if (timestamp > currentTime) {
        // Future timestamp, probably invalid
        return "Unknown";
    }
    
    uint32_t secondsAgo = currentTime - timestamp;
    
    if (secondsAgo < 60) {
        return String(secondsAgo) + "s ago";
    } else if (secondsAgo < 3600) {
        return String(secondsAgo / 60) + "m ago";
    } else if (secondsAgo < 86400) {
        return String(secondsAgo / 3600) + "h ago";
    } else {
        return String(secondsAgo / 86400) + "d ago";
    }
}