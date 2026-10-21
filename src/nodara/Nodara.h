#pragma once
#if defined(Nodara)

namespace nodara
{
void setup();
void loop();

/** True while handling external MCU power-cut (0x0005), until System OFF. */
bool isExternalPowerOff();
void setExternalPowerOff(bool on);
} // namespace nodara
#endif
