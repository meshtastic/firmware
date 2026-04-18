#include "./EInkParallel.h"

#if defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) && defined(ARCH_ESP32) && defined(NICHE_HAS_FASTEPD)

#include "FastEPD.h"

using namespace NicheGraphics::Drivers;

EInkParallel::EInkParallel(uint16_t width, uint16_t height, uint32_t panelType, uint32_t panelClock)
    : EInk(width, height, (UpdateTypes)(FULL | FAST)), panelType(panelType), panelClock(panelClock)
{
}

EInkParallel::~EInkParallel()
{
    if (asyncRunning.load()) {
        for (int i = 0; i < 50 && asyncRunning.load(); ++i)
            delay(50);
        if (asyncTaskHandle) {
            vTaskDelete(asyncTaskHandle);
            asyncTaskHandle = nullptr;
        }
    }
    delete epaper;
}

void EInkParallel::begin(SPIClass *, uint8_t, uint8_t, uint8_t, uint8_t)
{
    // Parallel panels don't use the SPI args; FastEPD owns the bus.
    if (!epaper) {
        epaper = new FASTEPD;
        epaper->initPanel((int)panelType, panelClock);
        postPanelInit();
        epaper->setMode(BB_MODE_1BPP);
        epaper->clearWhite();
        epaper->fullUpdate(true);
    }
}

void EInkParallel::update(uint8_t *imageData, UpdateTypes type)
{
    if (!epaper)
        return;

    pendingType = type;
    copyImageInverted(imageData);

    if (type == FULL) {
        // Pick CLEAR_SLOW periodically to clear ghosting.
        const int clearMode = (fastRefreshCount >= FULL_SLOW_PERIOD) ? CLEAR_SLOW : CLEAR_FAST;
        fastRefreshCount = 0;

        if (!asyncRunning.load()) {
            asyncRunning.store(true);
            BaseType_t rc =
                xTaskCreatePinnedToCore(asyncFullTask, "epd_full", 4096 / sizeof(StackType_t), this, 2, &asyncTaskHandle,
#if CONFIG_FREERTOS_UNICORE
                                        0
#else
                                        1
#endif
                );
            if (rc != pdPASS) {
                LOG_WARN("Async full failed; running blocking");
                epaper->fullUpdate(clearMode, false);
                epaper->backupPlane();
                asyncRunning.store(false);
                asyncTaskHandle = nullptr;
                return; // synchronous: nothing to poll
            }
            // Begin polling for completion.
            beginPolling(100, 1500);
        }
    } else {
        // FAST: synchronous partial / clipped fullUpdate. Block briefly here.
        epaper->fullUpdate(CLEAR_FAST, false);
        epaper->backupPlane();
        fastRefreshCount++;
        // No polling needed; isUpdateDone() will report done immediately.
        beginPolling(10, 0);
    }
}

void EInkParallel::asyncFullTask(void *param)
{
    auto *self = static_cast<EInkParallel *>(param);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }
    self->epaper->fullUpdate(CLEAR_FAST, false);
    self->epaper->backupPlane();
    self->asyncRunning.store(false);
    self->asyncTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool EInkParallel::isUpdateDone()
{
    return !asyncRunning.load();
}

void EInkParallel::finalizeUpdate()
{
    pendingType = UpdateTypes::UNSPECIFIED;
}

// Convert a niche-format buffer (row-major, MSB-left, 1=WHITE) into FastEPD's currentBuffer
// (row-major, MSB-left, 1=BLACK). Polarity inversion only.
void EInkParallel::copyImageInverted(const uint8_t *src)
{
    uint8_t *dst = epaper->currentBuffer();
    if (!dst || !src)
        return;

    const uint16_t rowBytes = ((width - 1) / 8) + 1;
    const uint32_t total = rowBytes * height;

    // Mask off bits beyond the panel width in the trailing byte of each row.
    const uint8_t trailingMask = (uint8_t)(0xFFu << ((rowBytes * 8) - width));

    for (uint16_t y = 0; y < height; y++) {
        const uint32_t base = y * rowBytes;
        for (uint16_t b = 0; b < rowBytes - 1; b++) {
            dst[base + b] = ~src[base + b];
        }
        dst[base + rowBytes - 1] = (~src[base + rowBytes - 1]) & trailingMask;
    }
    (void)total;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS && ARCH_ESP32 && NICHE_HAS_FASTEPD
