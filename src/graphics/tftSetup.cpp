#if HAS_TFT

#include "SPILock.h"
#include "sleep.h"

#include "api/PacketAPI.h"
#include "comms/PacketClient.h"
#include "comms/PacketServer.h"
#include "graphics/DeviceScreen.h"
#include "graphics/driver/DisplayDriverConfig.h"

#ifdef ARCH_PORTDUINO
#include "PortduinoGlue.h"
#include <thread>
#endif

#ifdef SENSECAP_INDICATOR
#include "graphics/map/RemoteSDService.h"
#include "input/I2CKeyboardScanner.h"
#include "mesh/IndicatorSerial.h"
#include "mesh/comms/I2CProxy.h"

// Set while the TFT task holds spiLock around the LVGL handler, so the file
// operations below (which run inside LVGL callbacks) can hand the SPI bus
// back to the radio while they wait on the interdevice link
static volatile TaskHandle_t spiLockHolder = nullptr;

/**
 * The remote FS round trips block for the request timeout, and the LVGL
 * handler runs with spiLock held, which would starve the LoRa radio for
 * that whole time. Nothing in a link round trip touches SPI, so give the
 * bus back for its duration.
 */
class SpiLockBreak
{
  public:
    SpiLockBreak()
    {
        held = spiLockHolder == xTaskGetCurrentTaskHandle();
        if (held) {
            spiLockHolder = nullptr;
            spiLock->unlock();
        }
    }
    ~SpiLockBreak()
    {
        if (held) {
            spiLock->lock();
            spiLockHolder = xTaskGetCurrentTaskHandle();
        }
    }

  private:
    bool held;
};

// Serves the UI map tiles from the SD card behind the RP2040, chunk-wise
// over the interdevice link.
//
// Retry policy: a lost frame (timeout) and a co-processor busy with card
// maintenance (FILE_BUSY) are transient, so they are retried; a request the
// co-processor refused (nack) or answered definitively (missing file, IO
// error) is not. Correlation ids make retrying safe, a late response to the
// first attempt is dropped as stale.
class IndicatorRemoteFS : public IRemoteFS
{
    static const int LINK_ATTEMPTS = 3;
    static const uint32_t BUSY_BACKOFF_MS = 150;

    // Returns true when the request should be sent again. `answered` is
    // false when the link itself failed (timeout), true when the
    // co-processor replied.
    static bool retryable(bool answered, meshtastic_FileStatus status, int attempt)
    {
        if (attempt + 1 >= LINK_ATTEMPTS)
            return false;
        if (!answered)
            return !sensecapIndicator->last_request_nacked(); // refused, not lost
        if (status == meshtastic_FileStatus_FILE_BUSY) {
            delay(BUSY_BACKOFF_MS); // card maintenance, give it a moment
            return true;
        }
        return false;
    }

  public:
    bool readChunk(const char *path, uint32_t offset, uint8_t *buf, uint32_t len, uint32_t *bytesRead,
                   uint32_t *fileSize) override
    {
        if (!sensecapIndicator)
            return false;
        SpiLockBreak spiFree;
        for (int attempt = 0; attempt < LINK_ATTEMPTS; attempt++) {
            memset(&result, 0, sizeof(result));
            bool answered = sensecapIndicator->file_read(path, offset, len, &result);
            if (answered && result.status == meshtastic_FileStatus_FILE_OK) {
                uint32_t n = result.filedata.size;
                if (n > len)
                    n = len;
                memcpy(buf, result.filedata.bytes, n);
                *bytesRead = n;
                // lv_fs positions are 32 bit, map tiles never come close
                *fileSize = (uint32_t)result.file_size;
                return true;
            }
            if (!retryable(answered, result.status, attempt))
                return false;
        }
        return false;
    }

    bool writeChunk(const char *path, uint32_t offset, const uint8_t *buf, uint32_t len, bool create) override
    {
        if (!sensecapIndicator)
            return false;
        SpiLockBreak spiFree;
        for (int attempt = 0; attempt < LINK_ATTEMPTS; attempt++) {
            memset(&result, 0, sizeof(result));
            bool answered = sensecapIndicator->file_write(path, offset, buf, len, create, &result);
            if (answered) {
                if (result.status == meshtastic_FileStatus_FILE_OK)
                    return true;
                // An append whose first attempt landed but whose response was
                // lost is refused as an offset conflict, and the file already
                // holds this chunk: that is the outcome we wanted
                if (attempt > 0 && !create && result.status == meshtastic_FileStatus_FILE_OFFSET_CONFLICT &&
                    result.file_size == (uint64_t)offset + len)
                    return true;
            }
            if (!retryable(answered, result.status, attempt))
                return false;
        }
        return false;
    }

    bool sdInfo(RemoteSdInfo &info) override
    {
        if (!sensecapIndicator)
            return false;
        SpiLockBreak spiFree;
        meshtastic_SdCardInfo result = meshtastic_SdCardInfo_init_zero;
        bool ok = false;
        for (int attempt = 0; attempt < LINK_ATTEMPTS && !ok; attempt++) {
            ok = sensecapIndicator->sd_info(&result);
            if (!ok && sensecapIndicator->last_request_nacked())
                break;
        }
        if (!ok)
            return false;
        info.present = result.present;
        info.cardType = (uint8_t)result.card_type;
        info.fatType = (uint8_t)result.fat_type;
        info.cardSize = result.card_size;
        info.usedBytes = result.used_bytes;
        info.freeBytes = result.free_bytes;
        info.statsValid = result.stats_valid;
        return true;
    }

    bool remove(const char *path) override
    {
        if (!sensecapIndicator)
            return false;
        SpiLockBreak spiFree;
        for (int attempt = 0; attempt < LINK_ATTEMPTS; attempt++) {
            memset(&result, 0, sizeof(result));
            bool answered = sensecapIndicator->file_remove(path, &result);
            // delete is idempotent on the co-processor: a file that is
            // already gone reports OK, so a retry after a lost response does
            // not have to be guessed at here
            if (answered && result.status == meshtastic_FileStatus_FILE_OK)
                return true;
            if (!retryable(answered, result.status, attempt))
                return false;
        }
        return false;
    }

    bool listDir(const char *path, std::set<std::string> &entries) override
    {
        if (!sensecapIndicator)
            return false;
        SpiLockBreak spiFree;
        uint32_t offset = 0;
        while (true) {
            bool got_page = false;
            for (int attempt = 0; attempt < LINK_ATTEMPTS && !got_page; attempt++) {
                memset(&listing, 0, sizeof(listing));
                bool answered = sensecapIndicator->list_directory(path, offset, &listing);
                if (answered && listing.status == meshtastic_FileStatus_FILE_OK)
                    got_page = true;
                else if (!retryable(answered, listing.status, attempt))
                    return false;
            }
            if (!got_page)
                return false;
            for (pb_size_t i = 0; i < listing.filenames_count; i++)
                entries.insert(listing.filenames[i]);
            offset += listing.filenames_count;
            if (listing.filenames_count == 0 || offset >= listing.total_count)
                break;
        }
        return true;
    }

  private:
    // Several KB each, kept off the UI task stack. All file operations
    // originate from the single UI task.
    meshtastic_FileTransfer result;
    meshtastic_DirectoryListing listing;
};
#endif

DeviceScreen *deviceScreen = nullptr;

#ifndef TFT_TASK_STACK_SIZE
#define TFT_TASK_STACK_SIZE 16384
#endif

#ifdef ARCH_ESP32
// Get notified when the system is entering light sleep
CallbackObserver<DeviceScreen, void *> tftSleepObserver =
    CallbackObserver<DeviceScreen, void *>(deviceScreen, &DeviceScreen::prepareSleep);
CallbackObserver<DeviceScreen, esp_sleep_wakeup_cause_t> endSleepObserver =
    CallbackObserver<DeviceScreen, esp_sleep_wakeup_cause_t>(deviceScreen, &DeviceScreen::wakeUp);
#endif

void tft_task_handler(void *param = nullptr)
{
    while (true) {
        spiLock->lock();
#ifdef SENSECAP_INDICATOR
        // lets the remote FS hand the bus back while it waits on the link
        spiLockHolder = xTaskGetCurrentTaskHandle();
#endif
        deviceScreen->task_handler();
#ifdef SENSECAP_INDICATOR
        spiLockHolder = nullptr;
#endif
        spiLock->unlock();
        deviceScreen->sleep();
    }
}

void tftSetup(void)
{
#ifdef SENSECAP_INDICATOR
    RemoteSDService::setBackend(new IndicatorRemoteFS());
    // the second bus is bridged to the RP2040, keep the keyboard scan off the
    // uninitialized local Wire1
    I2CKeyboardScanner::setSecondaryBus(i2cProxy);
#endif
#ifndef ARCH_PORTDUINO
    deviceScreen = &DeviceScreen::create();
    PacketAPI::create(PacketServer::init());
    deviceScreen->init(new PacketClient);
#else
    if (portduino_config.displayPanel != no_screen) {
        DisplayDriverConfig displayConfig;
        static char *panels[] = {"NOSCREEN", "X11",     "FB",      "ST7789",  "ST7735",  "ST7735S",
                                 "ST7796",   "ILI9341", "ILI9342", "ILI9486", "ILI9488", "HX8357D"};
        static char *touch[] = {"NOTOUCH", "XPT2046", "STMPE610", "GT911", "FT5x06"};
#if defined(USE_X11)
        if (portduino_config.displayPanel == x11) {
            if (portduino_config.displayWidth && portduino_config.displayHeight)
                displayConfig = DisplayDriverConfig(DisplayDriverConfig::device_t::X11, (uint16_t)portduino_config.displayWidth,
                                                    (uint16_t)portduino_config.displayHeight);
            else
                displayConfig.device(DisplayDriverConfig::device_t::X11);
        } else
#elif defined(USE_FRAMEBUFFER)
        if (portduino_config.displayPanel == fb) {
            if (portduino_config.displayWidth && portduino_config.displayHeight)
                displayConfig = DisplayDriverConfig(DisplayDriverConfig::device_t::FB, (uint16_t)portduino_config.displayWidth,
                                                    (uint16_t)portduino_config.displayHeight);
            else
                displayConfig.device(DisplayDriverConfig::device_t::FB);
        } else
#endif
        {
            displayConfig.device(DisplayDriverConfig::device_t::CUSTOM_TFT)
                .panel(DisplayDriverConfig::panel_config_t{.type = panels[portduino_config.displayPanel],
                                                           .panel_width = (uint16_t)portduino_config.displayWidth,
                                                           .panel_height = (uint16_t)portduino_config.displayHeight,
                                                           .rotation = (bool)portduino_config.displayRotate,
                                                           .pin_cs = (int16_t)portduino_config.displayCS.pin,
                                                           .pin_rst = (int16_t)portduino_config.displayReset.pin,
                                                           .offset_x = (uint16_t)portduino_config.displayOffsetX,
                                                           .offset_y = (uint16_t)portduino_config.displayOffsetY,
                                                           .offset_rotation = (uint8_t)portduino_config.displayOffsetRotate,
                                                           .invert = portduino_config.displayInvert ? true : false,
                                                           .rgb_order = (bool)portduino_config.displayRGBOrder,
                                                           .dlen_16bit = portduino_config.displayPanel == ili9486 ||
                                                                         portduino_config.displayPanel == ili9488})
                .bus(DisplayDriverConfig::bus_config_t{.freq_write = (uint32_t)portduino_config.displayBusFrequency,
                                                       .freq_read = 16000000,
                                                       .spi{.pin_dc = (int8_t)portduino_config.displayDC.pin,
                                                            .use_lock = true,
                                                            .spi_host = (uint16_t)portduino_config.display_spi_dev_int}})
                .input(DisplayDriverConfig::input_config_t{.keyboardDevice = portduino_config.keyboardDevice,
                                                           .pointerDevice = portduino_config.pointerDevice})
                .light(DisplayDriverConfig::light_config_t{.pin_bl = (int16_t)portduino_config.displayBacklight.pin,
                                                           .pwm_channel = (int8_t)portduino_config.displayBacklightPWMChannel.pin,
                                                           .invert = (bool)portduino_config.displayBacklightInvert});
            if (portduino_config.touchscreenI2CAddr == -1) {
                displayConfig.touch(
                    DisplayDriverConfig::touch_config_t{.type = touch[portduino_config.touchscreenModule],
                                                        .freq = (uint32_t)portduino_config.touchscreenBusFrequency,
                                                        .pin_int = (int16_t)portduino_config.touchscreenIRQ.pin,
                                                        .offset_rotation = (uint8_t)portduino_config.touchscreenRotate,
                                                        .spi{
                                                            .spi_host = (int8_t)portduino_config.touchscreen_spi_dev_int,
                                                        },
                                                        .pin_cs = (int16_t)portduino_config.touchscreenCS.pin});
            } else {
                displayConfig.touch(DisplayDriverConfig::touch_config_t{
                    .type = touch[portduino_config.touchscreenModule],
                    .freq = (uint32_t)portduino_config.touchscreenBusFrequency,
                    .x_min = 0,
                    .x_max = (int16_t)((portduino_config.touchscreenRotate & 1 ? portduino_config.displayWidth
                                                                               : portduino_config.displayHeight) -
                                       1),
                    .y_min = 0,
                    .y_max = (int16_t)((portduino_config.touchscreenRotate & 1 ? portduino_config.displayHeight
                                                                               : portduino_config.displayWidth) -
                                       1),
                    .pin_int = (int16_t)portduino_config.touchscreenIRQ.pin,
                    .offset_rotation = (uint8_t)portduino_config.touchscreenRotate,
                    .i2c{.i2c_addr = (uint8_t)portduino_config.touchscreenI2CAddr}});
            }
        }
        deviceScreen = &DeviceScreen::create(&displayConfig);
        PacketAPI::create(PacketServer::init());
        deviceScreen->init(new PacketClient);
    } else {
        LOG_INFO("Running without TFT display!");
    }
#endif

    if (deviceScreen) {
#ifdef ARCH_ESP32
        tftSleepObserver.observe(&notifyLightSleep);
        endSleepObserver.observe(&notifyLightSleepEnd);
        xTaskCreatePinnedToCore(tft_task_handler, "tft", TFT_TASK_STACK_SIZE, NULL, 1, NULL, 0);
#elif defined(ARCH_PORTDUINO)
        std::thread *tft_task = new std::thread([] { tft_task_handler(); });
#endif
    }
}

#endif