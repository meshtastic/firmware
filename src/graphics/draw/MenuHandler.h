#pragma once
#if HAS_SCREEN
#include "configuration.h"
namespace graphics
{

class menuHandler
{
  public:
    enum screenMenus {
        MenuNone,
        LoraMenu,
        LoraPicker,
        DeviceRolePicker,
        RadioPresetPicker,
        TXEnabledMenu,
        FrequencySlot,
        NoTimeoutLoraPicker,
        TzPicker,
        TwelveHourPicker,
        ClockFacePicker,
        ClockMenu,
        PositionBaseMenu,
        NodeBaseMenu,
        GpsToggleMenu,
        GpsFormatMenu,
        GpsSmartPositionMenu,
        GpsUpdateIntervalMenu,
        GpsPositionBroadcastMenu,
        CompassPointNorthMenu,
        ResetNodeDbMenu,
        BuzzerModeMenuPicker,
        MuiPicker,
        BrightnessPicker,
        RebootMenu,
        ShutdownMenu,
        NodePickerMenu,
        ManageNodeMenu,
        RemoveFavorite,
        WaypointBaseMenu,
        GeofenceWaypointMenu,
        GeofenceOptionsMenu,
        RemoveWaypointMenu,
        TestMenu,
        NumberTest,
        EnvironmentTelemetryMenu,
        EnvironmentTelemetrySourceMenu,
        WifiToggleMenu,
        BluetoothToggleMenu,
        ScreenOptionsMenu,
        PowerMenu,
        SystemBaseMenu,
        KeyVerificationInit,
        KeyVerificationFinalPrompt,
        TraceRouteMenu,
        ThrottleMessage,
        MessageResponseMenu,
        MessageViewModeMenu,
        ReplyMenu,
        DeleteMessagesMenu,
        NodeNameLengthMenu,
        FrameToggles,
        DisplayUnits,
        MessageBubblesMenu,
        ThemeMenu,
        HamModeConfirm,
        LicensedToNormalConfirm,
#if HAS_LORA_FEM
        LoraFemLnaToggleMenu
#endif
    };
    static screenMenus menuQueue;
    static uint32_t pickedNodeNum; // node selected by NodePicker for ManageNodeMenu
    static meshtastic_Config_LoRaConfig_RegionCode pendingRegion;

    static void OnboardMessage();
    static void LoraRegionPicker(uint32_t duration = 30000);
    static void loraMenu();
    static void deviceRolePicker();
    static void radioPresetPicker();
    static void txEnabledMenu();
    static void FrequencySlotPicker();
    static void handleMenuSwitch(OLEDDisplay *display);
    static void showConfirmationBanner(const char *message, std::function<void()> onConfirm);
    static void clockMenu();
    static void TZPicker();
    static void twelveHourPicker();
    static void clockFacePicker();
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
    static void nodeListMenu();
    static void resetNodeDBMenu();
    static void BrightnessPickerMenu();
    static void rebootMenu();
    static void shutdownMenu();
    static void NodePicker();
    static void manageNodeMenu();
    static void addFavoriteMenu();
    static void removeFavoriteMenu();
    static void waypointBaseMenu();
    static void geofenceWaypointMenu();
    static void geofenceOptionsMenu();
    static void removeWaypointMenu();
    static void traceRouteMenu();
    static void testMenu();
    static void numberTest();
    static void environmentTelemetryMenu();
    static void environmentTelemetrySourceMenu();
    static void wifiBaseMenu();
    static void wifiToggleMenu();
    static void screenOptionsMenu();
    static void powerMenu();
    static void nodeNameLengthMenu();
    static void frameTogglesMenu();
    static void displayUnitsMenu();
    static void messageBubblesMenu();
    static void themeMenu();
    static void textMessageMenu();
    static void hamModeConfirmMenu();
    static void licensedToNormalConfirmMenu();
#if HAS_LORA_FEM
    static void LoRaFEMLNAToggleMenu();
#endif

    // Lifted out of its banner-callback lambda so it is reachable without a Screen. The lambda only
    // ever runs via screen->showOverlayBanner(), which is why nothing here was unit-testable.
    static void toggleNodeMuted(uint32_t nodeNum); // uint32_t, matching pickedNodeNum above

    // Preset a region selection should leave installed. `lora` is the config as it stands *before*
    // the selection is written.
    static meshtastic_Config_LoRaConfig_ModemPreset presetForRegionSelection(const meshtastic_Config_LoRaConfig &lora,
                                                                             meshtastic_Config_LoRaConfig_RegionCode selected);

  private:
    static void saveUIConfig();
    static void keyVerificationInitMenu();
    static void keyVerificationFinalPrompt();
    static void bluetoothToggleMenu();
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

using RadioPresetOption = MenuOption<meshtastic_Config_LoRaConfig_ModemPreset>;
using LoraRegionOption = MenuOption<meshtastic_Config_LoRaConfig_RegionCode>;
using TimezoneOption = MenuOption<const char *>;
using CompassOption = MenuOption<meshtastic_CompassMode>;
using GPSToggleOption = MenuOption<meshtastic_Config_PositionConfig_GpsMode>;
using GPSFormatOption = MenuOption<meshtastic_DeviceUIConfig_GpsCoordinateFormat>;
using NodeNameOption = MenuOption<bool>;
using PositionMenuOption = MenuOption<int>;
using ManageNodeOption = MenuOption<int>;
using ClockFaceOption = MenuOption<bool>;
#if HAS_LORA_FEM
using LoRaFEMLNAToggleOption = MenuOption<meshtastic_Config_LoRaConfig_FEM_LNA_Mode>;
#endif

} // namespace graphics
#endif
