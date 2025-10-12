#include "BuzzerFeedbackThread.h"
#include "NodeDB.h"
#include "buzz.h"
#include "configuration.h"

BuzzerFeedbackThread *buzzerFeedbackThread;

BuzzerFeedbackThread::BuzzerFeedbackThread()
{
    if (inputBroker)
        inputObserver.observe(inputBroker);
}

int BuzzerFeedbackThread::handleInputEvent(const InputEvent *event)
{
    // Only provide feedback if buzzer is enabled for notifications
    if (config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED ||
        config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_NOTIFICATIONS_ONLY ||
        config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_DIRECT_MSG_ONLY) {
        return 0; // Let other handlers process the event
    }

    // Handle different input events with appropriate buzzer feedback
    switch (event->inputEvent) {
    case INPUT_BROKER_USER_PRESS:
    case INPUT_BROKER_ALT_PRESS:
    case INPUT_BROKER_SELECT:
    case INPUT_BROKER_SELECT_LONG:
        playBeep(); // Confirmation feedback
        break;

    case INPUT_BROKER_UP:
    case INPUT_BROKER_UP_LONG:
    case INPUT_BROKER_DOWN:
    case INPUT_BROKER_DOWN_LONG:
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_RIGHT:
        playChirp(); // Navigation feedback
        break;

    case INPUT_BROKER_CANCEL:
    case INPUT_BROKER_BACK:
        playBoop(); // Cancel/back feedback
        break;

    case INPUT_BROKER_SEND_PING:
        playComboTune(); // Ping sent feedback
        break;

    default:
        // For other events, check if it's a printable character
        if (event->kbchar >= 32 && event->kbchar <= 126) {
            // Typing feedback - very short boop
            // Removing this for now, too chatty
            // playChirp();
        }
        break;
    }

    return 0; // Allow other handlers to process the event
}