#pragma once

#if defined(EINK_RA01S_GP02)

/**
 * NavCombos - variant-specific combo-key behavior for eink-ra01s-gp02.
 *
 * Provides the combo-key handler functions to be assigned to
 * SixKeyNavInput::onCombo* function pointers in InputBroker::Init().
 *
 * All state is managed here, so SixKeyNavInput and InputBroker remain
 * decoupled from board-specific behavior.
 *
 * Combo mapping:
 *   Fn + Up    → toggle Bluetooth (persistent, triggers reboot)
 *   Fn + Down  → screen off
 *   Fn + Right → toggle GPS power (persistent)
 *   Fn held ≥ 4 s → shutdown
 */

void navComboUp();
void navComboDown();
void navComboRight();
void navComboFnHeld();

#ifdef EINK_RA01S_GP02_INKHUD
// Called from nicheGraphics.h after driver creation, before inkhud->begin().
// Stores the driver + SPI pointers needed for wakeup reinit.
namespace NicheGraphics
{
namespace Drivers
{
class EInk;
}
} // namespace NicheGraphics
class SPIClass;
void setInkHudDriver(NicheGraphics::Drivers::EInk *driver, SPIClass *spi);

// Display power state query (used by SixKeyNavInput to detect wakeup condition).
bool inkHudIsDisplayOff();

// Restore panel power + reinit SPI1 + reinit SSD1680 controller + force FULL refresh.
void inkHudWakeup();
#endif // EINK_RA01S_GP02_INKHUD

#endif // EINK_RA01S_GP02
