#pragma once

#include "configuration.h"

#if defined(GAT562)

namespace graphics
{

// BLE advertising identity is hardware-derived and intentionally independent
// from the user-editable Meshtastic owner name.
const char *getGAT562BleName();

} // namespace graphics

#endif
