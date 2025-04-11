#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Responsible for maintaining display health, by optimizing the ratio of FAST vs FULL refreshes

- counts number of FULL vs FAST refresh
- suggests whether to use FAST or FULL, when not explicitly specified
- periodically requests update unprovoked, if required for display health

*/

#pragma once

#include "configuration.h"

#include "InkHUD.h"

#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

class DisplayHealth : protected concurrency::OSThread
{
  public:
    DisplayHealth();

    void requestUpdateType(Drivers::EInk::UpdateTypes type);
    void forceUpdateType(Drivers::EInk::UpdateTypes type);
    Drivers::EInk::UpdateTypes decideUpdateType();

    uint8_t fastPerFull = 5;      // Ideal number of fast refreshes between full refreshes
    float stressMultiplier = 2.0; // How bad for the display are extra fast refreshes beyond fastPerFull?

  private:
    int32_t runOnce() override;
    void beginMaintenance();  // Excessive debt: begin unprovoked refreshing of display, for health
    int32_t endMaintenance(); // End unprovoked refreshing: debt paid

    Drivers::EInk::UpdateTypes
    prioritize(Drivers::EInk::UpdateTypes type1,
               Drivers::EInk::UpdateTypes type2); // Determine which of two update types is more important to honor

    bool forced = false;
    Drivers::EInk::UpdateTypes workingDecision = Drivers::EInk::UpdateTypes::UNSPECIFIED;

    float debt = 0.0; // How many full refreshes are due
};

} // namespace NicheGraphics::InkHUD

#endif