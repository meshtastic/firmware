#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Shows the Bluetooth passkey during pairing

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class PairingApplet : public Applet
{
  public:
    void onRender() override;
    void onActivate() override;
    void onDeactivate() override;
    void onForeground() override;
    void onBackground() override;

    int onBluetoothStatusUpdate(const meshtastic::Status *status);

  protected:
    // Get notified when status of the Bluetooth connection changes
    CallbackObserver<PairingApplet, const meshtastic::Status *> bluetoothStatusObserver =
        CallbackObserver<PairingApplet, const meshtastic::Status *>(this, &PairingApplet::onBluetoothStatusUpdate);

    std::string passkey = ""; // Passkey. Six digits, possibly with leading zeros
};

} // namespace NicheGraphics::InkHUD

#endif