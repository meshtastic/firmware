/*

    Base class for E-Ink display drivers

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#include "configuration.h"

#include "concurrency/OSThread.h"
#include <SPI.h>

namespace NicheGraphics::Drivers
{

class EInk : private concurrency::OSThread
{
  public:
    // Different possible operations used to update an E-Ink display
    // Some displays will not support all operations
    // Each value needs a unique bit. In some cases, we might set more than one bit (e.g. EInk::supportedUpdateType)
    enum UpdateTypes : uint8_t {
        UNSPECIFIED = 0,
        FULL = 1 << 0,
        FAST = 1 << 1, // "Partial Refresh"
    };

    EInk(uint16_t width, uint16_t height, UpdateTypes supported);
    virtual void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) = 0;
    virtual void update(uint8_t *imageData, UpdateTypes type) = 0; // Change the display image
    void await();                                                  // Wait for an in-progress update to complete before proceeding
    bool supports(UpdateTypes type);                               // Can display perform a certain update type
    bool busy() { return updateRunning; }                          // Display able to update right now?

    const uint16_t width; // Public so that NicheGraphics implementations can access. Safe because const.
    const uint16_t height;

  protected:
    void beginPolling(uint32_t interval, uint32_t expectedDuration); // Begin checking repeatedly if update finished
    virtual bool isUpdateDone() = 0;                                 // Check once if update finished
    virtual void finalizeUpdate() {}                                 // Run any post-update code
    bool failed = false;                                             // If an error occurred during update

  private:
    int32_t runOnce() override; // Repeated checking if update finished

    const UpdateTypes supportedUpdateTypes; // Capabilities of a derived display class
    bool updateRunning = false;             // see EInk::busy()
    uint32_t pollingInterval = 0;           // How often to check if update complete (ms)
    uint32_t pollingBegunAt = 0;            // To timeout during polling
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS