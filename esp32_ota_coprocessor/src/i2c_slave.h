#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// OTA status bytes — returned by the ESP32 to the nRF52 on CMD_STATUS.
// Values must match OtaI2CStatus in OtaRequestModule.h.
// ---------------------------------------------------------------------------
enum OtaStatus : uint8_t {
    STATUS_IDLE        = 0xA0,
    STATUS_CONNECTING  = 0xA1,
    STATUS_DOWNLOADING = 0xA2,
    STATUS_READY       = 0xA3,
    STATUS_BLE_DFU     = 0xA4,
    STATUS_DFU_DONE    = 0xA5,
    STATUS_ERROR       = 0xAF,
};

// ---------------------------------------------------------------------------
// Commands sent by the nRF52 master.
// Values must match OtaI2CCmd in OtaRequestModule.h.
// ---------------------------------------------------------------------------
enum OtaCmd : uint8_t {
    CMD_STATUS    = 0x01,
    CMD_START     = 0x02,
    CMD_ENTER_DFU = 0x03,
    CMD_DONE      = 0x04,
};

// Current status read by onRequest() — set by main loop.
extern volatile OtaStatus g_status;

// Last non-STATUS command received; valid when g_cmd_ready == true.
extern volatile OtaCmd g_pending_cmd;

// Set to true by ISR when a new command arrives; cleared by main loop.
extern volatile bool g_cmd_ready;

// Initialize Wire as I2C slave on the given address and pins.
void i2cSlaveInit(uint8_t addr, int sda, int scl);
