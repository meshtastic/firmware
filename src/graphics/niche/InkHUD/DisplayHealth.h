#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Maintains display health by balancing fast updates with panel-specific cleanup or full refreshes.

- tracks maintenance debt from fast updates
- chooses fast refresh or maintenance when no update type is specified
- periodically requests update unprovoked, if required for display health

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"

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

    uint8_t fastPerFull = 5;      // Ideal number of fast refreshes between maintenance refreshes
    float stressMultiplier = 2.0; // How bad for the display are extra fast refreshes beyond fastPerFull?
    Drivers::EInk::UpdateTypes maintenanceUpdateType = Drivers::EInk::UpdateTypes::FULL;

  private:
    int32_t runOnce() override;
    void beginMaintenance();  // Excessive debt: begin unprovoked refreshing of display, for health
    int32_t endMaintenance(); // End unprovoked refreshing: debt paid

    Drivers::EInk::UpdateTypes
    prioritize(Drivers::EInk::UpdateTypes type1,
               Drivers::EInk::UpdateTypes type2); // Determine which of two update types is more important to honor

    bool forced = false;
    Drivers::EInk::UpdateTypes workingDecision = Drivers::EInk::UpdateTypes::UNSPECIFIED;

    float debt = 0.0; // How many maintenance refreshes are due
};

} // namespace NicheGraphics::InkHUD

#endif
