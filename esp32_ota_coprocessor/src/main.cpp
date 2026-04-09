#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "config.h"
#include "i2c_slave.h"
#include "wifi_downloader.h"
#include "ble_dfu.h"

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[OTA-C5B] ESP32 OTA co-processor starting");

    if (!LittleFS.begin(true /* format on fail */)) {
        Serial.println("[OTA-C5B] LittleFS mount failed — halting");
        while (true) delay(1000);
    }
    Serial.printf("[OTA-C5B] LittleFS: %u/%u bytes used\n",
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes());

    i2cSlaveInit(I2C_SLAVE_ADDR, I2C_SDA_PIN, I2C_SCL_PIN);

    Serial.println("[OTA-C5B] Ready, waiting for commands from nRF52");
}

// ---------------------------------------------------------------------------
// loop — driven by commands from the nRF52 via I2C
// ---------------------------------------------------------------------------
void loop()
{
    if (!g_cmd_ready) {
        delay(10);
        return;
    }

    // Atomically grab the command
    OtaCmd cmd   = g_pending_cmd;
    g_cmd_ready  = false;

    Serial.printf("[OTA-C5B] Command received: 0x%02X\n", (uint8_t)cmd);

    switch (cmd) {

    // ------------------------------------------------------------------
    case CMD_START:
    // ------------------------------------------------------------------
        // Connect to the C5-A peer and download both firmware files
        g_status = STATUS_CONNECTING;
        if (!wifiConnect(WIFI_SSID, WIFI_PASS, WIFI_TIMEOUT_MS)) {
            Serial.println("[OTA-C5B] WiFi failed");
            g_status = STATUS_ERROR;
            break;
        }

        g_status = STATUS_DOWNLOADING;
        if (!downloadFirmware(FIRMWARE_URL_DAT, FIRMWARE_PATH_DAT,
                              FIRMWARE_URL_BIN, FIRMWARE_PATH_BIN)) {
            Serial.println("[OTA-C5B] Download failed");
            WiFi.disconnect(true);
            g_status = STATUS_ERROR;
            break;
        }

        WiFi.disconnect(true); // release WiFi before BLE (both use RF on C5)
        g_status = STATUS_READY;
        Serial.println("[OTA-C5B] Firmware ready — waiting for CMD_ENTER_DFU");
        break;

    // ------------------------------------------------------------------
    case CMD_ENTER_DFU:
    // ------------------------------------------------------------------
        // nRF52 is resetting into the DFU bootloader now.
        // Give it a few seconds to start advertising, then run Nordic DFU.
        Serial.println("[OTA-C5B] Starting BLE DFU...");
        delay(3000); // wait for nRF52 bootloader to come up

        g_status = STATUS_BLE_DFU;
        if (runBleNordicDfu(FIRMWARE_PATH_DAT, FIRMWARE_PATH_BIN)) {
            // nRF52 will reboot into new firmware; wait for CMD_DONE
            g_status = STATUS_DFU_DONE;
            Serial.println("[OTA-C5B] DFU complete, status=DFU_DONE");
        } else {
            Serial.println("[OTA-C5B] DFU failed");
            g_status = STATUS_ERROR;
        }
        break;

    // ------------------------------------------------------------------
    case CMD_DONE:
    // ------------------------------------------------------------------
        // nRF52 running new firmware confirms success.
        // Clean up downloaded files and return to idle.
        Serial.println("[OTA-C5B] OTA confirmed by nRF52, cleaning up");
        LittleFS.remove(FIRMWARE_PATH_DAT);
        LittleFS.remove(FIRMWARE_PATH_BIN);
        g_status = STATUS_IDLE;
        Serial.println("[OTA-C5B] Idle — GPIO will go LOW, powering off");
        break;

    default:
        Serial.printf("[OTA-C5B] Unknown command 0x%02X ignored\n", (uint8_t)cmd);
        break;
    }
}
