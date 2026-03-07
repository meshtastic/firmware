
/*

Power Hardware Abstraction Layer. Set of API calls to offload power management, measurements, reboots, etc
to the platform and variant code to avoid #ifdef spaghetti hell and limitless device-based edge cases
in the main firmware code

Functions declared here (with exception of powerHAL_init) should be defined in platform specific codebase.
Default function body does usually nothing.

*/

// Initialize HAL layer. Call it as early as possible during device boot
// do not overwrite it as it's not declared with "weak" attribute.
void powerHAL_init();

// platform specific init code if needed to be run early on boot
void powerHAL_platformInit();

// Return true if current battery level is safe for device operation (for example flash writes).
// This should be reported by power failure comparator (NRF52) or similar circuits on other platforms.
// Do not use battery ADC as improper ADC configuration may prevent device from booting.
bool powerHAL_isPowerLevelSafe();

// return if USB voltage is connected
bool powerHAL_isVBUSConnected();
