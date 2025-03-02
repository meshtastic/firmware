#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Singleton class, which manages the broadest InkHUD behaviors

    Tasks include:
    - containing instances of Tiles and Applets
    - co-ordinating display updates
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
#include "./UpdateMediator.h"
#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

class Applet;
class Tile;

class LogoApplet;
class MenuApplet;
class NotificationApplet;

class WindowManager : protected concurrency::OSThread
{
  public:
    static WindowManager *getInstance(); // Get or create singleton instance

    void setDriver(NicheGraphics::Drivers::EInk *driver);                   // Assign a driver class
    void setDisplayResilience(uint8_t fastPerFull, float stressMultiplier); // How many FAST updates before FULL
    void addApplet(const char *name, Applet *a, bool defaultActive = false, bool defaultAutoshow = false,
                   uint8_t onTile = -1); // Select which applets are used with InkHUD
    void begin();                        // Start running the window manager (provisioning done)

    void createSystemApplets(); // Instantiate and activate system applets
    void createSystemTiles();   // Instantiate tiles which host system applets
    void assignSystemAppletsToTiles();
    void placeSystemTiles();          // Set position and size
    void claimFullscreen(Applet *sa); // Assign a system applet to the fullscreen tile
    void releaseFullscreen();         // Remove any system applet from the fullscreen tile

    void createUserApplets(); // Activate user's selected applets
    void createUserTiles();   // Instantiate enough tiles for user's selected layout
    void assignUserAppletsToTiles();
    void placeUserTiles(); // Automatically place tiles, according to user's layout
    void refocusTile();    // Ensure focused tile has a valid applet

    int beforeDeepSleep(void *unused);                             // Prepare for shutdown
    int beforeReboot(void *unused);                                // Prepare for reboot
    int onReceiveTextMessage(const meshtastic_MeshPacket *packet); // Store most recent text message
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused); // Prepare for light sleep
#endif

    void handleButtonShort(); // User button: short press
    void handleButtonLong();  // User button: long press

    void nextApplet(); // Cycle through user applets
    void nextTile();   // Focus the next tile (when showing multiple applets at once)

    void changeLayout();                       // Change tile layout or count
    void changeActivatedApplets();             // Change which applets are activated
    void toggleBatteryIcon();                  // Change whether the battery icon is shown
    bool approveNotification(Notification &n); // Ask applets if a notification is worth showing

    void handleTilePixel(int16_t x, int16_t y, Color c); // Apply rotation, then store the pixel in framebuffer
    void requestUpdate();                                // Update display, if a foreground applet has info it wants to show
    void forceUpdate(Drivers::EInk::UpdateTypes type = Drivers::EInk::UpdateTypes::UNSPECIFIED,
                     bool async = true); // Update display, regardless of whether any applets requested this

    uint16_t getWidth();                      // Display width, relative to rotation
    uint16_t getHeight();                     // Display height, relative to rotation
    uint8_t getAppletCount();                 // How many user applets are available, including inactivated
    const char *getAppletName(uint8_t index); // By order in userApplets

    void lock(Applet *owner);                   // Allows system applets to prevent other applets triggering a refresh
    void unlock(Applet *owner);                 // Allows normal updating of user applets to continue
    bool canRequestUpdate(Applet *a = nullptr); // Checks if allowed to request an update (not locked by other applet)
    Applet *whoLocked();                        // Find which applet is blocking update requests, if any

  protected:
    WindowManager(); // Private constructor for singleton

    int32_t runOnce() override;

    void clearBuffer();                            // Empty the framebuffer
    void autoshow();                               // Show a different applet, to display new info
    bool shouldUpdate();                           // Check if reason to change display image
    Drivers::EInk::UpdateTypes selectUpdateType(); // Determine how the display hardware will perform the image update
    void renderUserApplets();                      // Draw all currently displayed user applets to the frame buffer
    void renderSystemApplets();                    // Draw all currently displayed system applets to the frame buffer
    void renderPlaceholders();                     // Draw diagonal lines on user tiles which have no assigned applet
    void render(bool async = true);                // Attempt to update the display

    void setBufferPixel(int16_t x, int16_t y, Color c); // Place pixels into the frame buffer. All translation / rotation done.
    void rotatePixelCoords(int16_t *x, int16_t *y);     // Apply the display rotation

    void findOrphanApplets(); // Find any applets left-behind when layout changes

    // Get notified when the system is shutting down
    CallbackObserver<WindowManager, void *> deepSleepObserver =
        CallbackObserver<WindowManager, void *>(this, &WindowManager::beforeDeepSleep);

    // Get notified when the system is rebooting
    CallbackObserver<WindowManager, void *> rebootObserver =
        CallbackObserver<WindowManager, void *>(this, &WindowManager::beforeReboot);

    // Cache *incoming* text messages, for use by applets
    CallbackObserver<WindowManager, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<WindowManager, const meshtastic_MeshPacket *>(this, &WindowManager::onReceiveTextMessage);

#ifdef ARCH_ESP32
    // Get notified when the system is entering light sleep
    CallbackObserver<WindowManager, void *> lightSleepObserver =
        CallbackObserver<WindowManager, void *>(this, &WindowManager::beforeLightSleep);
#endif

    NicheGraphics::Drivers::EInk *driver = nullptr;
    uint8_t *imageBuffer; // Fed into driver
    uint16_t imageBufferHeight;
    uint16_t imageBufferWidth;
    uint32_t imageBufferSize; // Bytes

    // Encapsulates decision making about E-Ink update types
    // Responsible for display health
    UpdateMediator mediator;

    // User Applets
    std::vector<Applet *> userApplets;
    std::vector<Tile *> userTiles;

    // System Applets
    std::vector<Applet *> systemApplets;
    Tile *fullscreenTile = nullptr;
    Tile *notificationTile = nullptr;
    Tile *batteryIconTile = nullptr;
    LogoApplet *logoApplet;
    Applet *pairingApplet;
    Applet *tipsApplet;
    NotificationApplet *notificationApplet;
    Applet *batteryIconApplet;
    MenuApplet *menuApplet;
    Applet *placeholderApplet;

    // requestUpdate
    bool requestingUpdate = false; // WindowManager::render run pending

    // forceUpdate
    bool forcingUpdate = false; // WindowManager::render run pending, guaranteed no skip of update
    Drivers::EInk::UpdateTypes forcedUpdateType = Drivers::EInk::UpdateTypes::UNSPECIFIED; // guaranteed update using this type

    Applet *lockOwner = nullptr; // Which system applet (if any) is preventing other applets from requesting update
};

}; // namespace NicheGraphics::InkHUD

#endif