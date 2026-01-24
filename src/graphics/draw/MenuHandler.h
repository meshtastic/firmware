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
        lora_Menu,
        lora_picker,
        device_role_picker,
        radio_preset_picker,
        frequency_slot,
        no_timeout_lora_picker,
        TZ_picker,
        twelve_hour_picker,
        clock_face_picker,
        clock_menu,
        position_base_menu,
        node_base_menu,
        gps_toggle_menu,
        gps_format_menu,
        gps_smart_position_menu,
        gps_update_interval_menu,
        gps_position_broadcast_menu,
        compass_point_north_menu,
        reset_node_db_menu,
        buzzermodemenupicker,
        mui_picker,
        tftcolormenupicker,
        brightness_picker,
        reboot_menu,
        shutdown_menu,
        NodePicker_menu,
        Manage_Node_menu,
        remove_favorite,
        test_menu,
        number_test,
        wifi_toggle_menu,
        bluetooth_toggle_menu,
        screen_options_menu,
        power_menu,
        battery_calibration_menu,
        battery_calibration_confirm_menu,
        system_base_menu,
        key_verification_init,
        key_verification_final_prompt,
        trace_route_menu,
        throttle_message,
        message_response_menu,
        message_viewmode_menu,
        reply_menu,
        delete_messages_menu,
        node_name_length_menu,
        FrameToggles,
        DisplayUnits
    };
    static screenMenus menuQueue;
    static uint32_t pickedNodeNum; // node selected by NodePicker for ManageNodeMenu

    static void OnboardMessage();
    static void LoraRegionPicker(uint32_t duration = 30000);
    static void loraMenu();
    static void DeviceRolePicker();
    static void RadioPresetPicker();
    static void FrequencySlotPicker();
    static void handleMenuSwitch(OLEDDisplay *display);
    static void showConfirmationBanner(const char *message, std::function<void()> onConfirm);
    static void clockMenu();
    static void TZPicker();
    static void TwelveHourPicker();
    static void ClockFacePicker();
    static void messageResponseMenu();
    static void messageViewModeMenu();
    static void replyMenu();
    static void deleteMessagesMenu();
    static void homeBaseMenu();
    static void textMessageBaseMenu();
    static void systemBaseMenu();
    static void favoriteBaseMenu();
    static void positionBaseMenu();
    static void compassNorthMenu();
    static void GPSToggleMenu();
    static void GPSFormatMenu();
    static void GPSSmartPositionMenu();
    static void GPSUpdateIntervalMenu();
    static void GPSPositionBroadcastMenu();
    static void BuzzerModeMenu();
    static void switchToMUIMenu();
    static void TFTColorPickerMenu(OLEDDisplay *display);
    static void nodeListMenu();
    static void resetNodeDBMenu();
    static void BrightnessPickerMenu();
    static void rebootMenu();
    static void shutdownMenu();
    static void NodePicker();
    static void ManageNodeMenu();
    static void addFavoriteMenu();
    static void removeFavoriteMenu();
    static void traceRouteMenu();
    static void testMenu();
    static void numberTest();
    static void wifiBaseMenu();
    static void wifiToggleMenu();
    static void screenOptionsMenu();
    static void powerMenu();
    static void batteryCalibrationMenu();
    static void batteryCalibrationConfirmMenu();
    static void nodeNameLengthMenu();
    static void FrameToggles_menu();
    static void DisplayUnits_menu();
    static void textMessageMenu();

  private:
    static void saveUIConfig();
    static void keyVerificationInitMenu();
    static void keyVerificationFinalPrompt();
    static void BluetoothToggleMenu();
};

/* Generic Menu Options designations  */
enum class OptionsAction { Back, Select };

template <typename T> struct MenuOption {
    const char *label;
    OptionsAction action;
    bool hasValue;
    T value;

    MenuOption(const char *labelIn, OptionsAction actionIn, T valueIn)
        : label(labelIn), action(actionIn), hasValue(true), value(valueIn)
    {
    }

    MenuOption(const char *labelIn, OptionsAction actionIn) : label(labelIn), action(actionIn), hasValue(false), value() {}
};

struct ScreenColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    bool useVariant;

    ScreenColor(uint8_t rIn = 0, uint8_t gIn = 0, uint8_t bIn = 0, bool variantIn = false)
        : r(rIn), g(gIn), b(bIn), useVariant(variantIn)
    {
    }
};

using RadioPresetOption = MenuOption<meshtastic_Config_LoRaConfig_ModemPreset>;
using LoraRegionOption = MenuOption<meshtastic_Config_LoRaConfig_RegionCode>;
using TimezoneOption = MenuOption<const char *>;
using CompassOption = MenuOption<meshtastic_CompassMode>;
using ScreenColorOption = MenuOption<ScreenColor>;
using GPSToggleOption = MenuOption<meshtastic_Config_PositionConfig_GpsMode>;
using GPSFormatOption = MenuOption<meshtastic_DeviceUIConfig_GpsCoordinateFormat>;
using NodeNameOption = MenuOption<bool>;
using PositionMenuOption = MenuOption<int>;
using ManageNodeOption = MenuOption<int>;
using ClockFaceOption = MenuOption<bool>;

} // namespace graphics
#endif
