#pragma once

#include <WiFi.h>
#include <vector>
#include <String.h>

/**
 * WiFi Helper for network scanning and management
 * Handles WiFi operations for CustomUI module
 */

struct WiFiNetworkInfo {
    String ssid;
    String security;
    int32_t rssi;
    uint8_t channel;
    bool isOpen;
    
    WiFiNetworkInfo() : rssi(0), channel(0), isOpen(false) {}
    WiFiNetworkInfo(const String& s, const String& sec, int32_t r, uint8_t ch, bool open) 
        : ssid(s), security(sec), rssi(r), channel(ch), isOpen(open) {}
};

class WiFiHelper {
public:
    WiFiHelper();
    ~WiFiHelper();
    
    /**
     * Scan for available WiFi networks
     * @param maxNetworks Maximum number of networks to return (default 15)
     * @return Vector of WiFi network information, sorted by signal strength
     */
    std::vector<WiFiNetworkInfo> scanNetworks(int maxNetworks = 15);
    
    /**
     * Start async WiFi scan (non-blocking)
     */
    void startAsyncScan();
    
    /**
     * Check if async scan is complete
     * @return true if scan is finished
     */
    bool isScanComplete();
    
    /**
     * Get results from completed async scan
     * @param maxNetworks Maximum number of networks to return
     * @return Vector of WiFi network information, sorted by signal strength
     */
    std::vector<WiFiNetworkInfo> getAsyncScanResults(int maxNetworks = 15);
    
    /**
     * Get signal strength description
     * @param rssi Signal strength in dBm
     * @return Signal strength as text (Excellent/Good/Fair/Weak)
     */
    String getSignalStrength(int32_t rssi);
    
    /**
     * Get signal strength bars (1-4)
     * @param rssi Signal strength in dBm
     * @return Number of bars (1-4)
     */
    int getSignalBars(int32_t rssi);
    
    /**
     * Get security type as readable string
     * @param authMode WiFi authentication mode
     * @return Security type string
     */
    String getSecurityType(wifi_auth_mode_t authMode);
    
    /**
     * Check if WiFi is currently connected
     * @return true if connected
     */
    bool isConnected();
    
    /**
     * Get current connected SSID
     * @return SSID name or empty string if not connected
     */
    String getCurrentSSID();
    
    /**
     * Get current IP address
     * @return IP address as string or empty if not connected
     */
    String getCurrentIP();

private:
    /**
     * Sort networks by signal strength (strongest first)
     */
    void sortNetworksBySignal(std::vector<WiFiNetworkInfo>& networks);
    
    /**
     * Process scan results into WiFiNetworkInfo vector
     */
    std::vector<WiFiNetworkInfo> processNetworks(int networkCount, int maxNetworks);
    
    /**
     * Compare networks by signal strength (for sorting)
     */
    static bool compareBySignalStrength(const WiFiNetworkInfo& a, const WiFiNetworkInfo& b);
    
    bool asyncScanInProgress = false;
    
    /**
     * Remove duplicate SSIDs, keeping the strongest signal
     */
    void removeDuplicates(std::vector<WiFiNetworkInfo>& networks);
};