#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/WindowManager.h"

#include "./MenuItem.h"
#include "./MenuPage.h"

#include "concurrency/OSThread.h"

namespace NicheGraphics::InkHUD
{

class MenuApplet : public Applet, public concurrency::OSThread
{
  public:
    MenuApplet();
    void onActivate() override;
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void render() override;

  protected:
    int32_t runOnce() override;

    void execute(MenuItem item);  // Perform the MenuAction associated with a MenuItem, if any
    void showPage(MenuPage page); // Load and display a MenuPage
    void populateAppletPage();    // Dynamically create MenuItems for toggling loaded applets
    void populateAutoshowPage();  // Dynamically create MenuItems for selecting which applets can autoshow
    void populateRecentsPage();   // Create menu items: a choice of values for settings.recentlyActiveSeconds
    uint16_t getSystemInfoPanelHeight();
    void drawSystemInfoPanel(int16_t left, int16_t top, uint16_t width,
                             uint16_t *height = nullptr); // Info panel at top of root menu

    MenuPage currentPage;
    uint8_t cursor = 0;       // Which menu item is currently highlighted
    bool cursorShown = false; // Is *any* item highlighted? (Root menu: no initial selection)

    uint16_t systemInfoPanelHeight = 0; // Need to know before we render

    std::vector<MenuItem> items; // MenuItems for the current page. Filled by ShowPage

    WindowManager *windowManager;                    // Convenient access to the InkHUD::WindowManager singleton
    Drivers::LatchingBacklight *backlight = nullptr; // Convenient access to the backlight singleton
};

} // namespace NicheGraphics::InkHUD

#endif