#pragma once

// ---------------------------------------------------------------------------
// SoftAP — phone connects here to upload firmware
// ---------------------------------------------------------------------------
#define AP_SSID     "OTA_C5A"
#define AP_PASS     "meshtastic_ota"   // min 8 chars; change as needed
#define AP_CHANNEL  6
#define AP_IP       "192.168.4.1"      // default ESP32 SoftAP gateway

// ---------------------------------------------------------------------------
// LittleFS paths
// ---------------------------------------------------------------------------
#define ZIP_TMP_PATH    "/upload.zip"  // temporary during upload
#define FW_DAT_PATH     "/fw.dat"      // extracted init packet
#define FW_BIN_PATH     "/fw.bin"      // extracted firmware binary
