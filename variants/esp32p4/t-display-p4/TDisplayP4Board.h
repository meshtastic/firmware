#pragma once

// Board helpers for the display classes, defined in variant.cpp.
// No includes, no firmware types: this header is parsed inside device-ui's DisplayDriverFactory.cpp.
#ifdef __cplusplus
void tDisplayP4PanelReset();    // EXIO_DISP_RST: LOW, delay(10), HIGH, delay(120)
void tDisplayP4TouchReset();    // EXIO_TOUCH_RST: LOW, delay(30), HIGH, delay(100)
bool tDisplayP4ExpanderReady(); // true when the XL9535 answered at boot
bool tDisplayP4I2cUsable();     // false once the deep-sleep observer has run: no touch I2C after that
#endif
