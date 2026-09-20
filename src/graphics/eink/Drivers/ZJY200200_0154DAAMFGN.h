/*

E-Ink display driver
    - ZJY200200-0154DAAMFGN
    - Manufacturer: Zhongjingyuan
    - Size: 1.54 inch
    - Resolution: 200px x 200px
    - Flex connector marking: FPC-B001

    Note: as of Feb. 2025, these panels are used for "WeActStudio 1.54in B&W" display modules

    This *is* a distinct panel, however the driver is currently identical to GDEY0154D67
    We recognize it as separate now, to avoid breaking any custom builds if the drivers do need to diverge in future.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./GDEY0154D67.h"

namespace NicheGraphics::Drivers
{

typedef GDEY0154D67 ZJY200200_0154DAAMFGN;

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS