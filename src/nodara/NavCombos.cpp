#include "NavCombos.h"

#if defined(Nodara)

#include "configuration.h"
#include "input/InputBroker.h"
#include "SixKeyNavInput.h"

// ---------------------------------------------------------------------------
// Fn + Up → toggle Bluetooth (persistent, triggers reboot)
// ---------------------------------------------------------------------------
void navComboUp()
{
    LOG_INFO("NavCombo: BLE toggle -> saveToDisk + reboot scheduled");
    if (sixKeyNavInput) {
        sixKeyNavInput->fireKbEvent(INPUT_BROKER_MSG_BLUETOOTH_TOGGLE);
    }
}

// ---------------------------------------------------------------------------
// Fn + Down → deep sleep (INPUT_BROKER_SHUTDOWN)
// ---------------------------------------------------------------------------
void navComboDown()
{
    LOG_INFO("NavCombo: shutdown");
    if (sixKeyNavInput)
        sixKeyNavInput->fireEvent(INPUT_BROKER_SHUTDOWN);
}

// ---------------------------------------------------------------------------
// Fn + Left → unassigned
// ---------------------------------------------------------------------------
void navComboLeft()
{
}

// ---------------------------------------------------------------------------
// Fn + Right → toggle GPS power (persistent)
// ---------------------------------------------------------------------------
void navComboRight()
{
    LOG_INFO("NavCombo: GPS toggle");
    if (sixKeyNavInput) {
        sixKeyNavInput->fireEvent(INPUT_BROKER_GPS_TOGGLE);
    }
}

// ---------------------------------------------------------------------------
// Fn + Enter → unassigned
// ---------------------------------------------------------------------------
void navComboEnter()
{
}

#endif // Nodara
