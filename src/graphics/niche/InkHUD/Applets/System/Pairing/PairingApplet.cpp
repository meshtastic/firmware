#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./PairingApplet.h"

using namespace NicheGraphics;

void InkHUD::PairingApplet::onRender()
{
    // Header
    setFont(fontLarge);
    printAt(X(0.5), Y(0.25), "Bluetooth", CENTER, BOTTOM);
    setFont(fontSmall);
    printAt(X(0.5), Y(0.25), "Enter this code", CENTER, TOP);

    // Passkey
    setFont(fontLarge);
    printThick(X(0.5), Y(0.5), passkey.substr(0, 3) + " " + passkey.substr(3), 3, 2);

    // Device's bluetooth name, if it will fit
    setFont(fontSmall);
    std::string name = "Name: " + std::string(getDeviceName());
    if (getTextWidth(name) > width()) // Too wide, try without the leading "Name: "
        name = std::string(getDeviceName());
    if (getTextWidth(name) < width()) // Does it fit?
        printAt(X(0.5), Y(0.75), name, CENTER, MIDDLE);
}

void InkHUD::PairingApplet::onActivate()
{
    bluetoothStatusObserver.observe(&bluetoothStatus->onNewStatus);
}

void InkHUD::PairingApplet::onDeactivate()
{
    bluetoothStatusObserver.unobserve(&bluetoothStatus->onNewStatus);
}

void InkHUD::PairingApplet::onForeground()
{
    lockRendering();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // FAST refresh guaranteed, instead of usual UNSPECIFIED
}
void InkHUD::PairingApplet::onBackground()
{
    unlockRendering();
    requestUpdate(Drivers::EInk::UpdateTypes::FULL, true); // FULL refresh, and re-render all applets we were previously covering
}

int InkHUD::PairingApplet::onBluetoothStatusUpdate(const meshtastic::Status *status)
{
    // The standard Meshtastic convention is to pass these "generic" Status objects,
    // check their type, and then cast them.
    // We'll mimic that behavior, just to keep in line with the other Statuses,
    // even though I'm not sure what the original reason for jumping through these extra hoops was.
    assert(status->getStatusType() == STATUS_TYPE_BLUETOOTH);
    meshtastic::BluetoothStatus *bluetoothStatus = (meshtastic::BluetoothStatus *)status;

    // When pairing begins
    if (bluetoothStatus->getConnectionState() == meshtastic::BluetoothStatus::ConnectionState::PAIRING) {
        // Store the passkey for rendering
        passkey = bluetoothStatus->getPasskey();

        // Make sure no other system applets are locking the display
        // Boot screen, menu, etc
        Applet *lockOwner = WindowManager::getInstance()->whoLockedRendering();
        if (lockOwner)
            lockOwner->sendToBackground();

        // Show pairing screen
        bringToForeground();
    }

    // When pairing ends
    // or rather, when something changes, and we shouldn't be showing the pairing screen
    else if (isForeground())
        sendToBackground();

    return 0; // No special result to report back to Observable
}

#endif