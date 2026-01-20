#include "StatusLEDModule.h"
#include "MeshService.h"
#include "configuration.h"
#include <Arduino.h>

/*
StatusLEDModule manages the device's status LEDs, updating their states based on power and Bluetooth status.
It reflects charging, charged, discharging, and Bluetooth connection states using the appropriate LEDs.
*/
StatusLEDModule *statusLEDModule;

StatusLEDModule::StatusLEDModule() : concurrency::OSThread("StatusLEDModule")
{
    bluetoothStatusObserver.observe(&bluetoothStatus->onNewStatus);
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    if (inputBroker)
        inputObserver.observe(inputBroker);
}

int StatusLEDModule::handleStatusUpdate(const meshtastic::Status *arg)
{
    switch (arg->getStatusType()) {
    case STATUS_TYPE_POWER: {
        meshtastic::PowerStatus *powerStatus = (meshtastic::PowerStatus *)arg;
        if (powerStatus->getHasUSB() || powerStatus->getIsCharging()) {
            power_state = charging;
            if (powerStatus->getBatteryChargePercent() >= 100) {
                power_state = charged;
            }
        } else {
            if (powerStatus->getBatteryChargePercent() > 5) {
                power_state = discharging;
            } else {
                power_state = critical;
            }
        }
        break;
    }
    case STATUS_TYPE_BLUETOOTH: {
        meshtastic::BluetoothStatus *bluetoothStatus = (meshtastic::BluetoothStatus *)arg;
        switch (bluetoothStatus->getConnectionState()) {
        case meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED: {
            ble_state = unpaired;
            PAIRING_LED_starttime = millis();
            break;
        }
        case meshtastic::BluetoothStatus::ConnectionState::PAIRING: {
            ble_state = pairing;
            PAIRING_LED_starttime = millis();
            break;
        }
        case meshtastic::BluetoothStatus::ConnectionState::CONNECTED: {
            if (ble_state != connected) {
                ble_state = connected;
                PAIRING_LED_starttime = millis();
            }
        }
        }

        break;
    }
    }
    return 0;
};

int StatusLEDModule::handleInputEvent(const InputEvent *event)
{
    lastUserbuttonTime = millis();
    return 0;
}

int32_t StatusLEDModule::runOnce()
{
    my_interval = 1000;

    if (power_state == charging) {
        CHARGE_LED_state = !CHARGE_LED_state;
    } else if (power_state == charged) {
        CHARGE_LED_state = LED_STATE_ON;
    } else if (power_state == critical) {
        if (POWER_LED_starttime + 30000 < millis() && !doing_fast_blink) {
            doing_fast_blink = true;
            POWER_LED_starttime = millis();
        }
        if (doing_fast_blink) {
            PAIRING_LED_state = LED_STATE_OFF;
            CHARGE_LED_state = !CHARGE_LED_state;
            my_interval = 250;
            if (POWER_LED_starttime + 2000 < millis()) {
                doing_fast_blink = false;
            }
        } else {
            CHARGE_LED_state = LED_STATE_OFF;
        }

    } else {
        CHARGE_LED_state = LED_STATE_OFF;
    }

    if (!config.bluetooth.enabled || PAIRING_LED_starttime + 30 * 1000 < millis() || doing_fast_blink) {
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

    bool chargeIndicatorLED1 = LED_STATE_OFF;
    bool chargeIndicatorLED2 = LED_STATE_OFF;
    bool chargeIndicatorLED3 = LED_STATE_OFF;
    bool chargeIndicatorLED4 = LED_STATE_OFF;
    if (lastUserbuttonTime + 10 * 1000 > millis() || CHARGE_LED_state == LED_STATE_ON) {
        // should this be off at very low percentages?
        chargeIndicatorLED1 = LED_STATE_ON;
        if (powerStatus && powerStatus->getBatteryChargePercent() >= 25)
            chargeIndicatorLED2 = LED_STATE_ON;
        if (powerStatus && powerStatus->getBatteryChargePercent() >= 50)
            chargeIndicatorLED3 = LED_STATE_ON;
        if (powerStatus && powerStatus->getBatteryChargePercent() >= 75)
            chargeIndicatorLED4 = LED_STATE_ON;
    }

#ifdef LED_CHARGE
    digitalWrite(LED_CHARGE, CHARGE_LED_state);
#endif
    // digitalWrite(green_LED_PIN, LED_STATE_OFF);
#ifdef LED_PAIRING
    digitalWrite(LED_PAIRING, PAIRING_LED_state);
#endif

#ifdef Battery_LED_1
    digitalWrite(Battery_LED_1, chargeIndicatorLED1);
#endif
#ifdef Battery_LED_2
    digitalWrite(Battery_LED_2, chargeIndicatorLED2);
#endif
#ifdef Battery_LED_3
    digitalWrite(Battery_LED_3, chargeIndicatorLED3);
#endif
#ifdef Battery_LED_4
    digitalWrite(Battery_LED_4, chargeIndicatorLED4);
#endif

    return (my_interval);
}
