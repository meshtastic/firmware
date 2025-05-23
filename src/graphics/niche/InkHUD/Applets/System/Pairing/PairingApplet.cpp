#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./PairingApplet.h"

using namespace NicheGraphics;

InkHUD::PairingApplet::PairingApplet()
{
    bluetoothStatusObserver.observe(&bluetoothStatus->onNewStatus);
}

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
    std::string name = "Name: " + parse(getDeviceName());
    if (getTextWidth(name) > width()) // Too wide, try without the leading "Name: "
        name = parse(getDeviceName());
    if (getTextWidth(name) < width()) // Does it fit?
        printAt(X(0.5), Y(0.75), name, CENTER, MIDDLE);
}

void InkHUD::PairingApplet::onForeground()
{
    // Prevent most other applets from requesting update, and skip their rendering entirely
    // Another system applet with a higher precedence can potentially ignore this
    SystemApplet::lockRendering = true;
    SystemApplet::lockRequests = true;
}
void InkHUD::PairingApplet::onBackground()
{
    // Allow normal update behavior to resume
    SystemApplet::lockRendering = false;
    SystemApplet::lockRequests = false;

    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // Usually, onBackground is followed by another applet's onForeground (which requests update), but not in this case
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
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