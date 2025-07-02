#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/Utils/CannedMessageStore.h"

#include "./MenuItem.h"
#include "./MenuPage.h"

#include "Channels.h"
#include "concurrency/OSThread.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class MenuApplet : public SystemApplet, public concurrency::OSThread
{
  public:
    MenuApplet();
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onRender() override;

    void show(Tile *t); // Open the menu, onto a user tile

  protected:
    Drivers::LatchingBacklight *backlight = nullptr; // Convenient access to the backlight singleton

    int32_t runOnce() override;

    void execute(MenuItem item);  // Perform the MenuAction associated with a MenuItem, if any
    void showPage(MenuPage page); // Load and display a MenuPage

    void populateSendPage();      // Dynamically create MenuItems including canned messages
    void populateRecipientPage(); // Dynamically create a page of possible destinations for a canned message
    void populateAppletPage();    // Dynamically create MenuItems for toggling loaded applets
    void populateAutoshowPage();  // Dynamically create MenuItems for selecting which applets can autoshow
    void populateRecentsPage();   // Create menu items: a choice of values for settings.recentlyActiveSeconds

    uint16_t getSystemInfoPanelHeight();
    void drawSystemInfoPanel(int16_t left, int16_t top, uint16_t width,
                             uint16_t *height = nullptr);                   // Info panel at top of root menu
    void sendText(NodeNum dest, ChannelIndex channel, const char *message); // Send a text message to mesh
    void freeCannedMessageResources();                                      // Clear MenuApplet's canned message processing data

    MenuPage currentPage = MenuPage::ROOT;
    uint8_t cursor = 0;       // Which menu item is currently highlighted
    bool cursorShown = false; // Is *any* item highlighted? (Root menu: no initial selection)

    uint16_t systemInfoPanelHeight = 0; // Need to know before we render

    std::vector<MenuItem> items; // MenuItems for the current page. Filled by ShowPage

    // Data for selecting and sending canned messages via the menu
    // Placed into a sub-class for organization only
    class CannedMessages
    {
      public:
        // Share NicheGraphics component
        // Handles loading, getting, setting
        CannedMessageStore *store;

        // One canned message
        // Links the menu item to the true message text
        struct MessageItem {
            std::string label;   // Shown in menu. Prefixed, and UTF-8 chars parsed
            std::string rawText; // The message which will be sent, if this item is selected
        } *selectedMessageItem;

        // One possible destination for a canned message
        // Links the menu item to the intended recipient
        // May represent either broadcast or DM
        struct RecipientItem {
            std::string label; // Shown in menu
            NodeNum dest = NODENUM_BROADCAST;
            uint8_t channelIndex = 0;
        } *selectedRecipientItem;

        // These lists are generated when the menu page is populated
        // Cleared onBackground (when MenuApplet closes)
        std::vector<MessageItem> messageItems;
        std::vector<RecipientItem> recipientItems;
    } cm;

    Applet *borrowedTileOwner = nullptr; // Which applet we have temporarily replaced while displaying menu

    bool invertedColors = false; // Helper to display current state of config.display.displaymode in InkHUD options
};

} // namespace NicheGraphics::InkHUD

#endif