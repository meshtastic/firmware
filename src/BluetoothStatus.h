#pragma once
#include "Status.h"
#include "assert.h"
#include "configuration.h"
#include "meshUtils.h"
#include <Arduino.h>
#include <cstring>

namespace meshtastic
{

// Describes the state of the Bluetooth connection
// Allows display to handle pairing events without each UI needing to explicitly hook the Bluefruit / NimBLE code
class BluetoothStatus : public Status
{
  public:
    enum class ConnectionState {
        DISCONNECTED,
        PAIRING,
        CONNECTED,
    };

  private:
    CallbackObserver<BluetoothStatus, const BluetoothStatus *> statusObserver =
        CallbackObserver<BluetoothStatus, const BluetoothStatus *>(this, &BluetoothStatus::updateStatus);

    ConnectionState state = ConnectionState::DISCONNECTED;
    // Fixed storage avoids heap churn during pairing updates and preserves
    // leading zeroes in six-digit BLE passkeys.
    char passkey[7] = {};

  public:
    BluetoothStatus() { statusType = STATUS_TYPE_BLUETOOTH; }

    // New BluetoothStatus: connected or disconnected
    explicit BluetoothStatus(ConnectionState state)
    {
        assert(state != ConnectionState::PAIRING); // If pairing, use constructor which specifies passkey
        statusType = STATUS_TYPE_BLUETOOTH;
        this->state = state;
    }

    // New BluetoothStatus: pairing, with passkey
    explicit BluetoothStatus(const char *passkeyText) : Status()
    {
        statusType = STATUS_TYPE_BLUETOOTH;
        this->state = ConnectionState::PAIRING;
        setPasskey(passkeyText);
    }

    ConnectionState getConnectionState() const { return this->state; }

    const char *getPasskey() const
    {
        assert(state == ConnectionState::PAIRING);
        return this->passkey;
    }

    void observe(Observable<const BluetoothStatus *> *source) { statusObserver.observe(source); }

    bool matches(const BluetoothStatus *newStatus) const
    {
        if (this->state == newStatus->getConnectionState()) {
            // Same state: CONNECTED / DISCONNECTED
            if (this->state != ConnectionState::PAIRING)
                return true;
            // Same state: PAIRING, and passkey matches
            else if (strncmp(this->getPasskey(), newStatus->getPasskey(), sizeof(passkey)) == 0)
                return true;
        }

        return false;
    }

    int updateStatus(const BluetoothStatus *newStatus)
    {
        // Has the status changed?
        if (!matches(newStatus)) {
            // Copy the members
            state = newStatus->getConnectionState();
            if (state == ConnectionState::PAIRING)
                setPasskey(newStatus->getPasskey());

            // Tell anyone interested that we have an update
            onNewStatus.notifyObservers(this);

            // Debug only:
            switch (state) {
            case ConnectionState::PAIRING:
                LOG_DEBUG("BluetoothStatus PAIRING, key=%s", passkey);
                break;
            case ConnectionState::CONNECTED:
                LOG_DEBUG("BluetoothStatus CONNECTED");
#ifdef BLE_LED
                digitalWrite(BLE_LED, LED_STATE_ON);
#endif
                break;

            case ConnectionState::DISCONNECTED:
                LOG_DEBUG("BluetoothStatus DISCONNECTED");
#ifdef BLE_LED
                digitalWrite(BLE_LED, LED_STATE_OFF);
#endif
                break;
            }
        }

        return 0;
    }
  private:
    void setPasskey(const char *passkeyText)
    {
        if (!passkeyText) {
            passkey[0] = '\0';
            return;
        }

        strncpy(passkey, passkeyText, sizeof(passkey) - 1);
        passkey[sizeof(passkey) - 1] = '\0';
    }
};

} // namespace meshtastic

extern meshtastic::BluetoothStatus *bluetoothStatus;
