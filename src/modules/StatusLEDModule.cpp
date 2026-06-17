#include "StatusLEDModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "mesh/RadioInterface.h"
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
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    if (inputBroker)
        inputObserver.observe(inputBroker);
#endif
#ifdef LED_LORA
    loraRxObserver.observe(&RadioInterface::loraRxPacketObservable);
#endif
#ifdef NEOPIXEL_STATUS_POWER_PIN
    powerPixel.begin();
    powerPixel.clear();
    powerPixel.show();
#endif
#ifdef NEOPIXEL_STATUS_PAIRING_PIN
    pairingPixel.begin();
    pairingPixel.clear();
    pairingPixel.show();
#endif
}

// Helper: write a 1-pixel NeoPixel strand to `color` when stateOn, else clear.
// Kept as a static inline here (rather than a member) so it compiles out
// completely when no NeoPixel status pins are defined.
#if defined(NEOPIXEL_STATUS_POWER_PIN) || defined(NEOPIXEL_STATUS_PAIRING_PIN)
static inline void writeStatusPixel(Adafruit_NeoPixel &pixel, uint32_t color, bool stateOn)
{
    pixel.setPixelColor(0, stateOn ? color : 0);
    pixel.show();
}
#endif

int StatusLEDModule::handleStatusUpdate(const meshtastic::Status *arg)
{
    switch (arg->getStatusType()) {
    case STATUS_TYPE_POWER: {
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
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
int StatusLEDModule::handleInputEvent(const InputEvent *event)
{
    lastUserbuttonTime = millis();
    return 0;
}
#endif
#ifdef LED_LORA
int StatusLEDModule::handleLoRaRx(uint32_t)
{
    // Briefly flash LED_LORA on each received packet. Turn it on now (we share the main thread with
    // the radio's receive handler, so this is safe) and wake runOnce() at flash end to turn it off.
    digitalWrite(LED_LORA, LED_STATE_ON);
    LORA_LED_state = LED_STATE_ON;
    LORA_LED_starttime = millis();
    setIntervalFromNow(LORA_RX_LED_FLASH_MS);
    return 0;
}
#endif

int32_t StatusLEDModule::runOnce()
{
    my_interval = 1000;

    if (power_state == charging) {
#ifndef POWER_LED_HARDWARE_BLINKS_WHILE_CHARGING
        CHARGE_LED_state = !CHARGE_LED_state;
#endif
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
                CHARGE_LED_state = LED_STATE_OFF;
            }
        }
    }
// If we want a LED to be dedicated to the simple hearbeat, we can use that instead of the charge LED
#if defined(LED_HEARTBEAT)
    if (power_state != charging && power_state != charged && !doing_fast_blink && !config.device.led_heartbeat_disabled) {
        if (HEARTBEAT_LED_state == LED_STATE_ON) {
            HEARTBEAT_LED_state = LED_STATE_OFF;
            my_interval = 999;
        } else {
            HEARTBEAT_LED_state = LED_STATE_ON;
            my_interval = 1;
        }
        digitalWrite(LED_HEARTBEAT, HEARTBEAT_LED_state);
    } else {
        HEARTBEAT_LED_state = LED_STATE_OFF;
        digitalWrite(LED_HEARTBEAT, HEARTBEAT_LED_state);
    }
#else
    if (power_state != charging && power_state != charged && !doing_fast_blink && !config.device.led_heartbeat_disabled) {
        if (CHARGE_LED_state == LED_STATE_ON) {
            CHARGE_LED_state = LED_STATE_OFF;
            my_interval = 999;
        } else {
            CHARGE_LED_state = LED_STATE_ON;
            my_interval = 1;
        }
    }
#endif
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

    // Override if disabled in config
    if (config.device.led_heartbeat_disabled) {
        CHARGE_LED_state = LED_STATE_OFF;
    }
#ifdef Battery_LED_1
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
#endif

#if defined(HAS_PMU)
    if (pmu_found && PMU) {
        // blink the axp led
        PMU->setChargingLedMode(CHARGE_LED_state ? XPOWERS_CHG_LED_ON : XPOWERS_CHG_LED_OFF);
    }
#endif

#ifdef PCA_LED_POWER
    io.digitalWrite(PCA_LED_POWER, CHARGE_LED_state);
#endif
#ifdef PCA_LED_ENABLE
    io.digitalWrite(PCA_LED_ENABLE, CHARGE_LED_state);
#endif
#ifdef LED_POWER
    digitalWrite(LED_POWER, CHARGE_LED_state);
#endif
#ifdef LED_PAIRING
    digitalWrite(LED_PAIRING, PAIRING_LED_state);
#endif
#ifdef NEOPIXEL_STATUS_POWER_PIN
    writeStatusPixel(powerPixel, NEOPIXEL_STATUS_POWER_COLOR, CHARGE_LED_state == LED_STATE_ON);
#endif
#ifdef NEOPIXEL_STATUS_PAIRING_PIN
    writeStatusPixel(pairingPixel, NEOPIXEL_STATUS_PAIRING_COLOR, PAIRING_LED_state == LED_STATE_ON);
#endif

#ifdef RGB_LED_POWER
    if (!config.device.led_heartbeat_disabled) {
        if (CHARGE_LED_state == LED_STATE_ON) {
            ambientLightingThread->setLighting(10, 255, 0, 0);
        } else {
            ambientLightingThread->setLighting(0, 0, 0, 0);
        }
    }
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

#ifdef LED_LORA
    // End the LoRa-RX flash once its duration has elapsed; otherwise make sure we come back
    // exactly at flash end (only ever clamp my_interval down, so other LED timing is preserved).
    if (LORA_LED_state == LED_STATE_ON) {
        uint32_t elapsed = millis() - LORA_LED_starttime;
        if (elapsed >= LORA_RX_LED_FLASH_MS) {
            digitalWrite(LED_LORA, LED_STATE_OFF);
            LORA_LED_state = LED_STATE_OFF;
        } else if ((uint32_t)my_interval > LORA_RX_LED_FLASH_MS - elapsed) {
            my_interval = LORA_RX_LED_FLASH_MS - elapsed;
        }
    }
#endif

    return (my_interval);
}

void StatusLEDModule::setPowerLED(bool LEDon)
{

#if defined(HAS_PMU)
    if (pmu_found && PMU) {
        // blink the axp led
        PMU->setChargingLedMode(LEDon ? XPOWERS_CHG_LED_ON : XPOWERS_CHG_LED_OFF);
    }
#endif
    uint8_t ledState = LEDon ? LED_STATE_ON : LED_STATE_OFF;
#ifdef PCA_LED_POWER
    io.digitalWrite(PCA_LED_POWER, ledState);
#endif
#ifdef PCA_LED_ENABLE
    io.digitalWrite(PCA_LED_ENABLE, ledState);
#endif
#ifdef LED_POWER
    digitalWrite(LED_POWER, ledState);
#endif
#ifdef LED_PAIRING
    digitalWrite(LED_PAIRING, ledState);
#endif
#ifdef NEOPIXEL_STATUS_POWER_PIN
    writeStatusPixel(powerPixel, NEOPIXEL_STATUS_POWER_COLOR, LEDon);
#endif
#ifdef NEOPIXEL_STATUS_PAIRING_PIN
    writeStatusPixel(pairingPixel, NEOPIXEL_STATUS_PAIRING_COLOR, LEDon);
#endif

#ifdef Battery_LED_1
    digitalWrite(Battery_LED_1, ledState);
#endif
#ifdef Battery_LED_2
    digitalWrite(Battery_LED_2, ledState);
#endif
#ifdef Battery_LED_3
    digitalWrite(Battery_LED_3, ledState);
#endif
#ifdef Battery_LED_4
    digitalWrite(Battery_LED_4, ledState);
#endif
}
