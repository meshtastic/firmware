#pragma once

#ifdef OUTPUT_GPIO_PIN

#include "concurrency/OSThread.h"
#include "configuration.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CConsumer.h"

/**
 * I2C address of the ESP32-C5 OTA co-processor (slave).
 * Must match the address configured in the ESP32-C5 firmware.
 */
#define ESP32C5B_I2C_ADDR 0x42

/**
 * Commands sent by the nRF52 (master) → ESP32-C5B (slave).
 * Each command is a single byte written via Wire.beginTransmission().
 */
enum OtaI2CCmd : uint8_t {
    OTA_CMD_STATUS    = 0x01, // Query current status (reads back 1 byte)
    OTA_CMD_START     = 0x02, // Begin WiFi connect + firmware download
    OTA_CMD_ENTER_DFU = 0x03, // Notify ESP32 that nRF52 is about to enter DFU
    OTA_CMD_DONE      = 0x04, // Confirm OTA success; ESP32 can power off
};

/**
 * Status bytes returned by the ESP32-C5B after OTA_CMD_STATUS.
 */
enum OtaI2CStatus : uint8_t {
    OTA_STATUS_IDLE        = 0xA0, // Waiting for CMD_START
    OTA_STATUS_CONNECTING  = 0xA1, // Connecting to WiFi / peer
    OTA_STATUS_DOWNLOADING = 0xA2, // Downloading firmware .zip
    OTA_STATUS_READY       = 0xA3, // Firmware downloaded, BLE DFU ready
    OTA_STATUS_BLE_DFU     = 0xA4, // BLE DFU transfer in progress
    OTA_STATUS_DFU_DONE    = 0xA5, // DFU completed successfully
    OTA_STATUS_ERROR       = 0xAF, // Unrecoverable error
};

/**
 * OtaRequestModule — coordinates the full OTA flow between the nRF52 and the
 * ESP32-C5 co-processor via I2C.
 *
 * Lifecycle:
 *  1. Sleeps (disabled) until the I2C scan detects the ESP32-C5B at 0x42.
 *  2. Queries status to determine phase (pre-OTA or post-OTA boot).
 *  3. Pre-OTA: sends CMD_START, polls until firmware ready, then resets into DFU.
 *  4. Post-OTA: sends CMD_DONE, sets output_gpio_enabled = false → co-processor off.
 */
class OtaRequestModule : public concurrency::OSThread, public ScanI2CConsumer
{
  public:
    OtaRequestModule();

    /** Called by ScanI2CCompleted() after each I2C bus scan. */
    void i2cScanFinished(ScanI2C *scanner) override;

  protected:
    int32_t runOnce() override;

  private:
    enum State {
        STATE_IDLE,
        STATE_PROBE_RETRY,      // GPIO HIGH pero C5-B aún arrancando — sondea I2C directamente
        STATE_SEND_START,
        STATE_POLLING_DOWNLOAD,
        STATE_ENTER_DFU,
        STATE_POST_OTA,
    };

    // Timeout para STATE_PROBE_RETRY: 30 × 2 s = 60 s
    static constexpr uint8_t PROBE_RETRY_MAX = 30;

    State   _state   = STATE_IDLE;
    uint8_t _retries = 0;

    /** Iniciar la secuencia OTA (común para detección directa o por reintento). */
    void startOta();

    /** Send CMD_STATUS and return the 1-byte response. */
    uint8_t queryStatus();

    /** Write a single command byte to the co-processor. */
    void sendCommand(OtaI2CCmd cmd);

    /** Trigger the Adafruit nRF52 bootloader DFU entry (does not return). */
    void enterDfuBootloader();

    /** Set output_gpio_enabled = false, save config, apply GPIO. */
    void powerOffCoprocessor();
};

extern OtaRequestModule *otaRequestModule;

#endif // OUTPUT_GPIO_PIN
