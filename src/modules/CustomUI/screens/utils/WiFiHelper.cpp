#include "WiFiHelper.h"
#include <WiFi.h>
#include <algorithm>
#include <Arduino.h>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

WiFiHelper::WiFiHelper() {
    LOG_INFO("📶 WiFiHelper: Constructor");
}

WiFiHelper::~WiFiHelper() {
    LOG_INFO("📶 WiFiHelper: Destructor");
}

std::vector<WiFiNetworkInfo> WiFiHelper::scanNetworks(int maxNetworks) {
    LOG_INFO("📶 WiFiHelper: Scanning for WiFi networks...");
    
    // Start WiFi in station mode if not already started
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        delay(100);
    }
    
    // Perform scan
    int networkCount = WiFi.scanNetworks();
    return processNetworks(networkCount, maxNetworks);
}

void WiFiHelper::startAsyncScan() {
    LOG_INFO("📶 WiFiHelper: Starting async WiFi scan...");
    
    // Start WiFi in station mode if not already started
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        delay(100);
    }
    
    WiFi.scanNetworks(true, false, false, 300);
    asyncScanInProgress = true;
}

bool WiFiHelper::isScanComplete() {
    if (!asyncScanInProgress) {
        return false;
    }
    
    int result = WiFi.scanComplete();
    return result != WIFI_SCAN_RUNNING;
}

std::vector<WiFiNetworkInfo> WiFiHelper::getAsyncScanResults(int maxNetworks) {
    if (asyncScanInProgress) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            asyncScanInProgress = false;
            return processNetworks(n, maxNetworks);
        }
    }
    return std::vector<WiFiNetworkInfo>();
}

std::vector<WiFiNetworkInfo> WiFiHelper::processNetworks(int networkCount, int maxNetworks) {
    std::vector<WiFiNetworkInfo> networks;
    
    if (networkCount == 0) {
        LOG_INFO("📶 WiFiHelper: No networks found");
        return networks;
    }
    
    LOG_INFO("📶 WiFiHelper: Found %d networks", networkCount);
    
    // Collect network information without String allocations
    for (int i = 0; i < networkCount && networks.size() < maxNetworks; i++) {
        // Get SSID directly as c_str to avoid String allocation
        const char* ssidStr = WiFi.SSID(i).c_str();
        
        // Skip hidden networks (empty SSID)
        if (strlen(ssidStr) == 0) {
            continue;
        }
        
        // Skip very weak signals (below -90 dBm)
        int32_t rssi = WiFi.RSSI(i);
        if (rssi < -90) {
            continue;
        }
        
        wifi_auth_mode_t authMode = WiFi.encryptionType(i);
        uint8_t channel = WiFi.channel(i);
        bool isOpen = (authMode == WIFI_AUTH_OPEN);
        
        // Create WiFiNetworkInfo directly with char arrays
        WiFiNetworkInfo networkInfo;
        
        // Copy SSID to fixed buffer
        strncpy(networkInfo.ssid, ssidStr, sizeof(networkInfo.ssid) - 1);
        networkInfo.ssid[sizeof(networkInfo.ssid) - 1] = '\0';
        
        // Copy security type to fixed buffer
        const char* securityStr = getSecurityTypeCStr(authMode);
        strncpy(networkInfo.security, securityStr, sizeof(networkInfo.security) - 1);
        networkInfo.security[sizeof(networkInfo.security) - 1] = '\0';
        
        networkInfo.rssi = rssi;
        networkInfo.channel = channel;
        networkInfo.isOpen = isOpen;
        
        networks.push_back(networkInfo);
    }
    
    // Remove duplicates (keep strongest signal)
    removeDuplicates(networks);
    
    // Sort by signal strength (strongest first)
    sortNetworksBySignal(networks);
    
    // Limit to maxNetworks
    if (networks.size() > maxNetworks) {
        networks.resize(maxNetworks);
    }
    
    LOG_INFO("📶 WiFiHelper: Returning %d unique networks", networks.size());
    
    // Clean up scan results
    WiFi.scanDelete();
    
    return networks;
}

String WiFiHelper::getSignalStrength(int32_t rssi) {
    if (rssi > -50) return "Excellent";
    if (rssi > -65) return "Good";
    if (rssi > -75) return "Fair";
    return "Weak";
}

int WiFiHelper::getSignalBars(int32_t rssi) {
    if (rssi > -50) return 4;  // Excellent
    if (rssi > -65) return 3;  // Good
    if (rssi > -75) return 2;  // Fair
    return 1;                  // Weak
}

String WiFiHelper::getSecurityType(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        default: return "Unknown";
    }
}

const char* WiFiHelper::getSecurityTypeCStr(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        default: return "Unknown";
    }
}

bool WiFiHelper::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiHelper::getCurrentSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return "";
}

String WiFiHelper::getCurrentIP() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "";
}

bool WiFiHelper::compareBySignalStrength(const WiFiNetworkInfo& a, const WiFiNetworkInfo& b) {
    return a.rssi > b.rssi; // Higher RSSI = stronger signal
}

void WiFiHelper::sortNetworksBySignal(std::vector<WiFiNetworkInfo>& networks) {
    std::sort(networks.begin(), networks.end(), [](const WiFiNetworkInfo& a, const WiFiNetworkInfo& b) {
        return a.rssi > b.rssi; // Higher RSSI = stronger signal
    });
}

void WiFiHelper::removeDuplicates(std::vector<WiFiNetworkInfo>& networks) {
    // Sort by SSID first, then by RSSI (strongest first)
    std::sort(networks.begin(), networks.end(), [](const WiFiNetworkInfo& a, const WiFiNetworkInfo& b) {
        int cmp = strcmp(a.ssid, b.ssid);
        if (cmp == 0) {
            return a.rssi > b.rssi; // For same SSID, prefer stronger signal
        }
        return cmp < 0;
    });
    
    // Remove duplicates, keeping the first (strongest) of each SSID
    auto last = std::unique(networks.begin(), networks.end(), 
        [](const WiFiNetworkInfo& a, const WiFiNetworkInfo& b) {
            return strcmp(a.ssid, b.ssid) == 0;
        });
    
    networks.erase(last, networks.end());
}