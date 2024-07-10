/**
 * @brief This class enables on the fly software and hardware setup.
 *        It will contain all command messages to change internal settings.
 */

enum class Cmd {
    INVALID,
    SET_ON,
    SET_OFF,
    ON_PRESS,
    START_ALERT_FRAME,
    STOP_ALERT_FRAME,
    START_FIRMWARE_UPDATE_SCREEN,
    STOP_BOOT_SCREEN,
    PRINT,
    SHOW_PREV_FRAME,
    SHOW_NEXT_FRAME
};