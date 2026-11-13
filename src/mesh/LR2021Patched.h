#pragma once
#include <RadioLib.h>

#if RADIOLIB_EXCLUDE_LR2021 != 1

/**
 * LR2021 that loads Semtech's firmware patch (PRAM, DS 22.3) right after the chip reset in begin(), Semtech's order:
 * begin() resets the chip and calls standby() before the TCXO setup and calibration, so an armed load runs there.
 */
class LR2021Patched : public LR2021
{
  public:
    explicit LR2021Patched(Module *mod) : LR2021(mod) {}

    using LR2021::standby;
    int16_t standby() override;

    /// Load the patch at the next standby(), which in begin() is the first command after the reset
    void armPatchLoad() { patchPending = true; }

    /// True if an armed load never ran
    bool patchLoadPending() const { return patchPending; }

    /// Write, activate and check the patch now; logs the outcome. The chip still works without it.
    int16_t loadPatch();

  private:
    bool patchPending = false;

    /// Read the PRAM magic word and the type/version word after it
    int16_t readPatchState(uint32_t &magic, uint32_t &typeVersion);
};

#endif
