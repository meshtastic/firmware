#pragma once

// ---------------------------------------------------------------------------
// I2C slave — must match ESP32C5B_I2C_ADDR in OtaRequestModule.h
// ---------------------------------------------------------------------------
#define I2C_SLAVE_ADDR  0x42
#define I2C_SDA_PIN     6   // GPIO6 on ESP32-C5; adjust to your wiring
#define I2C_SCL_PIN     7   // GPIO7 on ESP32-C5; adjust to your wiring

// ---------------------------------------------------------------------------
// WiFi — the C5-A peer acts as a SoftAP; C5-B connects as STA.
// ---------------------------------------------------------------------------
#define WIFI_SSID       "OTA_C5A"
#define WIFI_PASS       "meshtastic_ota"
#define WIFI_TIMEOUT_MS 20000

// C5-A serves two files over plain HTTP (extracted from the Nordic .zip):
//   firmware.dat  → init packet (signed metadata)
//   firmware.bin  → application binary
#define FIRMWARE_SERVER_IP  "192.168.4.1"   // default SoftAP gateway IP
#define FIRMWARE_URL_DAT    "http://" FIRMWARE_SERVER_IP "/firmware.dat"
#define FIRMWARE_URL_BIN    "http://" FIRMWARE_SERVER_IP "/firmware.bin"

// LittleFS paths for downloaded files
#define FIRMWARE_PATH_DAT   "/fw.dat"
#define FIRMWARE_PATH_BIN   "/fw.bin"

// ---------------------------------------------------------------------------
// BLE DFU — Nordic Secure DFU (Adafruit nRF52 bootloader)
// ---------------------------------------------------------------------------

// nRF52 bootloader advertises this device name
#define DFU_TARGET_NAME     "DfuTarg"

// DFU buttonless service (not needed — nRF52 already in bootloader)
// Secure DFU service
#define DFU_SERVICE_UUID    "FE59"

// Control Point: Write (with response) + Notify
#define DFU_CTRL_UUID       "8EC90001-F315-4F60-9FB8-838830DAEA50"
// Packet: Write Without Response
#define DFU_PKT_UUID        "8EC90002-F315-4F60-9FB8-838830DAEA50"

// BLE scan duration (seconds) per attempt; retried up to DFU_SCAN_RETRIES times
#define DFU_SCAN_SECS       10
#define DFU_SCAN_RETRIES    6   // up to 60 s total wait for DFU advertisement

// Max bytes per single BLE write to the Packet characteristic.
// Real limit = negotiated MTU - 3. This is a safe conservative default;
// ble_dfu.cpp will use the actual negotiated value if available.
#define DFU_CHUNK_SIZE      244
