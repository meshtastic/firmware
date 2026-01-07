#pragma once

#include "BluetoothStatus.h"
#include "MeshModule.h"
#include "PowerStatus.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class StatusLEDModule : private concurrency::OSThread
{
    bool slowTrack = false;

  public:
    StatusLEDModule();

    int handleStatusUpdate(const meshtastic::Status *);

  protected:
    unsigned int my_interval = 1000; // interval in millisconds
    virtual int32_t runOnce() override;

    CallbackObserver<StatusLEDModule, const meshtastic::Status *> bluetoothStatusObserver =
        CallbackObserver<StatusLEDModule, const meshtastic::Status *>(this, &StatusLEDModule::handleStatusUpdate);
    CallbackObserver<StatusLEDModule, const meshtastic::Status *> powerStatusObserver =
        CallbackObserver<StatusLEDModule, const meshtastic::Status *>(this, &StatusLEDModule::handleStatusUpdate);

  private:
    bool CHARGE_LED_state = LED_STATE_OFF;
    bool PAIRING_LED_state = LED_STATE_OFF;

    uint32_t PAIRING_LED_starttime = 0;
    uint32_t POWER_LED_starttime = 0;
    bool doing_fast_blink = false;

    enum PowerState { discharging, charging, charged, critical };

    PowerState power_state = discharging;

    enum BLEState { unpaired, pairing, connected };

    BLEState ble_state = unpaired;
};

extern StatusLEDModule *statusLEDModule;
