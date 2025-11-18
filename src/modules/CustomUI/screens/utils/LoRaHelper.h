#pragma once

#include <Arduino.h>
#include <vector>

/**
 * Information about a mesh node for display purposes
 */
struct NodeInfo {
    uint32_t nodeNum;           // Node number
    String longName;            // Long name (display name)
    String shortName;           // Short name
    uint32_t lastHeard;         // When we last heard from this node (Unix timestamp)
    float snr;                  // Signal-to-noise ratio in dB
    int signalBars;             // Signal strength as bars (0-4)
    bool isOnline;              // Is node considered online
    bool isFavorite;            // Is node marked as favorite
    bool viaInternet;           // Heard via internet/MQTT
    uint8_t hopsAway;           // Number of hops away
};

/**
 * LoRa utility helper for CustomUI screens
 * Provides device name and LoRa status information
 */
class LoRaHelper {
public:
    static void init();
    
    /**
     * Get device long name
     * @return device long name string
     */
    static String getDeviceLongName();
    
    /**
     * Get device short name
     * @return device short name string
     */
    static String getDeviceShortName();
    
    /**
     * Check if device name has changed since last call
     * @return true if device name changed
     */
    static bool hasChanged();
    
    /**
     * Get LoRa signal strength (RSSI)
     * @return RSSI value, 0 if unavailable
     */
    static int getRSSI();
    
    /**
     * Get number of connected nodes
     * @return node count
     */
    static int getNodeCount();
    
    /**
     * Check if LoRa radio is online
     * @return true if LoRa is active
     */
    static bool isLoRaOnline();

    /**
     * Get list of mesh nodes with their information
     * @param maxNodes Maximum number of nodes to return (default 15)
     * @param includeOffline Include offline nodes in the list
     * @return vector of NodeInfo structures
     */
    static std::vector<NodeInfo> getNodesList(int maxNodes = 15, bool includeOffline = true);

    /**
     * Convert SNR to signal bars (0-4)
     * @param snr Signal-to-noise ratio in dB
     * @return number of signal bars (0-4)
     */
    static int snrToSignalBars(float snr);

    /**
     * Convert time since last heard to online status
     * @param lastHeard Unix timestamp of last heard
     * @return true if node is considered online
     */
    static bool isNodeOnline(uint32_t lastHeard);

private:
    static String lastLongName;
    static String lastShortName;
    static bool initialized;
};