#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "fw_manager.h"
#include "http_server.h"

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[C5-A] OTA co-processor A starting");

    // Mount filesystem (format on first boot)
    if (!LittleFS.begin(true)) {
        Serial.println("[C5-A] LittleFS mount failed — halting");
        while (true) delay(1000);
    }
    Serial.printf("[C5-A] LittleFS: %u / %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes());

    // Check if firmware files already exist from a previous upload
    if (LittleFS.exists(FW_DAT_PATH) && LittleFS.exists(FW_BIN_PATH)) {
        g_fw_state = FwState::READY;
        Serial.printf("[C5-A] Cached firmware found — DAT %u B, BIN %u B\n",
                      (unsigned)fwDatSize(), (unsigned)fwBinSize());
    }

    // Start SoftAP
    WiFi.mode(WIFI_AP);

    // Log station connect / disconnect events
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
            Serial.printf("[AP] C5-B connected — MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                          info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                          info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
        } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
            Serial.printf("[AP] C5-B disconnected — MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                          info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                          info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
        }
    });

    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
    Serial.printf("[C5-A] AP \"%s\" up at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Start HTTP server
    httpServerBegin();

    Serial.println("[C5-A] Ready — connect to WiFi \"" AP_SSID "\" and open http://" AP_IP);
}

void loop()
{
    // Offload zip extraction from the async web server task
    fwManagerTick();
    delay(10);
}
