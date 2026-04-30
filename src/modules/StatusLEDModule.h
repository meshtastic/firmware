#pragma once

#include "BluetoothStatus.h"
#include "MeshModule.h"
#include "PowerStatus.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <functional>

#if !MESHTASTIC_EXCLUDE_INPUTBROKER
#include "input/InputBroker.h"
#endif

// WS2812/NeoPixel status-LED support. A variant may define
//   NEOPIXEL_STATUS_POWER_PIN   (required to enable the power/charge pixel)
//   NEOPIXEL_STATUS_POWER_COLOR (optional, default red 0xFF0000)
//   NEOPIXEL_STATUS_PAIRING_PIN / _COLOR  (default blue 0x0000FF)
// Each pixel is a standalone 1-LED strand on its own GPIO — this mirrors how
// boards like the LilyGo T-Echo-Card expose three independent WS2812s.
#if defined(NEOPIXEL_STATUS_POWER_PIN) || defined(NEOPIXEL_STATUS_PAIRING_PIN)
#include <Adafruit_NeoPixel.h>
#ifndef NEOPIXEL_STATUS_TYPE
#define NEOPIXEL_STATUS_TYPE (NEO_GRB + NEO_KHZ800)
#endif
#ifndef NEOPIXEL_STATUS_POWER_COLOR
#define NEOPIXEL_STATUS_POWER_COLOR 0xFF0000 // red
#endif
#ifndef NEOPIXEL_STATUS_PAIRING_COLOR
#define NEOPIXEL_STATUS_PAIRING_COLOR 0x0000FF // blue
#endif
#endif

class StatusLEDModule : private concurrency::OSThread
{
    bool slowTrack = false;

  public:
    StatusLEDModule();

    int handleStatusUpdate(const meshtastic::Status *);
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    int handleInputEvent(const InputEvent *arg);
#endif

    void setPowerLED(bool);

#ifdef NEOPIXEL_STATUS_POWER_PIN
    Adafruit_NeoPixel powerPixel = Adafruit_NeoPixel(1, NEOPIXEL_STATUS_POWER_PIN, NEOPIXEL_STATUS_TYPE);
#endif
#ifdef NEOPIXEL_STATUS_PAIRING_PIN
    Adafruit_NeoPixel pairingPixel = Adafruit_NeoPixel(1, NEOPIXEL_STATUS_PAIRING_PIN, NEOPIXEL_STATUS_TYPE);
#endif

  protected:
    unsigned int my_interval = 1000; // interval in millisconds
    virtual int32_t runOnce() override;

    CallbackObserver<StatusLEDModule, const meshtastic::Status *> bluetoothStatusObserver =
        CallbackObserver<StatusLEDModule, const meshtastic::Status *>(this, &StatusLEDModule::handleStatusUpdate);
    CallbackObserver<StatusLEDModule, const meshtastic::Status *> powerStatusObserver =
        CallbackObserver<StatusLEDModule, const meshtastic::Status *>(this, &StatusLEDModule::handleStatusUpdate);
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    CallbackObserver<StatusLEDModule, const InputEvent *> inputObserver =
        CallbackObserver<StatusLEDModule, const InputEvent *>(this, &StatusLEDModule::handleInputEvent);
#endif

  private:
    bool CHARGE_LED_state = LED_STATE_OFF;
    bool PAIRING_LED_state = LED_STATE_OFF;

    uint32_t PAIRING_LED_starttime = 0;
    uint32_t lastUserbuttonTime = 0;
    uint32_t POWER_LED_starttime = 0;
    bool doing_fast_blink = false;

    enum PowerState { discharging, charging, charged, critical };

    PowerState power_state = discharging;

    enum BLEState { unpaired, pairing, connected };

    BLEState ble_state = unpaired;
};

extern StatusLEDModule *statusLEDModule;
#ifdef RGB_LED_POWER
#include "AmbientLightingThread.h"
extern AmbientLightingThread *ambientLightingThread;
#endif
