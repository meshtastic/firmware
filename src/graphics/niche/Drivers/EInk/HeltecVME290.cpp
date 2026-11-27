#include "./HeltecVME290.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/niche/Utils/FlashData.h"

#include <GFX.h>
#include <cstring>

#include "graphics/niche/Fonts/FreeSans12pt_Win1252.h"
#include "graphics/niche/Fonts/FreeSans6pt_Win1252.h"
#include "graphics/niche/Fonts/FreeSans9pt_Win1252.h"

using namespace NicheGraphics::Drivers;

HeltecVME290 *HeltecVME290::activeInstance = nullptr;

namespace
{

// Custom LUT from DEPG0290BNS800 - used for flex 15-22243-M0282 FAST.
static const uint8_t LUT_FAST[] = {
    0x40, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // B2B
    0x00, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // B2W
    0x00, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // W2B
    0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // W2W
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // VCOM

    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //

    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
};

struct WaveformStore {
    uint8_t mode = static_cast<uint8_t>(HeltecVME290Waveform::Unset);
};

// Heltec-default landscape (296 x 128): buttons along the top edge.
// Same controller map as classic E290 (GxEPD2 rot3 + DISPLAY_FLIP_SCREEN).
class RotatedCanvas : public GFX
{
  public:
    RotatedCanvas(uint16_t drvW, uint16_t drvH, uint8_t *buf)
        : GFX(drvH, drvW), driverWidth(drvW), driverHeight(drvH), buffer(buf),
          rowBytes(static_cast<uint16_t>(((drvW - 1) / 8) + 1))
    {
        memset(buffer, 0xFF, rowBytes * driverHeight); // white
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        if (x < 0 || y < 0 || x >= width() || y >= height())
            return;

        int16_t cx = (driverWidth - 1) - y;
        int16_t cy = x;
        if (cx < 0 || cy < 0 || cx >= driverWidth || cy >= driverHeight)
            return;

        const uint32_t byteNum = (static_cast<uint32_t>(cy) * rowBytes) + (cx / 8);
        const uint8_t bitNum = 7 - (cx % 8);
        bitWrite(buffer[byteNum], bitNum, color ? 1 : 0);
    }

  private:
    uint16_t driverWidth;
    uint16_t driverHeight;
    uint8_t *buffer;
    uint16_t rowBytes;
};

// Button centers from left glass edge: USR 3.8mm, AUX 10.8mm (296px / 66.9mm).
// Tuned -1.2mm from measured 5/12mm after on-device check.
constexpr int16_t kUsrArrowX = 17;
constexpr int16_t kAuxArrowX = 48;

static void drawUpArrow(GFX &g, int16_t tipX, int16_t tipY)
{
    constexpr int16_t head = 10;
    constexpr int16_t stem = 14;
    g.fillTriangle(tipX, tipY, tipX - head / 2, tipY + head, tipX + head / 2, tipY + head, 0);
    g.fillRect(tipX - 1, tipY + head, 3, stem, 0);
}

static bool buttonDown(uint8_t pin)
{
    return digitalRead(pin) == LOW;
}

static void waitRelease(uint8_t pin)
{
    while (buttonDown(pin))
        yield();
    delay(30);
}

} // namespace

void HeltecVME290::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    SSD16XX::begin(spi, pin_dc, pin_cs, pin_busy, pin_rst);
    loadMode();
}

void HeltecVME290::loadMode()
{
    WaveformStore stored;
    if (!FlashData<WaveformStore>::load(&stored, "e290wave"))
        return;

    if (stored.mode == static_cast<uint8_t>(HeltecVME290Waveform::CustomLut) ||
        stored.mode == static_cast<uint8_t>(HeltecVME290Waveform::OtpFast)) {
        mode = static_cast<HeltecVME290Waveform>(stored.mode);
        LOG_INFO("E290 waveform: %s", heltecVME290WaveformLabel(mode));
    }
}

void HeltecVME290::saveMode()
{
    WaveformStore stored;
    stored.mode = static_cast<uint8_t>(mode);
    FlashData<WaveformStore>::save(&stored, "e290wave");
}

void HeltecVME290::setWaveform(HeltecVME290Waveform next)
{
    mode = next;
    saveMode();
    LOG_INFO("E290 waveform set: %s", heltecVME290WaveformLabel(mode));
}

void HeltecVME290::toggleWaveform()
{
    if (mode == HeltecVME290Waveform::CustomLut)
        setWaveform(HeltecVME290Waveform::OtpFast);
    else
        setWaveform(HeltecVME290Waveform::CustomLut);
}

void HeltecVME290::update(uint8_t *imageData, UpdateTypes type)
{
    const uint8_t effective = heltecVME290EffectiveType(mode, static_cast<uint8_t>(type));
    SSD16XX::update(imageData, static_cast<UpdateTypes>(effective));
}

void HeltecVME290::updateOtpFast(uint8_t *imageData)
{
    // Cleared in finalizeUpdate once the async refresh finishes.
    forceOtpFast = true;
    SSD16XX::update(imageData, FAST);
}

void HeltecVME290::configVoltages()
{
    if (updateType != FAST || forceOtpFast || mode != HeltecVME290Waveform::CustomLut)
        return;

    sendCommand(0x04);
    sendData(0x41); // VSH1 15V
    sendData(0x00); // VSH2 NA
    sendData(0x32); // VSL -15V
}

void HeltecVME290::configWaveform()
{
    if (updateType == FAST && !forceOtpFast && mode == HeltecVME290Waveform::CustomLut) {
        sendCommand(0x3C);
        sendData(0x60); // hold border
        sendCommand(0x32);
        sendData(LUT_FAST, sizeof(LUT_FAST));
        return;
    }

    // OTP paths (FULL, OtpFast FAST, or forced OTP FAST)
    sendCommand(0x3C);
    sendData(0x05); // border follows LUT1
    sendCommand(0x18);
    sendData(0x80); // internal temp sensor
}

void HeltecVME290::configUpdateSequence()
{
    HeltecVME290Waveform seqMode = mode;
    if (forceOtpFast)
        seqMode = HeltecVME290Waveform::OtpFast;

    const uint8_t seq = heltecVME290UpdateSequence(seqMode, static_cast<uint8_t>(updateType));
    sendCommand(0x22);
    sendData(seq);
}

void HeltecVME290::detachFromUpdate()
{
    if (updateType == FAST) {
        if (forceOtpFast || mode == HeltecVME290Waveform::OtpFast)
            return beginPolling(50, 300);
        return beginPolling(50, 450);
    }
    return beginPolling(100, 2000);
}

void HeltecVME290::finalizeUpdate()
{
    if (updateType != FULL) {
        // CustomLut optimization: no writeNewImage needed (same as DEPG0290BNS800).
        if (!forceOtpFast && mode == HeltecVME290Waveform::CustomLut) {
            writeOldImage();
            sendCommand(0x7F);
            wait();
        } else {
            writeNewImage();
            writeOldImage();
            sendCommand(0x7F);
            wait();
        }
    }

    if (pin_rst != 0xFF)
        deepSleep();

    forceOtpFast = false;
}

void HeltecVME290::paintSelectFrame(uint8_t *buffer, bool arrowAtAux)
{
    // Frame 1: arrow under USR. Frame 2 (OTP FAST): arrow under AUX. User presses the named button.
    RotatedCanvas canvas(width, height, buffer);
    canvas.setTextColor(0);

    const int16_t arrowX = arrowAtAux ? kAuxArrowX : kUsrArrowX;
    drawUpArrow(canvas, arrowX, 2);

    canvas.setFont(&FreeSans9pt_Win1252);
    canvas.setCursor(72, 36);
    canvas.print("Screen detection");

    canvas.setFont(&FreeSans12pt_Win1252);
    canvas.setCursor(72, 68);
    canvas.print(arrowAtAux ? "Press AUX" : "Press USR");

    canvas.setFont(&FreeSans6pt_Win1252);
    canvas.setCursor(72, 100);
    canvas.print("Redo: hold AUX + RESET");
}

void HeltecVME290::paintBannerFrame(uint8_t *buffer, const char *label)
{
    RotatedCanvas canvas(width, height, buffer);
    canvas.setTextColor(0);

    canvas.setFont(&FreeSans12pt_Win1252);
    canvas.setCursor(8, 36);
    canvas.print("Display ready");

    canvas.setFont(&FreeSans9pt_Win1252);
    canvas.setCursor(8, 68);
    canvas.print("Using ");
    canvas.print(label);

    canvas.setFont(&FreeSans6pt_Win1252);
    canvas.setCursor(8, 100);
    canvas.print("Redo: hold AUX + RESET, or Options");
}

void HeltecVME290::showModeBanner()
{
    const uint16_t rowBytes = static_cast<uint16_t>(((width - 1) / 8) + 1);
    uint8_t *buffer = new uint8_t[rowBytes * height];
    paintBannerFrame(buffer, heltecVME290WaveformLabel(mode));
    SSD16XX::update(buffer, FULL);
    await();
    delete[] buffer;
}

HeltecVME290Waveform HeltecVME290::waitForSelectChoice(uint8_t userButtonPin, uint8_t auxButtonPin)
{
    pinMode(userButtonPin, INPUT_PULLUP);
    pinMode(auxButtonPin, INPUT_PULLUP);

    while (true) {
        if (buttonDown(userButtonPin)) {
            delay(40);
            if (buttonDown(userButtonPin)) {
                waitRelease(userButtonPin);
                return HeltecVME290Waveform::CustomLut; // parked arrow still under USR
            }
        }

        if (buttonDown(auxButtonPin)) {
            delay(40);
            if (buttonDown(auxButtonPin)) {
                waitRelease(auxButtonPin);
                return HeltecVME290Waveform::OtpFast; // parked arrow under AUX
            }
        }

        yield();
    }
}

void HeltecVME290::runPanelSelectIfNeeded(uint8_t userButtonPin, uint8_t auxButtonPin)
{
    pinMode(auxButtonPin, INPUT_PULLUP);
    delay(20);
    // Hold AUX through RESET to re-run the wizard. Do not use Aux long-press while InkHUD runs.
    if (buttonDown(auxButtonPin)) {
        delay(50);
        if (buttonDown(auxButtonPin)) {
            LOG_INFO("E290 panel select: Aux held at boot, re-running wizard");
            mode = HeltecVME290Waveform::Unset;
            waitRelease(auxButtonPin); // don't count this hold as the select answer
        }
    }

    if (mode != HeltecVME290Waveform::Unset)
        return;

    LOG_INFO("E290 panel select: starting");

    const uint16_t rowBytes = static_cast<uint16_t>(((width - 1) / 8) + 1);
    uint8_t *buffer = new uint8_t[rowBytes * height];

    paintSelectFrame(buffer, false); // arrow under USR
    SSD16XX::update(buffer, FULL);
    await();

    paintSelectFrame(buffer, true); // arrow under AUX
    updateOtpFast(buffer);
    await();

    setWaveform(waitForSelectChoice(userButtonPin, auxButtonPin));
    showModeBanner();

    delete[] buffer;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
