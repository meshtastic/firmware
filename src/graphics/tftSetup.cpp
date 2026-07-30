#if HAS_TFT

#include "SPILock.h"
#include "sleep.h"

#include "api/PacketAPI.h"
#include "comms/PacketClient.h"
#include "comms/PacketServer.h"
#include "graphics/DeviceScreen.h"
#include "graphics/driver/DisplayDriverConfig.h"
#include "util/ISpiLock.h"

#ifdef ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

#if defined(ARCH_PORTDUINO) || !defined(HAS_FREE_RTOS)
#include <cstdio>
#include <cstdlib>
#include <thread>
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

/**
 * Lends spiLock to device-ui so it can guard its own bus traffic.
 *
 * This used to be a coarse hold around the whole UI cycle in tft_task_handler(). On
 * boards where the TFT, SD card and LoRa radio share one SPI bus (T-Deck), that blocked
 * every radio operation on the main loop for an entire render+flush - tens to hundreds
 * of milliseconds while the UI animates, felt as mesh RX/TX latency. Most of that time
 * is LVGL timer work and rendering into the draw buffer, which touches no SPI at all.
 *
 * device-ui now takes the lock only around real transfers, so the bus is free during the
 * CPU-only majority of a redraw.
 *
 * Reentrant because ISpiLock requires it: device-ui guards at method granularity, so a
 * caller that already holds the bus can reach another guarded method, while spiLock is a
 * plain binary semaphore that would self-deadlock on the second take. Track the owning
 * thread and only touch the underlying lock on the outermost acquire.
 *
 * Only the owner writes owner/depth, and only while holding the lock, so no additional
 * synchronization is needed: a thread that does not own it either reads some other
 * thread's id or the unowned sentinel, and in both cases correctly goes on to block.
 */
class ReentrantSpiLock : public ISpiLock
{
  public:
    void lock(void) override
    {
        ThreadId self = currentThread();
        if (depth && owner == self) {
            depth++;
            return;
        }
        spiLock->lock();
        owner = self;
        depth = 1;
    }

    void unlock(void) override
    {
        if (--depth == 0) {
            owner = ThreadId();
            spiLock->unlock();
        }
    }

  private:
    // Portduino builds have no FreeRTOS, but device-ui runs there too, so the
    // reentrancy has to be portable rather than an ESP32-only path.
#ifdef HAS_FREE_RTOS
    using ThreadId = TaskHandle_t;
    static ThreadId currentThread(void) { return xTaskGetCurrentTaskHandle(); }
#else
    using ThreadId = std::thread::id;
    static ThreadId currentThread(void) { return std::this_thread::get_id(); }
#endif

    ThreadId owner = ThreadId();
    uint32_t depth = 0;
};

static ReentrantSpiLock reentrantSpiLock;

void tft_task_handler(void *param = nullptr)
{
    while (true) {
        // No lock held here on purpose - device-ui guards its own SPI access.
        deviceScreen->task_handler();
        deviceScreen->sleep();
    }
}

void tftSetup(void)
{
#ifndef ARCH_PORTDUINO
    deviceScreen = &DeviceScreen::create(reentrantSpiLock);
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
            // Rotation from yaml Display.OffsetRotate: 1=90, 2=180, 3=270 deg
            char rbuf[4];
            snprintf(rbuf, sizeof(rbuf), "%d", portduino_config.displayRotate ? (portduino_config.displayOffsetRotate & 3) : 0);
            if (setenv("MESHTASTIC_FB_ROTATION", rbuf, 1) != 0)
                LOG_ERROR("Failed to set MESHTASTIC_FB_ROTATION, framebuffer will use its default rotation");
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
        deviceScreen = &DeviceScreen::create(&displayConfig, &reentrantSpiLock);
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