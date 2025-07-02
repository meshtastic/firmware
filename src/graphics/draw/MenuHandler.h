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
#if !MESHTASTIC_EXCLUDE_GPS
        gps_toggle_menu,
#endif
        compass_point_north_menu,
        reset_node_db_menu
    };
    static screenMenus menuQueue;

    static void LoraRegionPicker(uint32_t duration = 30000);
    static void handleMenuSwitch();
    static void clockMenu();
    static void TZPicker();
    static void TwelveHourPicker();
    static void ClockFacePicker();
    static void messageResponseMenu();
    static void homeBaseMenu();
    static void favoriteBaseMenu();
    static void positionBaseMenu();
    static void compassNorthMenu();
    static void GPSToggleMenu();
    static void BuzzerModeMenu();
    static void switchToMUIMenu();
    static void nodeListMenu();
    static void resetNodeDBMenu();
};

} // namespace graphics