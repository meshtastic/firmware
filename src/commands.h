/**
 * @brief This class enables on the fly software and hardware setup.
 *        It will contain all command messages to change internal settings.
 */

enum class Cmd {
    INVALID,
    SET_ON,
    SET_OFF,
    ON_PRESS,
    START_BLUETOOTH_PIN_SCREEN,
    START_FIRMWARE_UPDATE_SCREEN,
    STOP_BLUETOOTH_PIN_SCREEN,
    STOP_BOOT_SCREEN,
    PRINT,
    START_SHUTDOWN_SCREEN,
    START_REBOOT_SCREEN,
    SHOW_PREV_FRAME,
    SHOW_NEXT_FRAME
};