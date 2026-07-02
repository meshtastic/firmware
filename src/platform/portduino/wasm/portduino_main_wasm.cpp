// WASM replacement for framework-portduino's cores/portduino/main.cpp.
// The original uses argp (CLI parse), ftw/nftw (FS walk) and an internal
// while(1){ loop(); usleep(loopDelay); } pump - none of which fit the browser.
//
// Here: run the firmware setup() once from main(), then RETURN. JavaScript
// drives the cooperative scheduler by calling wasm_loop_once() on a timer,
// using the delay (ms-until-next-task) the firmware itself computes. This keeps
// the Asyncify stack shallow (we suspend only inside WebUSB transfers, not
// across idle time) and lets JS pump WebUSB IRQ polling between ticks.
//
// firmware loop() already does: service->loop(); mainController.runOrDelay();
// RadioLibInterface::instance->pollMissedIrqs();  (src/main.cpp:1223+)
// so we just invoke it once per tick.

#include <cstdio>
#include <emscripten.h>

// Firmware entry points (Arduino-style), defined in src/main.cpp with C linkage.
extern "C" void setup();
extern "C" void loop();

// portduinoSetup() is the firmware's portduino init (PortduinoGlue.cpp). Under
// ARCH_PORTDUINO_WASM it applies the wasm config (wasm_config_apply) and creates
// the WebUSB-backed Ch341Hal, then returns (no YAML/filesystem). Must run before setup().
extern void portduinoSetup();
extern void wasm_fs_mount(); // points portduinoVFS at /meshdata (portduino_glue_wasm.cpp)

// Re-entrancy guard (defined in portduino_glue_wasm.cpp). True while the firmware
// is executing setup()/loop() - including while it is Asyncify-suspended inside a
// WebUSB transfer - so the wasm_* API/region entry points reject a mid-tick
// re-entry from JS instead of corrupting state or aborting Asyncify. The host is
// expected to call them only between ticks; this is the safety net.
extern "C" volatile bool g_wasm_in_firmware;

// Boot the node. Call from JS AFTER Module.ch341 (the WebUSB bridge) is wired up.
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_setup()
{
    g_wasm_in_firmware = true;
    wasm_fs_mount();
    portduinoSetup();
    setup();
    g_wasm_in_firmware = false;
}

// Called repeatedly by JS on a timer. Returns the firmware's requested delay in
// ms until it next wants to run (mainController.runOrDelay()'s value is consumed
// inside loop(); we return a conservative pump interval the JS side can cap).
//
// If you want the exact next-delay, expose it: have a small in-tree change make
// loop() stash mainController.runOrDelay() in a global, or just poll fast (e.g.
// JS setTimeout(_, 5)) - RX latency is bounded by pollMissedIrqs() each tick.
extern "C" EMSCRIPTEN_KEEPALIVE int wasm_loop_once()
{
    g_wasm_in_firmware = true;
    loop();
    g_wasm_in_firmware = false;
    return 5; // ms; JS caps the scheduling cadence
}

// INVOKE_RUN=0, so main is only run if JS calls callMain(). We keep it for
// parity with the harness: boot on main() when present.
int main()
{
    // INVOKE_RUN=0: the page calls wasm_setup() after wiring up Module.ch341.
    printf("[meshnode] wasm loaded - set Module.ch341, then call wasm_setup()\n");
    return 0;
}
