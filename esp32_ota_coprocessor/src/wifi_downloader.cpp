#include "wifi_downloader.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>

static constexpr size_t HTTP_BUF = 1024;

bool wifiConnect(const char *ssid, const char *pass, uint32_t timeout_ms)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    Serial.printf("[WiFi] Connecting to \"%s\"...\n", ssid);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) {
            Serial.println("[WiFi] Timeout");
            WiFi.disconnect(true);
            return false;
        }
        delay(250);
    }
    Serial.printf("[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

bool downloadFile(const char *url, const char *dest_path)
{
    Serial.printf("[HTTP] GET %s -> %s\n", url, dest_path);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[HTTP] Error %d\n", code);
        http.end();
        return false;
    }

    File f = LittleFS.open(dest_path, "w");
    if (!f) {
        Serial.printf("[HTTP] Cannot open %s for writing\n", dest_path);
        http.end();
        return false;
    }

    WiFiClient *stream  = http.getStreamPtr();
    int         total   = http.getSize(); // -1 if chunked
    int         written = 0;
    uint8_t     buf[HTTP_BUF];

    while (http.connected() && (total < 0 || written < total)) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf, min((int)sizeof(buf), avail));
            f.write(buf, n);
            written += n;
        } else if (!stream->connected()) {
            break;
        } else {
            delay(1);
        }
    }

    f.close();
    http.end();

    if (written == 0) {
        Serial.println("[HTTP] No data received");
        return false;
    }
    Serial.printf("[HTTP] Downloaded %d bytes\n", written);
    return true;
}

bool downloadFirmware(const char *url_dat, const char *path_dat,
                      const char *url_bin, const char *path_bin)
{
    if (!downloadFile(url_dat, path_dat)) return false;
    if (!downloadFile(url_bin, path_bin)) return false;
    return true;
}
