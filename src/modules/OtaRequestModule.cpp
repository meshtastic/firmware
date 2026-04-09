#ifdef OUTPUT_GPIO_PIN

#include "OtaRequestModule.h"
#include "GpioOutputModule.h"
#include "NodeDB.h"
#include <Arduino.h>
#include <Wire.h>

OtaRequestModule *otaRequestModule;

// Maximum number of 5-second polls before giving up (~5 minutes)
static constexpr uint8_t OTA_POLL_TIMEOUT = 60;

OtaRequestModule::OtaRequestModule() : concurrency::OSThread("OtaRequestModule"), ScanI2CConsumer()
{
    // Disabled until the I2C scan finds the co-processor
    enabled = false;
}

void OtaRequestModule::i2cScanFinished(ScanI2C *scanner)
{
    auto found = scanner->find(ScanI2C::DeviceType::ESP32_OTA_COPROCESSOR);

    if (found.type == ScanI2C::DeviceType::NONE) {
        // C5-B no respondió en el scan. Si el GPIO está en HIGH (vía CHIP_PU)
        // el C5-B puede estar todavía arrancando tras el reboot del nRF52.
        // Entramos en modo reintento: sondeamos 0x42 directamente cada 2 s.
        if (config.device.output_gpio_enabled) {
            LOG_INFO("OtaRequestModule: C5-B not in scan but GPIO is HIGH — will retry for up to 60s");
            _state   = STATE_PROBE_RETRY;
            _retries = 0;
            enabled  = true;
            setInterval(2000);
        }
        return;
    }

    startOta();
}

void OtaRequestModule::startOta()
{
    uint8_t status = queryStatus();
    LOG_INFO("OtaRequestModule: ESP32-C5B ready, status = 0x%02X", status);

    if (status == OTA_STATUS_DFU_DONE) {
        _state = STATE_POST_OTA;
    } else {
        _state = STATE_SEND_START;
    }
    _retries = 0;
    enabled  = true;
    setInterval(200);
}

int32_t OtaRequestModule::runOnce()
{
    switch (_state) {

    case STATE_IDLE:
        enabled = false;
        return INT32_MAX;

    case STATE_PROBE_RETRY: {
        // Sondeo directo de la dirección I2C del C5-B.
        // El GPIO está en HIGH (CHIP_PU), el C5-B puede estar arrancando todavía.
        Wire.beginTransmission(ESP32C5B_I2C_ADDR);
        bool responded = (Wire.endTransmission() == 0);
        if (responded) {
            LOG_INFO("OtaRequestModule: C5-B responded on retry %u", _retries);
            startOta();
            return INT32_MAX; // startOta() llama a setInterval(200)
        }
        if (++_retries >= PROBE_RETRY_MAX) {
            LOG_ERROR("OtaRequestModule: C5-B did not respond in 60s, aborting");
            _state  = STATE_IDLE;
            enabled = false;
            return INT32_MAX;
        }
        LOG_DEBUG("OtaRequestModule: C5-B not yet ready, retry %u/%u", _retries, PROBE_RETRY_MAX);
        return 2000;
    }

    case STATE_SEND_START:
        LOG_INFO("OtaRequestModule: sending CMD_START to ESP32-C5B");
        sendCommand(OTA_CMD_START);
        _state   = STATE_POLLING_DOWNLOAD;
        _retries = 0;
        return 5000;

    case STATE_POLLING_DOWNLOAD: {
        uint8_t status = queryStatus();
        LOG_DEBUG("OtaRequestModule: poll %u/%" PRIu8 " status=0x%02X", _retries, OTA_POLL_TIMEOUT, status);

        if (status == OTA_STATUS_READY) {
            LOG_INFO("OtaRequestModule: firmware ready, entering DFU bootloader");
            _state = STATE_ENTER_DFU;
            return 100;
        }
        if (status == OTA_STATUS_ERROR) {
            LOG_ERROR("OtaRequestModule: ESP32-C5B reported error, aborting OTA");
            _state  = STATE_IDLE;
            enabled = false;
            return INT32_MAX;
        }
        if (++_retries >= OTA_POLL_TIMEOUT) {
            LOG_ERROR("OtaRequestModule: timeout waiting for firmware, aborting OTA");
            _state  = STATE_IDLE;
            enabled = false;
            return INT32_MAX;
        }
        return 5000;
    }

    case STATE_ENTER_DFU:
        sendCommand(OTA_CMD_ENTER_DFU);
        delay(100); // let the ESP32 register the command before bus drops
        enterDfuBootloader(); // does not return on nRF52
        // fallthrough safety for non-nRF52 builds
        _state  = STATE_IDLE;
        enabled = false;
        return INT32_MAX;

    case STATE_POST_OTA:
        LOG_INFO("OtaRequestModule: OTA confirmed, sending CMD_DONE, powering off co-processor");
        sendCommand(OTA_CMD_DONE);
        delay(200);
        powerOffCoprocessor();
        _state  = STATE_IDLE;
        enabled = false;
        return INT32_MAX;
    }

    // unreachable
    enabled = false;
    return INT32_MAX;
}

// ---------------------------------------------------------------------------
// I2C helpers
// ---------------------------------------------------------------------------

uint8_t OtaRequestModule::queryStatus()
{
    Wire.beginTransmission(ESP32C5B_I2C_ADDR);
    Wire.write(OTA_CMD_STATUS);
    Wire.endTransmission(false); // repeated start — keep bus
    Wire.requestFrom((uint8_t)ESP32C5B_I2C_ADDR, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    LOG_WARN("OtaRequestModule: no response from ESP32-C5B");
    return OTA_STATUS_ERROR;
}

void OtaRequestModule::sendCommand(OtaI2CCmd cmd)
{
    Wire.beginTransmission(ESP32C5B_I2C_ADDR);
    Wire.write((uint8_t)cmd);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        LOG_WARN("OtaRequestModule: I2C write error %u for cmd 0x%02X", err, (uint8_t)cmd);
    }
}

// ---------------------------------------------------------------------------
// Platform-specific DFU entry
// ---------------------------------------------------------------------------

void OtaRequestModule::enterDfuBootloader()
{
#if defined(ARCH_NRF52) && defined(NRF52_SERIES)
    // Adafruit nRF52 bootloader: write magic byte then reset
    NRF_POWER->GPREGRET = 0xA8;
    NVIC_SystemReset();
#else
    LOG_ERROR("OtaRequestModule: enterDfuBootloader() not supported on this platform");
#endif
}

// ---------------------------------------------------------------------------
// Power off the co-processor by clearing output_gpio_enabled
// ---------------------------------------------------------------------------

void OtaRequestModule::powerOffCoprocessor()
{
    config.device.output_gpio_enabled = false;
    nodeDB->saveToDisk(SEGMENT_CONFIG);
    if (gpioOutputModule) {
        gpioOutputModule->apply();
    }
}

#endif // OUTPUT_GPIO_PIN
