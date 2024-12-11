#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Singleton class, which manages the broadest InkHUD behaviors

    Tasks include:
    - containing instances of Tiles and Applets
    - co-ordinating refreshes
    - interacting with other NicheGraphics componets, such as the driver, and input sources
    - handling system-wide events (e.g. shutdown)

*/

#pragma once

#include "configuration.h"

#include <vector>

#include "main.h"
#include "modules/TextMessageModule.h"
#include "power.h"
#include "sleep.h"

#include "./Applet.h"
#include "./Applets/System/Notification/Notification.h"
#include "./Persistence.h"
#include "./Tile.h"
#include "./Types.h"
#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

class Applet;
class Tile;

class BootScreenApplet;
class NotificationApplet;

class WindowManager : protected concurrency::OSThread
{
  public:
    static WindowManager *getInstance(); // Get or create singleton instance

    void setDriver(NicheGraphics::Drivers::EInk *driver); // Assign a driver class
    void addApplet(const char *name, Applet *a, bool defaultActive = false, bool defaultAutoshow = false); // Select feature-set
    void begin(); // Start running the window manager (provisioning done)

    void createSystemApplets(); // Instantiate and activate system applets
    void createSystemTiles();   // Instantiate tiles which host system applets
    void assignSystemAppletsToTiles();
    void placeSystemTiles(); // Set position and size

    void createUserApplets(); // Activate user's selected applets
    void createUserTiles();   // Instantiate enough tiles for user's selected layout
    void assignUserAppletsToTiles();
    void placeUserTiles(); // Automatically place tiles, according to user's layout
    void refocusTile();    // Ensure focused tile has a valid applet

    int beforeDeepSleep(void *unused);                             // Prepare for shutdown
    int onReceiveTextMessage(const meshtastic_MeshPacket *packet); // Store most recent text message

    void handleButtonShort();    // User button: short press
    void handleButtonLong();     // User button: longp press
    void handleAuxButtonDown();  // Secondary button: press/hold starts
    void handleAuxButtonUp();    // Secondary button: press/hold ends
    void handleAuxButtonShort(); // Secondary button: short press
    void handleAuxButtonLong();  // Secondary button: long press

    void nextApplet(); // Cycle through user applets
    void nextTile();   // Focus the next tile (when showing multiple applets at once)

    void changeLayout();                       // Change tile layout or count
    void changeActivatedApplets();             // Change which applets are activated
    void toggleBatteryIcon();                  // Change whether the battery icon is shown
    bool approveNotification(Notification &n); // Ask applets if a notification is worth showing

    void requestUpdate(Drivers::EInk::UpdateTypes type, bool async, bool allTiles); // Update the display image
    void handleTilePixel(int16_t x, int16_t y, Color c);                            // Apply rotation, then store the pixel

    uint16_t getWidth();                      // Display width, relative to rotation
    uint16_t getHeight();                     // Display height, relative to rotation
    uint8_t getAppletCount();                 // How many user applets are available, including inactivated
    const char *getAppletName(uint8_t index); // By order in userApplets

  protected:
    WindowManager(); // Private constructor for singleton

    int32_t runOnce() override;

    void render(bool force = false);                    // Attempt to update the display
    void setBufferPixel(int16_t x, int16_t y, Color c); // Place pixels into the frame buffer. All translation / rotation done.
    void rotatePixelCoords(int16_t *x, int16_t *y);     // Apply the display rotation
    void clearBuffer();                                 // Empty the framebuffer

    void findOrphanApplets(); // Find any applets left-behind when layout changes

    // Get notified when the system is shutting down
    CallbackObserver<WindowManager, void *> deepSleepObserver =
        CallbackObserver<WindowManager, void *>(this, &WindowManager::beforeDeepSleep);

    // Cache *incoming* text messages, for use by applets
    CallbackObserver<WindowManager, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<WindowManager, const meshtastic_MeshPacket *>(this, &WindowManager::onReceiveTextMessage);

    NicheGraphics::Drivers::EInk *driver = nullptr;
    uint8_t *imageBuffer; // Fed into driver
    uint16_t imageBufferHeight;
    uint16_t imageBufferWidth;
    uint32_t imageBufferSize; // Bytes

    // User Applets
    std::vector<Applet *> userApplets;
    std::vector<Tile *> userTiles;

    // System Applets
    std::vector<Applet *> systemApplets;
    Tile *fullscreenTile = nullptr;
    Tile *notificationTile = nullptr;
    Tile *batteryIconTile = nullptr;
    BootScreenApplet *bootscreenApplet;
    NotificationApplet *notificationApplet;
    Applet *batteryIconApplet;
    Applet *menuApplet;
    Applet *placeholderApplet;

    // Set by requestUpdate
    bool updateRequested = false;
    Drivers::EInk::UpdateTypes requestedUpdateType = Drivers::EInk::UpdateTypes::UNSPECIFIED;
    bool requestedAsync = true;
    bool requestedRenderAll = false;
};

}; // namespace NicheGraphics::InkHUD

#endif