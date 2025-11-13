#include "StatusLEDModule.h"
#include "MeshService.h"
#include "configuration.h"
#include <Arduino.h>

/*
Generic Thread Module allows for the execution of custom code at a set interval.
*/
StatusLEDModule *statusLEDModule;

StatusLEDModule::StatusLEDModule() : concurrency::OSThread("StatusLEDdModule")
{
    bluetoothStatusObserver.observe(&bluetoothStatus->onNewStatus);
    powerStatusObserver.observe(&powerStatus->onNewStatus);
}

int StatusLEDModule::handleStatusUpdate(const meshtastic::Status *arg)
{
    switch (arg->getStatusType()) {
    case STATUS_TYPE_POWER: {
        meshtastic::PowerStatus *powerStatus = (meshtastic::PowerStatus *)arg;
        if (powerStatus->getHasUSB()) {
            power_state = charging;
            if (powerStatus->getBatteryChargePercent() >= 100) {
                power_state = charged;
            }
        } else {
            power_state = discharging;
        }
        break;
    }
    case STATUS_TYPE_BLUETOOTH: {
        meshtastic::BluetoothStatus *bluetoothStatus = (meshtastic::BluetoothStatus *)arg;
        switch (bluetoothStatus->getConnectionState()) {
        case meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED: {
            ble_state = unpaired;
            break;
        }
        case meshtastic::BluetoothStatus::ConnectionState::PAIRING: {
            ble_state = pairing;
            break;
        }
        case meshtastic::BluetoothStatus::ConnectionState::CONNECTED: {
            ble_state = connected;
            break;
        }
        }

        break;
    }
    }
    return 0;
};

int32_t StatusLEDModule::runOnce()
{

    if (power_state == charging) {
        CHARGE_LED_state = !CHARGE_LED_state;
    } else if (power_state == charged) {
        CHARGE_LED_state = LED_STATE_ON;
    } else {
        CHARGE_LED_state = LED_STATE_OFF;
    }

    if (!config.bluetooth.enabled) {
        PAIRING_LED_state = LED_STATE_OFF;
    } else if (ble_state == unpaired) {
        if (slowTrack) {
            PAIRING_LED_state = !PAIRING_LED_state;
            slowTrack = false;
        } else {
            slowTrack = true;
        }
    } else if (ble_state == pairing) {
        PAIRING_LED_state = !PAIRING_LED_state;
    } else {
        PAIRING_LED_state = LED_STATE_ON;
    }

#ifdef LED_CHARGE
    digitalWrite(LED_CHARGE, CHARGE_LED_state);
#endif
    // digitalWrite(green_LED_PIN, LED_STATE_OFF);
#ifdef LED_PAIRING
    digitalWrite(LED_PAIRING, PAIRING_LED_state);
#endif

    return (my_interval);
}
