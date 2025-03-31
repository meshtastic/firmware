#pragma once
#include "Status.h"
#include "assert.h"
#include "configuration.h"
#include "meshUtils.h"
#include <Arduino.h>

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
    std::string passkey; // Stored as string, because Bluefruit allows passkeys with a leading zero

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
    explicit BluetoothStatus(const std::string &passkey) : Status()
    {
        statusType = STATUS_TYPE_BLUETOOTH;
        this->state = ConnectionState::PAIRING;
        this->passkey = passkey;
    }

    ConnectionState getConnectionState() const { return this->state; }

    std::string getPasskey() const
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
            else if (this->getPasskey() == newStatus->getPasskey())
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
                passkey = newStatus->getPasskey();

            // Tell anyone interested that we have an update
            onNewStatus.notifyObservers(this);

            // Debug only:
            switch (state) {
            case ConnectionState::PAIRING:
                LOG_DEBUG("BluetoothStatus PAIRING, key=%s", passkey.c_str());
                break;
            case ConnectionState::CONNECTED:
                LOG_DEBUG("BluetoothStatus CONNECTED");
                break;

            case ConnectionState::DISCONNECTED:
                LOG_DEBUG("BluetoothStatus DISCONNECTED");
                break;
            }
        }

        return 0;
    }
};

} // namespace meshtastic

extern meshtastic::BluetoothStatus *bluetoothStatus;