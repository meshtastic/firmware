#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Responsible for display health
- counts number of FULL vs FAST refresh
- suggests whether to use FAST or FULL, when not explicitly specified
- periodically requests update unprovoked, if required for display health

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

class UpdateMediator : protected concurrency::OSThread
{
  public:
    UpdateMediator();

    // Tell the mediator what we want, get told what we can have
    Drivers::EInk::UpdateTypes evaluate(Drivers::EInk::UpdateTypes requested);

    // Determine which of two update types is more important to honor
    Drivers::EInk::UpdateTypes prioritize(Drivers::EInk::UpdateTypes type1, Drivers::EInk::UpdateTypes type2);

    uint8_t fastPerFull = 5;      // Ideal number of fast refreshes between full refreshes
    float stressMultiplier = 2.0; // How bad for the display are extra fast refreshes beyond fastPerFull?

  private:
    int32_t runOnce() override;
    void beginMaintenance();  // Excessive debt: begin unprovoked refreshing of display, for health
    int32_t endMaintenance(); // End unprovoked refreshing: debt paid

    float debt = 0.0; // How many full refreshes are due
};

} // namespace NicheGraphics::InkHUD

#endif