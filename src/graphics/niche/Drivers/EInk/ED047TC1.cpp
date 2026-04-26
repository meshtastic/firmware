/*

    NicheGraphics parallel E-Ink driver for the LilyGo T5-S3-ePaper-Pro (ED047TC1).

    InkHUD buffer format : 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white
    FastEPD buffer format: 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white

    Both formats share the same pixel layout and polarity (1 = white, 0 = black).
    The InkHUD safe-area buffer (944×523) is copied into the centre of the physical
    960×540 FastEPD buffer so content clears the panel's inactive edge border.
    See ED047TC1.h for the H_OFFSET_BYTES / V_OFFSET_TOP / V_OFFSET_BOTTOM constants.

*/

// Ruler diagnostic — uncomment to draw calibration lines at each physical edge.
// #define EINK_EDGE_LINES

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#ifdef T5_S3_EPAPER_PRO

#include "./ED047TC1.h"

#include "FastEPD.h"
#include "configuration.h"

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
    void installSafePowerHandler()
    {
#if defined(T5_S3_EPAPER_PRO_V2)
        _state.pfnEinkPower = safeEPDiyV7EinkPower;
#endif
    }
};
} // namespace

void ED047TC1::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    // Parallel display — SPI parameters are not used
    (void)spi;
    (void)pin_dc;
    (void)pin_cs;
    (void)pin_busy;
    (void)pin_rst;

    SafeFastEPD *safeEpaper = new SafeFastEPD;
    epaper = safeEpaper;

    int initRc = BBEP_ERROR_BAD_PARAMETER;
#if defined(T5_S3_EPAPER_PRO_V1)
    initRc = epaper->initPanel(BB_PANEL_LILYGO_T5PRO, 28000000);
#elif defined(T5_S3_EPAPER_PRO_V2)
    initRc = epaper->initPanel(BB_PANEL_LILYGO_T5PRO_V2, 28000000);
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
#error "ED047TC1 driver: unsupported variant — define T5_S3_EPAPER_PRO_V1 or T5_S3_EPAPER_PRO_V2"
#endif

    if (initRc != BBEP_SUCCESS) {
        LOG_ERROR("ED047TC1 initPanel failed rc=%d", initRc);
        return;
    }

    safeEpaper->installSafePowerHandler();

    const int modeRc = epaper->setMode(BB_MODE_1BPP);
    if (modeRc != BBEP_SUCCESS) {
        LOG_WARN("ED047TC1 setMode failed rc=%d", modeRc);
    }

    const int clearRc = epaper->clearWhite();
    if (clearRc != BBEP_SUCCESS) {
        LOG_WARN("ED047TC1 clearWhite failed rc=%d", clearRc);
    }

    const int fullRc = epaper->fullUpdate(true); // Blocking initial clear
    if (fullRc != BBEP_SUCCESS) {
        LOG_WARN("ED047TC1 initial fullUpdate failed rc=%d", fullRc);
    }
}

void ED047TC1::update(uint8_t *imageData, UpdateTypes type)
{
    if (!epaper)
        return;

    // InkHUD renders into a DISPLAY_WIDTH × DISPLAY_HEIGHT safe-area buffer.
    // We need to place that into the centre of the physical 960×540 FastEPD buffer,
    // leaving blank margins at every edge to avoid the panel's inactive border.
    const uint32_t srcRowBytes = (DISPLAY_WIDTH + 7) / 8; // bytes per row in InkHUD buffer (118)
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

#ifdef EINK_EDGE_LINES
    // Draw a 1px black box at the exact boundary of the safe area within the
    // physical buffer. If the margins are correct, all 4 lines should be
    // fully visible and right at the edge of the usable display area.

    auto setPixelBlack = [&](uint32_t col, uint32_t row) { cur[row * dstRowBytes + col / 8] &= ~(0x80 >> (col % 8)); };

    const uint32_t safeX = H_OFFSET_BYTES * 8;
    const uint32_t safeY = V_OFFSET_TOP;
    const uint32_t safeW = DISPLAY_WIDTH;
    const uint32_t safeH = DISPLAY_HEIGHT;

    // Top edge: horizontal line at safeY
    for (uint32_t col = safeX; col < safeX + safeW; col++)
        setPixelBlack(col, safeY);

    // Bottom edge: horizontal line at safeY + safeH - 1
    for (uint32_t col = safeX; col < safeX + safeW; col++)
        setPixelBlack(col, safeY + safeH - 1);

    // Left edge: vertical line at safeX
    for (uint32_t row = safeY; row < safeY + safeH; row++)
        setPixelBlack(safeX, row);

    // Right edge: vertical line at safeX + safeW - 1
    for (uint32_t row = safeY; row < safeY + safeH; row++)
        setPixelBlack(safeX + safeW - 1, row);
#endif

    if (type == FULL) {
        epaper->fullUpdate(CLEAR_SLOW, false);
        epaper->backupPlane(); // Sync pPrevious so next partialUpdate has a correct baseline
    } else {
        // FAST: true partial update - compares pCurrent vs pPrevious and only applies
        // update waveform to rows that changed. partialUpdate() updates pPrevious.
        epaper->partialUpdate(false, 0, dstTotalRows - 1);
    }
}

#endif // T5_S3_EPAPER_PRO
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
