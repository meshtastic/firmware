#include "NavCombos.h"

#if defined(EINK_RA01S_GP02)

#include "configuration.h"
#include "input/InputBroker.h"
#include "input/SixKeyNavInput.h"

#ifdef EINK_RA01S_GP02_INKHUD
#include "graphics/niche/Drivers/EInk/EInk.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

// ---------------------------------------------------------------------------
// InkHUD display power management
// Mirrors the anti-leakage logic in variant_shutdown() for runtime use.
// ---------------------------------------------------------------------------
static NicheGraphics::Drivers::EInk *s_inkHudDriver = nullptr;
static SPIClass *s_inkHudSpi = nullptr;
static bool s_inkHudDisplayOff = false;

void setInkHudDriver(NicheGraphics::Drivers::EInk *driver, SPIClass *spi)
{
    s_inkHudDriver = driver;
    s_inkHudSpi = spi;
}

bool inkHudIsDisplayOff()
{
    return s_inkHudDisplayOff;
}

// Restore panel power, reinitialize SPI1 and SSD1680, then force a full refresh.
// Called on any keypress while display is off (wakeup), or from navComboDown.
void inkHudWakeup()
{
    if (!s_inkHudDisplayOff || !s_inkHudDriver || !s_inkHudSpi)
        return;

    LOG_INFO("NavCombo: display wakeup - EN HIGH + SPI1.begin + SSD1680 reinit");

    // 1. Restore panel LDO (PIN_EINK_POWER stays OUTPUT from setupNicheGraphics).
    //    PIN_EINK_POWER direction is preserved; only level changes.
    digitalWrite(PIN_EINK_POWER, EINK_POWER_ACTIVE);
    delay(50); // LDO stabilize + SSD1680 POR completion

    // 2. Re-init SPI1 - pins were tristated during power-off.
    //    SPI1.begin() re-configures the nRF52 SPIM1 peripheral and sets
    //    SCLK / MOSI back to alternate-function outputs.
    s_inkHudSpi->begin();

    // 3. Re-init display controller.
    //    SSD16XX::begin():
    //      - sets pin directions (DC=OUT, CS=OUT, BUSY=IN, RST=INPUT_PULLUP)
    //      - calls reset(): RST LOW→HIGH + SW reset (0x12) + wait
    s_inkHudDriver->begin(s_inkHudSpi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    s_inkHudDisplayOff = false;

    // 4. Force a FULL display refresh to restore content.
    //    After a power cycle the controller's image RAM is blank; a FULL
    //    update pushes InkHUD's current image buffer to the panel.
    using namespace NicheGraphics;
    InkHUD::InkHUD::getInstance()->forceUpdate(Drivers::EInk::FULL, true);

    LOG_INFO("NavCombo: display wakeup complete");
}

// Safely shut down the display panel without leakage current.
// Mirrors the variant_shutdown() sequence: tristate driven pins, then cut EN.
static void inkHudDisplayPowerOff()
{
    if (s_inkHudDisplayOff)
        return;

    using namespace NicheGraphics;
    auto *inkhud = InkHUD::InkHUD::getInstance();

    LOG_INFO("NavCombo: display power off - await update, tristate SPI1, cut EN");

    // 1. Wait for any in-progress display update to finish before touching pins.
    //    SSD16XX::finalizeUpdate() sends deepSleep(0x10) after the update;
    //    the controller is already in deep sleep, but we must not cut power
    //    while a SPIM transaction is in progress.
    inkhud->awaitUpdate();

    // 2. Tristate ALL SSD1680-connected MCU pins BEFORE cutting EN.
    //    Root cause: DC / CS / RST idles HIGH (last driven state or pull-up).
    //    When EN cuts the LDO these HIGH pins backpower the SSD1680 through
    //    its I/O ESD protection diodes - identical hazard as in variant_shutdown().
    //    Order is critical: tristate FIRST, cut power LAST.
    pinMode(PIN_EINK_DC, INPUT);            // was OUTPUT HIGH (data mode)
    pinMode(PIN_EINK_RES, INPUT);           // was INPUT_PULLUP idle HIGH
    pinMode(PIN_EINK_CS, INPUT);            // was OUTPUT HIGH (deselected)
    pinMode(PIN_EINK_MOSI, INPUT);          // last SPI bit state unknown
    pinMode(PIN_EINK_SCLK, INPUT);          // SPI MODE0 idle LOW, but tristate for safety
    pinMode(PIN_EINK_BUSY, INPUT_PULLDOWN); // pull-down so wakeup reinit won't hang on stale HIGH

    // 3. Cut panel LDO - all driven pins are now high-Z.
    //    PIN_EINK_POWER direction stays OUTPUT (set once in setupNicheGraphics).
    digitalWrite(PIN_EINK_POWER, !EINK_POWER_ACTIVE);

    s_inkHudDisplayOff = true;
    LOG_INFO("NavCombo: display off ~0 uA leakage");
}
#endif // EINK_RA01S_GP02_INKHUD

// ---------------------------------------------------------------------------
// Fn held ≥ 4 s → shutdown
// Wakes the display first (if powered off via Fn+Down) so that
// Events::beforeDeepSleep() can render the shutdown graphic on a live panel.
// Without the wakeup: EN is LOW, SPI1 pins are tristated, SSD16XX::wait()
// returns immediately (BUSY pin pulled LOW), SPI transfers go nowhere.
// ---------------------------------------------------------------------------
void navComboFnHeld()
{
    LOG_INFO("NavCombo: Fn held 4s SHUTDOWN");
#ifdef EINK_RA01S_GP02_INKHUD
    if (inkHudIsDisplayOff()) {
        LOG_INFO("NavCombo: waking display for shutdown screen");
        inkHudWakeup();
    }
#endif
    if (sixKeyNavInput)
        sixKeyNavInput->fireEvent(INPUT_BROKER_SHUTDOWN);
}

// ---------------------------------------------------------------------------
// Fn + Up → toggle Bluetooth (persistent, triggers reboot)
// ---------------------------------------------------------------------------
void navComboUp()
{
    LOG_INFO("NavCombo: BLE toggle -> saveToDisk + reboot scheduled");
#ifdef EINK_RA01S_GP02_INKHUD
    // Wake display before BLE toggle so Events::beforeReboot() can render
    // the intermediate screen (applyingChanges banner, etc.).
    if (inkHudIsDisplayOff()) {
        LOG_INFO("NavCombo: waking display for BLE toggle reboot screen");
        inkHudWakeup();
    }
#endif
    if (sixKeyNavInput)
        sixKeyNavInput->fireKbEvent(INPUT_BROKER_MSG_BLUETOOTH_TOGGLE);
}

// ---------------------------------------------------------------------------
// Fn + Down → screen off / close overlay menu
//
// InkHUD mode (EINK_RA01S_GP02_INKHUD):
//   • Display already off    → wake it back up
//   • System applet active   → exitShort() (closes menu / keyboard / overlay)
//   • Main view, no overlay  → cut panel power (EN LOW, tristate SPI1 pins)
//
// Screen mode:
//   • fire INPUT_BROKER_CANCEL → Screen.cpp handles screen-off sequence
// ---------------------------------------------------------------------------
void navComboDown()
{
#ifdef EINK_RA01S_GP02_INKHUD
    using namespace NicheGraphics;
    auto *inkhud = InkHUD::InkHUD::getInstance();

    if (s_inkHudDisplayOff) {
        // Display is off - Fn+Down acts as wakeup.
        LOG_INFO("NavCombo: Fn+Down - display wakeup (was off)");
        inkHudWakeup();
        return;
    }

    // Check if any system applet is currently consuming input
    // (true when menu, keyboard, or notification overlay is open).
    bool uiOverlayActive = false;
    for (auto *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            uiOverlayActive = true;
            break;
        }
    }

    if (uiOverlayActive) {
        // Close the active overlay (menu exit, keyboard cancel, etc.).
        LOG_INFO("NavCombo: Fn+Down - close UI overlay");
        inkhud->exitShort();
    } else {
        // Main view - power off the display panel.
        inkHudDisplayPowerOff();
    }
#else
    // Standard Screen variant: fire CANCEL → Screen handles screen-off.
    LOG_INFO("NavCombo: screen off");
    if (sixKeyNavInput)
        sixKeyNavInput->fireEvent(INPUT_BROKER_CANCEL);
#endif
}

// ---------------------------------------------------------------------------
// Fn + Right → toggle GPS power (persistent)
// ---------------------------------------------------------------------------
void navComboRight()
{
    LOG_INFO("NavCombo: GPS toggle");
    if (sixKeyNavInput)
        sixKeyNavInput->fireEvent(INPUT_BROKER_GPS_TOGGLE);
}

#endif // EINK_RA01S_GP02
