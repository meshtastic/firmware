#pragma once
#if HAS_SCREEN
#include "configuration.h"
namespace graphics
{

class menuHandler
{
  public:
    enum screenMenus {
        menu_none,
        lora_picker,
        TZ_picker,
        twelve_hour_picker,
        clock_face_picker,
        clock_menu,
        position_base_menu,
        gps_toggle_menu,
        compass_point_north_menu,
        reset_node_db_menu,
        buzzermodemenupicker,
        mui_picker,
        tftcolormenupicker,
        brightness_picker,
        reboot_menu,
        shutdown_menu,
        add_favorite,
        remove_favorite,
        test_menu,
        number_test,
        wifi_toggle_menu,
        bluetooth_toggle_menu,
        notifications_menu,
        screen_options_menu,
        power_menu,
        system_base_menu,
        key_verification_init,
        key_verification_final_prompt,
        trace_route_menu,
        throttle_message,
    };
    static screenMenus menuQueue;

    static void LoraRegionPicker(uint32_t duration = 30000);
    static void handleMenuSwitch(OLEDDisplay *display);
    static void clockMenu();
    static void TZPicker();
    static void TwelveHourPicker();
    static void ClockFacePicker();
    static void messageResponseMenu();
    static void homeBaseMenu();
    static void textMessageBaseMenu();
    static void systemBaseMenu();
    static void favoriteBaseMenu();
    static void positionBaseMenu();
    static void compassNorthMenu();
    static void GPSToggleMenu();
    static void BuzzerModeMenu();
    static void switchToMUIMenu();
    static void TFTColorPickerMenu(OLEDDisplay *display);
    static void nodeListMenu();
    static void resetNodeDBMenu();
    static void BrightnessPickerMenu();
    static void rebootMenu();
    static void shutdownMenu();
    static void addFavoriteMenu();
    static void removeFavoriteMenu();
    static void traceRouteMenu();
    static void testMenu();
    static void numberTest();
    static void wifiBaseMenu();
    static void wifiToggleMenu();
    static void notificationsMenu();
    static void screenOptionsMenu();
    static void powerMenu();

  private:
    static void saveUIConfig();
    static void keyVerificationInitMenu();
    static void keyVerificationFinalPrompt();
    static void BluetoothToggleMenu();
};

} // namespace graphics
#endif