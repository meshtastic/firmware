#include "./EInk.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Separate from EInk::begin method, as derived class constructors can probably supply these parameters as constants
EInk::EInk(uint16_t width, uint16_t height, UpdateTypes supported)
    : concurrency::OSThread("E-Ink Driver"), width(width), height(height), supportedUpdateTypes(supported)
{
    OSThread::disable();
}

// Used by NicheGraphics implementations to check if a display supports a specific refresh operation.
// Whether or not the update type is supported is specified in the constructor
bool EInk::supports(UpdateTypes type)
{
    // The EInkUpdateTypes enum assigns each type a unique bit. We are checking if that bit is set.
    if (supportedUpdateTypes & type)
        return true;
    else
        return false;
}

// Begins using the OSThread to detect when a display update is complete
// This allows the refresh operation to run "asynchronously".
// Rather than blocking execution waiting for the update to complete, we are periodically checking the hardware's BUSY pin
// The expectedDuration argument allows us to delay the start of this checking, if we know "roughly" how long an update takes.
// Potentially, a display without hardware BUSY could rely entirely on "expectedDuration",
// provided its isUpdateDone() override always returns true.
void EInk::beginPolling(uint32_t interval, uint32_t expectedDuration)
{
    updateRunning = true;
    updateBegunAt = millis();
    pollingInterval = interval;

    // To minimize load, we can choose to delay polling for a few seconds, if we know roughly how long the update will take
    // By default, expectedDuration is 0, and we'll start polling immediately
    OSThread::setIntervalFromNow(expectedDuration);
    OSThread::enabled = true;
}

// Meshtastic's pseudo-threading layer
// We're using this as a timer, to periodically check if an update is complete
// This is what allows us to update the display asynchronously
int32_t EInk::runOnce()
{
    if (!isUpdateDone())
        return pollingInterval; // Poll again in a few ms

    // If update done:
    finalizeUpdate();      // Any post-update code: power down panel hardware, hibernate, etc
    updateRunning = false; // Change what we report via EInk::busy()
    return disable();      // Stop polling
}

// Wait for an in progress update to complete before continuing
// Run a normal (async) update first, *then* call await
void EInk::await()
{
    // Stop our concurrency thread
    OSThread::disable();

    // Sit and block until the update is complete
    while (updateRunning) {
        runOnce();
        yield();
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS