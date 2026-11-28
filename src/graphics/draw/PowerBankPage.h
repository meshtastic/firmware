#pragma once

#include "configuration.h"
#include "power/PowerBank.h"
#if HAS_SCREEN && HAS_POWERBANK
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#endif

namespace graphics
{
namespace PowerBankPage
{
constexpr uint8_t PAGE_LINES = 5;
constexpr uint8_t PAGE_LINE_CHARS = 32;

// The body lines of the power bank page; returns how many were written.
uint8_t formatLines(bool haveStatus, const PowerBankStatus &status, bool haveDetail, const PowerBankDetail &detail,
                    char lines[PAGE_LINES][PAGE_LINE_CHARS]);

#if HAS_SCREEN && HAS_POWERBANK
void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#if USE_POWER_BANK_GRAPHIC_PAGE
// Full-screen colour layout for a 220x128 TFT; boards that set USE_POWER_BANK_GRAPHIC_PAGE show it instead.
void drawGraphicFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#endif
#endif
} // namespace PowerBankPage
} // namespace graphics
