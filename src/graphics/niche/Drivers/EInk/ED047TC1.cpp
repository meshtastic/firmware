/*

    NicheGraphics parallel E-Ink driver for the LilyGo T5-S3-ePaper-Pro (ED047TC1).

    InkHUD buffer format : 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white
    FastEPD buffer format: 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white

    Both formats share the same pixel layout and polarity (1 = white, 0 = black).
    The InkHUD safe-area buffer (928×508) is copied into the centre of the physical
    960×540 FastEPD buffer so content clears the panel's inactive edge border.
    See ED047TC1.h for the H_OFFSET_BYTES / V_OFFSET_TOP / V_OFFSET_BOTTOM constants.

*/

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#ifdef T5_S3_EPAPER_PRO

#include "./ED047TC1.h"

#include "FastEPD.h"
#include "UptimeClock.h"
#include "configuration.h"
#include "mesh/Throttle.h"
#if INKHUD_BOOKREADER_GRAYSCALE
#include <esp_heap_caps.h>
#endif

extern volatile bool dma_is_done;
extern void bbepRowControl(FASTEPDSTATE *state, int type);
extern void bbepWriteRow(FASTEPDSTATE *state, uint8_t *data, int length, int rowStep);

using namespace NicheGraphics::Drivers;

#if defined(T5_S3_EPAPER_PRO_V2)
// FastEPD helper symbols are defined in FastEPD.inl with C++ linkage.
extern void bbepPCA9535DigitalWrite(uint8_t pin, uint8_t value);
extern uint8_t bbepPCA9535DigitalRead(uint8_t pin);
extern int bbepI2CWrite(unsigned char iAddr, unsigned char *pData, int iLen);
extern int bbepI2CReadRegister(unsigned char iAddr, unsigned char u8Register, unsigned char *pData, int iLen);
#endif

namespace
{
#if defined(T5_S3_EPAPER_PRO_V2)
// FastEPD default V2 power callback blocks forever waiting for PWRGOOD.
// Replace it with a timeout-safe version so boot never deadlocks.
int safeEPDiyV7EinkPower(void *pBBEP, int bOn)
{
    static bool warnedPgood = false;
    static bool warnedTpsPg = false;
    static bool warnedTpsWrite = false;

    FASTEPDSTATE *pState = static_cast<FASTEPDSTATE *>(pBBEP);
    if (!pState) {
        return BBEP_ERROR_BAD_PARAMETER;
    }

    if (bOn == pState->pwr_on) {
        return BBEP_SUCCESS;
    }

    if (bOn) {
        bbepPCA9535DigitalWrite(8, 1);  // OE on
        bbepPCA9535DigitalWrite(9, 1);  // GMOD on
        bbepPCA9535DigitalWrite(13, 1); // WAKEUP on
        bbepPCA9535DigitalWrite(11, 1); // PWRUP on
        bbepPCA9535DigitalWrite(12, 1); // VCOM CTRL on
        delay(1);

        const uint32_t pgoodStart = millis();
        bool pgoodSeen = false;
        while (!bbepPCA9535DigitalRead(14)) { // CFG_PIN_PWRGOOD
            if ((millis() - pgoodStart) > 1200) {
                if (!warnedPgood) {
                    LOG_WARN("ED047TC1: PWRGOOD timeout, continuing with fallback power-on path");
                    warnedPgood = true;
                }
                break;
            }
            delay(1);
        }
        if (bbepPCA9535DigitalRead(14)) {
            pgoodSeen = true;
        }

        uint8_t ucTemp[4] = {0};
        ucTemp[0] = 0x01; // TPS_REG_ENABLE
        ucTemp[1] = 0x3f; // enable rails
        const int tpsEnableRc = bbepI2CWrite(0x68, ucTemp, 2);

        const int vcom = pState->iVCOM / -10;
        ucTemp[0] = 3; // VCOM registers 3+4 (L + H)
        ucTemp[1] = static_cast<uint8_t>(vcom);
        ucTemp[2] = static_cast<uint8_t>(vcom >> 8);
        const int tpsVcomRc = bbepI2CWrite(0x68, ucTemp, 3);
        if ((tpsEnableRc == 0 || tpsVcomRc == 0) && !warnedTpsWrite) {
            LOG_WARN("ED047TC1: TPS write did not ACK, continuing with fallback");
            warnedTpsWrite = true;
        }

        int iTimeout = 0;
        uint8_t u8Value = 0;
        while (iTimeout < 220 && ((u8Value & 0xfa) != 0xfa)) {
            bbepI2CReadRegister(0x68, 0x0F, &u8Value, 1); // TPS_REG_PG
            iTimeout++;
            delay(1);
        }
        if (iTimeout >= 220 && !warnedTpsPg) {
            if (pgoodSeen) {
                LOG_WARN("ED047TC1: TPS power-good register timeout, panel may still work");
            } else {
                LOG_WARN("ED047TC1: TPS power-good register timeout after PWRGOOD fallback");
            }
            warnedTpsPg = true;
        }

        pState->pwr_on = 1;
    } else {
        bbepPCA9535DigitalWrite(8, 0);  // OE off
        bbepPCA9535DigitalWrite(9, 0);  // GMOD off
        bbepPCA9535DigitalWrite(11, 0); // PWRUP off
        bbepPCA9535DigitalWrite(12, 0); // VCOM CTRL off
        delay(1);
        bbepPCA9535DigitalWrite(13, 0); // WAKEUP off
        pState->pwr_on = 0;
    }

    return BBEP_SUCCESS;
}
#endif

class SafeFastEPD : public FASTEPD
{
  public:
    static constexpr unsigned whiteFinishPasses = 6;

    void installSafePowerHandler()
    {
#if defined(T5_S3_EPAPER_PRO_V2)
        _state.pfnEinkPower = safeEPDiyV7EinkPower;
#endif
    }
    int partialUpdateWithWhiteFinish(uint32_t whitePixels, bool cleanup = false)
    {
        if (dmaFault || _state.mode != BB_MODE_1BPP || _state.native_width != 960 || _state.native_height != 540 ||
            _state.rotation != 0 || (_state.iFlags & (BB_PANEL_FLAG_MIRROR_X | BB_PANEL_FLAG_MIRROR_Y)) ||
            _state.panelDef.iLinePadding || !_state.pTemp || !_state.dma_buf)
            return BBEP_ERROR_BAD_PARAMETER;
        int rc = FASTEPD::partialUpdate(true, 0, 539);
        if (rc == BBEP_SUCCESS && (whitePixels || cleanup))
            rc = finishMonochromePixels(_state.pTemp, whiteFinishPasses, cleanup);
        if (rc == BBEP_SUCCESS)
            rc = writePulseFrame(nullptr);
        const int offRc = einkPower(0);
        return rc == BBEP_SUCCESS ? offRc : rc;
    }

    bool hasDmaFault() const { return dmaFault; }

#if INKHUD_BOOKREADER_GRAYSCALE
    bool promoteMonochromeBaseline()
    {
        if (dmaFault || _state.mode != BB_MODE_1BPP || _state.native_width != 960 || _state.native_height != 540 ||
            _state.rotation != 0 || (_state.iFlags & (BB_PANEL_FLAG_MIRROR_X | BB_PANEL_FLAG_MIRROR_Y)) || !_state.pCurrent)
            return false;
        // Expand backwards; the larger gray plane overlaps the monochrome source.
        for (size_t i = 960U * 540U / 8U; i-- > 0;) {
            const uint8_t pixels = _state.pCurrent[i];
            for (unsigned pair = 0; pair < 4; ++pair)
                _state.pCurrent[i * 4U + pair] =
                    ((pixels & (0x80U >> (pair * 2U))) ? 0xF0U : 0U) | ((pixels & (0x40U >> (pair * 2U))) ? 0x0FU : 0U);
        }
        return true;
    }

    // Optimize frame preparation only; panel drive timing stays unchanged.
    __attribute__((optimize("O3"))) bool preparePanelGrayUpdate(const uint8_t *image, uint16_t width, uint16_t height,
                                                                uint16_t left, uint16_t top, bool baselineValid,
                                                                uint32_t &changed)
    {
        if (dmaFault || !image || _state.mode != BB_MODE_4BPP || _state.native_width != 960 || _state.native_height != 540 ||
            _state.rotation != 0 || (_state.iFlags & (BB_PANEL_FLAG_MIRROR_X | BB_PANEL_FLAG_MIRROR_Y)) ||
            _state.panelDef.iLinePadding || !_state.pCurrent || !_state.dma_buf || (left & 1U) || (width & 1U) ||
            left + width > 960 || top + height > 540 || !_state.panelDef.pGrayMatrix || _state.panelDef.iMatrixSize != 16U * 8U)
            return false;
        if (baselineValid) {
            uint16_t y = 0;
            for (; y < height; ++y)
                if (memcmp(_state.pCurrent + (top + y) * 480U + left / 2U, image + y * (width / 2U), width / 2U) != 0)
                    break;
            if (y == height) {
                changed = 0;
#if INKHUD_BOOKREADER_PERF_LOG
                LOG_INFO("[BookReaderDraw] panel prepare skipped identical packed frame");
#endif
                return true;
            }
        }
        if (!grayLookupReady) {
            const uint8_t *matrix = _state.panelDef.pGrayMatrix;
            for (unsigned pass = 0; pass < 8; ++pass)
                for (unsigned packed = 0; packed < 256; ++packed)
                    grayPairLookup[pass][packed] =
                        (matrix[(packed >> 4U) * 8U + pass] << 2U) | matrix[(packed & 15U) * 8U + pass];
            grayLookupReady = true;
        }
        constexpr size_t planeBytes = 960U * 540U / 4U;
        if (!grayPulsePlanes)
            grayPulsePlanes = static_cast<uint8_t *>(heap_caps_malloc(planeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!grayPulsePlanes)
            return false;
        memset(grayPulsePlanes, 0, planeBytes);
        memset(grayActiveRows, 0, sizeof(grayActiveRows));
        if (!baselineValid)
            memset(_state.pCurrent, 0xFF, 960U * 540U / 2U);
        changed = 0;
#if INKHUD_BOOKREADER_PERF_LOG
        uint32_t inputHash = 2166136261U, targetHash = 2166136261U;
        uint32_t targetCounts[4] = {}, transitions[16] = {};
        uint32_t packedCounts[256] = {};
#endif

        // Batch external-RAM transfers; compare packed pixels in local row buffers.
        uint8_t oldRow[480], newRow[480];
        const size_t rowBytes = width / 2U;
        for (uint16_t y = 0; y < height; ++y) {
            uint8_t *current = _state.pCurrent + (top + y) * 480U + left / 2U;
            const uint8_t *next = image + y * (width / 2U);
            memcpy(oldRow, current, rowBytes);
            memcpy(newRow, next, rowBytes);
#if !INKHUD_BOOKREADER_PERF_LOG
            if (baselineValid && memcmp(oldRow, newRow, rowBytes) == 0)
                continue;
#endif
            uint8_t *mask = grayPulsePlanes + (top + y) * 240U;
            uint8_t pendingMask = 0;
            bool rowChanged = false;
            for (uint16_t x = 0; x < width; x += 2) {
                const uint8_t oldPacked = oldRow[x / 2U];
                const uint8_t input = newRow[x / 2U];
                const uint8_t newPacked = input;
#if INKHUD_BOOKREADER_PERF_LOG
                inputHash = (inputHash ^ input) * 16777619U;
                targetHash = (targetHash ^ newPacked) * 16777619U;
                ++packedCounts[newPacked];
#endif
                const unsigned physicalX = left + x;
                if (!baselineValid || oldPacked != newPacked) {
                    const uint8_t difference = oldPacked ^ newPacked;
                    const bool highChanged = !baselineValid || (difference & 0xF0U);
                    const bool lowChanged = !baselineValid || (difference & 0x0FU);
#if INKHUD_BOOKREADER_PERF_LOG
                    if (highChanged)
                        ++transitions[(oldPacked >> 6U) * 4U + (newPacked >> 6U)];
                    if (lowChanged)
                        ++transitions[((oldPacked & 15U) / 4U) * 4U + (newPacked & 15U) / 4U];
#endif
                    const uint8_t pairMask = (highChanged ? 12U : 0U) | (lowChanged ? 3U : 0U);
                    pendingMask |= pairMask << ((physicalX & 2U) ? 0U : 4U);
                    changed += highChanged + lowChanged;
                    rowChanged = true;
                }
                if ((physicalX & 2U) || x + 2U == width) {
                    if (pendingMask)
                        mask[physicalX / 4U] = pendingMask;
                    pendingMask = 0;
                }
            }
            if (rowChanged) {
                grayActiveRows[top + y] = true;
                memcpy(current, newRow, rowBytes);
            }
        }
#if INKHUD_BOOKREADER_PERF_LOG
        for (unsigned packed = 0; packed < 256U; ++packed) {
            targetCounts[packed >> 6U] += packedCounts[packed];
            targetCounts[(packed & 15U) >> 2U] += packedCounts[packed];
        }
        LOG_INFO("[BookReaderDraw] panel prepare inputHash=%08lx targetHash=%08lx baseline=%u "
                 "targetBands0-3,4-7,8-11,12-15=%lu,%lu,%lu,%lu",
                 static_cast<unsigned long>(inputHash), static_cast<unsigned long>(targetHash), baselineValid,
                 static_cast<unsigned long>(targetCounts[0]), static_cast<unsigned long>(targetCounts[1]),
                 static_cast<unsigned long>(targetCounts[2]), static_cast<unsigned long>(targetCounts[3]));
        for (unsigned oldLevel = 0; oldLevel < 4; ++oldLevel)
            LOG_INFO("[BookReaderDraw] transitions oldBand=%u toBands=%lu,%lu,%lu,%lu", oldLevel,
                     static_cast<unsigned long>(transitions[oldLevel * 4U]),
                     static_cast<unsigned long>(transitions[oldLevel * 4U + 1U]),
                     static_cast<unsigned long>(transitions[oldLevel * 4U + 2U]),
                     static_cast<unsigned long>(transitions[oldLevel * 4U + 3U]));
#endif
        return true;
    }

    int updatePanelGray()
    {
        if (dmaFault || !grayPulsePlanes || !grayLookupReady || !_state.dma_buf)
            return BBEP_ERROR_BAD_PARAMETER;
#if INKHUD_BOOKREADER_PERF_LOG
        uint32_t passStarts[8] = {};
#endif
        const int powerRc = einkPower(1);
        if (powerRc != BBEP_SUCCESS) {
            einkPower(0);
            return powerRc;
        }
        int rc = BBEP_SUCCESS;
        for (unsigned pass = 0; pass < 8 && rc == BBEP_SUCCESS; ++pass) {
#if INKHUD_BOOKREADER_PERF_LOG
            passStarts[pass] = millis();
#endif
            rc = writePulseFrame(grayPulsePlanes, grayPairLookup[pass]);
        }
#if INKHUD_BOOKREADER_PERF_LOG
        const uint32_t neutralStart = millis();
#endif
        if (rc == BBEP_SUCCESS)
            rc = writePulseFrame(nullptr);
#if INKHUD_BOOKREADER_PERF_LOG
        const uint32_t powerOffStart = millis();
#endif
        const int offRc = einkPower(0);
#if INKHUD_BOOKREADER_PERF_LOG
        // Serial output must not stretch the powered waveform between phases.
        for (unsigned pass = 0; pass < 8; ++pass)
            LOG_INFO("[BookReaderDraw] panel-native streamed pass=%u at=%lu", pass, static_cast<unsigned long>(passStarts[pass]));
        LOG_INFO("[BookReaderDraw] neutral at=%lu power-off at=%lu", static_cast<unsigned long>(neutralStart),
                 static_cast<unsigned long>(powerOffStart));
        LOG_INFO("[BookReaderDraw] panel phase power-off END at=%lu rc=%d powerRc=%d", static_cast<unsigned long>(millis()), rc,
                 offRc);
#endif
        return rc == BBEP_SUCCESS ? offRc : rc;
    }

#endif

  private:
#if INKHUD_BOOKREADER_GRAYSCALE
    uint8_t *grayPulsePlanes = nullptr;
    uint8_t grayPairLookup[8][256] = {};
    bool grayLookupReady = false;
    bool grayActiveRows[540] = {};
#endif
    bool dmaFault = false;

    int finishMonochromePixels(uint8_t *plane, unsigned passes, bool cleanup = false)
    {
        // Maintenance restores both endpoints without inverting the visible image.
#if INKHUD_BOOKREADER_PERF_LOG
        const uint32_t prepareStart = millis();
        uint32_t whitePixels = 0;
#endif
        uint8_t active = 0;
        for (size_t i = 0; i < 960U * 540U / 4U; ++i) {
            uint8_t finish = plane[i] & 0xAA;
            if (cleanup) {
                const uint8_t pixels = (_state.pCurrent[i / 2U] >> ((i & 1U) ? 0U : 4U)) & 15U;
                finish = ((pixels & 8U) << 4U) | ((pixels & 4U) << 3U) | ((pixels & 2U) << 2U) | ((pixels & 1U) << 1U);
#if INKHUD_BOOKREADER_PERF_LOG
                whitePixels += __builtin_popcount(static_cast<unsigned>(finish));
#endif
                finish |= (finish ^ 0xAAU) >> 1U;
            }
            plane[i] = finish;
            active |= plane[i];
#if INKHUD_BOOKREADER_PERF_LOG
            if (!cleanup)
                whitePixels += __builtin_popcount(static_cast<unsigned>(finish));
#endif
        }
        int rc = BBEP_SUCCESS;
#if INKHUD_BOOKREADER_PERF_LOG
        LOG_INFO("[BookReaderDraw] monochrome finish BEGIN at=%lu whitePixels=%lu passes=%u prepareMs=%lu mode=%d cleanup=%u",
                 static_cast<unsigned long>(millis()), static_cast<unsigned long>(whitePixels), active ? passes : 0U,
                 static_cast<unsigned long>(millis() - prepareStart), _state.mode, cleanup);
#endif
        if (active)
            for (unsigned pass = 0; pass < passes && rc == BBEP_SUCCESS; ++pass) {
                if (cleanup && pass == static_cast<unsigned>(_state.iFullPasses))
                    for (size_t i = 0; i < 960U * 540U / 4U; ++i)
                        plane[i] &= 0xAAU;
                rc = writePulseFrame(plane);
            }
#if INKHUD_BOOKREADER_PERF_LOG
        LOG_INFO("[BookReaderDraw] monochrome finish END rc=%d", rc);
#endif
        return rc;
    }

    bool waitForDma()
    {
        if (dma_is_done)
            return true;
        const uint32_t start = Time::getMillis();
        while (!dma_is_done) {
            if (Throttle::hasElapsed(start, 250U)) {
                dmaFault = true;
                LOG_ERROR("ED047TC1 DMA timeout; display transfers stopped");
                return false;
            }
            delayMicroseconds(1);
        }
        return true;
    }

    int writePulseFrame(const uint8_t *plane, const uint8_t *grayLookup = nullptr)
    {
        if (!waitForDma())
            return BBEP_IO_ERROR;
        bbepRowControl(&_state, ROW_START);
        unsigned offset = 0;
        for (unsigned row = 0; row < 540; ++row) {
            // Alternate buffers keep the active DMA row untouched.
            uint8_t *out = _state.dma_buf + offset;
#if INKHUD_BOOKREADER_GRAYSCALE
            if (grayLookup && grayActiveRows[row]) {
                const uint8_t *source = _state.pCurrent + row * 480U;
                const uint8_t *mask = plane + row * 240U;
                for (unsigned x = 0; x < 240; ++x)
                    out[x] = mask[x] ? ((grayLookup[source[x * 2U]] << 4U) | grayLookup[source[x * 2U + 1U]]) & mask[x] : 0U;
            } else if (grayLookup)
                memset(out, 0, 240);
            else
#endif
                if (plane)
                memcpy(out, plane + row * 240U, 240);
            else
                memset(out, 0, 240);
            if (!waitForDma())
                return BBEP_IO_ERROR;
            bbepWriteRow(&_state, out, 240, row != 0);
            offset ^= 240U;
        }
        if (!waitForDma())
            return BBEP_IO_ERROR;
        delayMicroseconds(230);
        return BBEP_SUCCESS;
    }
};
} // namespace

void ED047TC1::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    // Parallel display - SPI parameters are not used
    (void)spi;
    (void)pin_dc;
    (void)pin_cs;
    (void)pin_busy;
    (void)pin_rst;

    SafeFastEPD *safeEpaper = new SafeFastEPD;
    epaper = safeEpaper;

    int initRc = BBEP_ERROR_BAD_PARAMETER;
#if defined(T5_S3_EPAPER_PRO_V1)
    initRc = epaper->initPanel(BB_PANEL_LILYGO_T5PRO, 20000000);
#elif defined(T5_S3_EPAPER_PRO_V2)
    initRc = epaper->initPanel(BB_PANEL_LILYGO_T5PRO_V2, 20000000);
    // Initialize all PCA9535 port-0 pins as outputs / HIGH
    for (int i = 0; i < 8; i++) {
        epaper->ioPinMode(i, OUTPUT);
        epaper->ioWrite(i, HIGH);
    }
    // On this board, the physical side key is labeled IO48; electrically it maps to PCA9535 IO12 (bit 2 on port-1).
    // FastEPD's generic V7 init drives 8..13 as outputs; force IO12 back to input
    // so variant touch-control polling can read the key reliably.
    epaper->ioPinMode(10, INPUT);
#else
#error "ED047TC1 driver: unsupported variant - define T5_S3_EPAPER_PRO_V1 or T5_S3_EPAPER_PRO_V2"
#endif

    if (initRc != BBEP_SUCCESS) {
        LOG_ERROR("ED047TC1 initPanel failed rc=%d", initRc);
        return;
    }

    safeEpaper->installSafePowerHandler();

    const int modeRc = epaper->setMode(BB_MODE_1BPP);
    if (modeRc != BBEP_SUCCESS) {
        LOG_WARN("ED047TC1 setMode failed rc=%d", modeRc);
        return;
    }
    panelReady = true;
    // FastEPD clearWhite()/backupPlane() overrun the aliased previous plane.
    epaper->fillScreen(BBEP_WHITE);
    const int fullRc = epaper->fullUpdate(CLEAR_SLOW, false);
    monochromeBaselineValid = fullRc == BBEP_SUCCESS;
    failed = !monochromeBaselineValid;
    if (failed)
        LOG_WARN("ED047TC1 initial fullUpdate failed rc=%d", fullRc);
    LOG_INFO("ED047TC1 panel profile: clock=20MHz partialPasses=4 fullPasses=5");
    TaskHandle_t task = nullptr;
    // Keep panel timing off the input/rendering core on dual-core boards.
    const BaseType_t refreshCore = portNUM_PROCESSORS > 1 ? (xPortGetCoreID() ^ 1) : tskNO_AFFINITY;
    if (xTaskCreatePinnedToCore(runUpdates, "eink-refresh", 8192, this, 2, &task, refreshCore) == pdPASS) {
        updateTask = task;
        LOG_INFO("ED047TC1 background refresh enabled core=%d", static_cast<int>(refreshCore));
    } else
        LOG_WARN("ED047TC1 refresh task unavailable; using blocking updates");
}

void ED047TC1::runUpdates(void *context)
{
    auto *display = static_cast<ED047TC1 *>(context);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        display->updateResult = display->driveUpdate();
        display->updateComplete.store(true, std::memory_order_release);
        concurrency::mainDelay.interrupt();
    }
}

void ED047TC1::queueUpdate(UpdateTypes type, bool grayscale, bool leavingGrayscale)
{
    pendingType = !grayscale && !monochromeBaselineValid ? FULL : type;
    pendingGrayscale = grayscale;
    pendingLeavingGrayscale = leavingGrayscale;
    monochromeBaselineValid = false;
#if INKHUD_BOOKREADER_GRAYSCALE
    grayscaleBaselineValid = false;
#endif
    updateStartedMs = Time::getMillis();
    failed = false;
    updateComplete.store(false, std::memory_order_relaxed);
    beginPolling(5, 0);
    if (updateTask)
        xTaskNotifyGive(static_cast<TaskHandle_t>(updateTask));
    else {
        updateResult = driveUpdate();
        updateComplete.store(true, std::memory_order_release);
    }
}

bool ED047TC1::isUpdateDone()
{
    if (!updateComplete.load(std::memory_order_acquire))
        return false;
    monochromeBaselineValid = !pendingGrayscale && updateResult == BBEP_SUCCESS;
#if INKHUD_BOOKREADER_GRAYSCALE
    grayscaleBaselineValid = pendingGrayscale && updateResult == BBEP_SUCCESS;
#endif
    if (static_cast<SafeFastEPD *>(epaper)->hasDmaFault())
        panelReady = false;
    failed = updateResult != BBEP_SUCCESS;
    LOG_DEBUG("[EInkPerf] refresh complete gray=%u type=%u ms=%lu rc=%d", pendingGrayscale, static_cast<unsigned>(pendingType),
              static_cast<unsigned long>(Time::getMillis() - updateStartedMs), updateResult);
    return !failed;
}

void ED047TC1::update(uint8_t *imageData, UpdateTypes type)
{
    if (!panelReady || !imageData || busy() || !updateComplete.load(std::memory_order_acquire))
        return;
    bool leavingGrayscale = false;
#if INKHUD_BOOKREADER_GRAYSCALE
    leavingGrayscale = epaper->getMode() == BB_MODE_4BPP;
    grayscaleBaselineValid = false;
#endif

    if (epaper->getMode() != BB_MODE_1BPP) {
        monochromeBaselineValid = false;
        const int rc = epaper->setMode(BB_MODE_1BPP);
        if (rc != BBEP_SUCCESS) {
            failed = true;
            LOG_WARN("ED047TC1 monochrome setMode failed rc=%d", rc);
            return;
        }
    }

    // InkHUD renders into a DISPLAY_WIDTH × DISPLAY_HEIGHT safe-area buffer.
    // We need to place that into the centre of the physical 960×540 FastEPD buffer,
    // leaving blank margins at every edge to avoid the panel's inactive border.
    const uint32_t srcRowBytes = (DISPLAY_WIDTH + 7) / 8; // bytes per row in InkHUD buffer (116)
    const uint32_t dstRowBytes = (960 + 7) / 8;           // bytes per row in physical buffer (120)
    const uint32_t dstTotalRows = 540;

    uint8_t *cur = epaper->currentBuffer();

    // Fill physical buffer with white (0xFF = white in FastEPD 1bpp)
    memset(cur, 0xFF, dstRowBytes * dstTotalRows);

    // Copy each InkHUD row into the physical buffer with horizontal + vertical offsets
    for (uint32_t row = 0; row < DISPLAY_HEIGHT; row++) {
        const uint8_t *srcRow = imageData + row * srcRowBytes;
        uint8_t *dstRow = cur + (row + V_OFFSET_TOP) * dstRowBytes + H_OFFSET_BYTES;
        memcpy(dstRow, srcRow, srcRowBytes);
    }

    if (type != FULL && type != CLEANUP && monochromeBaselineValid &&
        memcmp(cur, epaper->previousBuffer(), dstRowBytes * dstTotalRows) == 0) {
        failed = false;
        return;
    }
    queueUpdate(type, false, leavingGrayscale);
}

int ED047TC1::driveUpdate()
{
#if INKHUD_BOOKREADER_GRAYSCALE
    if (pendingGrayscale) {
        const int rc = static_cast<SafeFastEPD *>(epaper)->updatePanelGray();
        LOG_DEBUG("[EInkPerf] grayscale drive ms=%lu rc=%d", static_cast<unsigned long>(Time::getMillis() - updateStartedMs), rc);
        return rc;
    }
#endif
    if (pendingType == FULL)
        return epaper->fullUpdate(pendingLeavingGrayscale ? CLEAR_WHITE : CLEAR_SLOW, false);
    else {
        const uint32_t startMs = millis();
        uint32_t blackToWhite = 0, whiteToBlack = 0;
        const uint8_t *cur = epaper->currentBuffer();
        const uint8_t *previous = epaper->previousBuffer();
        for (size_t i = 0; i < 960U * 540U / 8U; ++i) {
            blackToWhite += __builtin_popcount(static_cast<unsigned>(cur[i] & ~previous[i]));
            whiteToBlack += __builtin_popcount(static_cast<unsigned>(~cur[i] & previous[i]));
        }
        auto *safeEpaper = static_cast<SafeFastEPD *>(epaper);
        const bool cleanup = pendingType == CLEANUP;
        const int rc = safeEpaper->partialUpdateWithWhiteFinish(blackToWhite, cleanup);
        LOG_DEBUG("[EInkPerf] %s blackToWhite=%lu whiteToBlack=%lu whiteExtraPasses=%u ms=%lu rc=%d",
                  cleanup ? "CLEANUP" : "FAST", static_cast<unsigned long>(blackToWhite),
                  static_cast<unsigned long>(whiteToBlack), (blackToWhite || cleanup) ? SafeFastEPD::whiteFinishPasses : 0U,
                  static_cast<unsigned long>(millis() - startMs), rc);
        return rc;
    }
}

#if INKHUD_BOOKREADER_GRAYSCALE
void ED047TC1::updateGrayscale(uint8_t *imageData)
{
    if (!panelReady || !imageData || busy() || !updateComplete.load(std::memory_order_acquire))
        return;
#if INKHUD_BOOKREADER_PERF_LOG
    const uint32_t startMs = millis();
#endif

    auto *safeEpaper = static_cast<SafeFastEPD *>(epaper);
    if (epaper->getMode() != BB_MODE_4BPP)
        grayscaleBaselineValid = monochromeBaselineValid && safeEpaper->promoteMonochromeBaseline();
    monochromeBaselineValid = false;
    const int modeRc = epaper->setMode(BB_MODE_4BPP);
    if (modeRc != BBEP_SUCCESS) {
        failed = true;
        grayscaleBaselineValid = false;
        LOG_WARN("ED047TC1 grayscale setMode failed rc=%d", modeRc);
        return;
    }

    uint32_t changedPixels = 0;
    const bool establishBaseline = !grayscaleBaselineValid;
    if (!safeEpaper->preparePanelGrayUpdate(imageData, DISPLAY_WIDTH, DISPLAY_HEIGHT, H_OFFSET_BYTES * 8U, V_OFFSET_TOP,
                                            grayscaleBaselineValid, changedPixels)) {
        failed = true;
        LOG_WARN("ED047TC1 panel-native grayscale preparation failed; retaining visible frame");
        return;
    }
    if (!establishBaseline && !changedPixels) {
        failed = false;
#if INKHUD_BOOKREADER_PERF_LOG
        LOG_INFO("[BookReaderPerf] gray panel skipped unchanged ms=%lu", static_cast<unsigned long>(millis() - startMs));
#endif
        return;
    }
#if INKHUD_BOOKREADER_PERF_LOG
    LOG_INFO("[BookReaderPerf] gray panel BEGIN clear=none waveform=panel-native shades=16 baseline=%u "
             "changed=%lu prepareMs=%lu",
             !establishBaseline, static_cast<unsigned long>(changedPixels), static_cast<unsigned long>(millis() - startMs));
#endif
    queueUpdate(FAST, true);
}
#endif

#endif // T5_S3_EPAPER_PRO
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
