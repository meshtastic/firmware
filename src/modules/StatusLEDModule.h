#pragma once

#include "BluetoothStatus.h"
#include "MeshModule.h"
#include "PowerStatus.h"
#include "concurrency/LockGuard.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "mesh/TxAckStatus.h"
#include <Arduino.h>
#include <functional>

#if !MESHTASTIC_EXCLUDE_INPUTBROKER
#include "input/InputBroker.h"
#endif

// WS2812/NeoPixel status-LED support. A variant may define
//   NEOPIXEL_STATUS_POWER_PIN   (required to enable the power/charge pixel)
//   NEOPIXEL_STATUS_POWER_COLOR (optional, default red 0xFF0000)
//   NEOPIXEL_STATUS_PAIRING_PIN / _COLOR  (default blue 0x0000FF)
// Each pixel is a standalone 1-LED strand on its own GPIO - this mirrors how
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
#ifdef LED_LORA
    int handleLoRaRx(uint32_t sender);
#endif
#ifdef LED_TX_ACK
    int handleTxAckStatus(const TxAckEvent *event);
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
#ifdef LED_LORA
    CallbackObserver<StatusLEDModule, uint32_t> loraRxObserver =
        CallbackObserver<StatusLEDModule, uint32_t>(this, &StatusLEDModule::handleLoRaRx);
#endif
#ifdef LED_TX_ACK
    CallbackObserver<StatusLEDModule, const TxAckEvent *> txAckObserver =
        CallbackObserver<StatusLEDModule, const TxAckEvent *>(this, &StatusLEDModule::handleTxAckStatus);
#endif

  private:
    bool CHARGE_LED_state = LED_STATE_OFF;
    bool PAIRING_LED_state = LED_STATE_OFF;
#if defined(LED_HEARTBEAT)
    bool HEARTBEAT_LED_state = LED_STATE_OFF;
#endif

    uint32_t PAIRING_LED_starttime = 0;
    uint32_t lastUserbuttonTime = 0;
    uint32_t POWER_LED_starttime = 0;
    bool doing_fast_blink = false;
#ifdef LED_LORA
    static constexpr uint32_t LORA_RX_LED_FLASH_MS = 100;
    bool LORA_LED_state = LED_STATE_OFF;
    uint32_t LORA_LED_starttime = 0;
#endif
#ifdef LED_TX_ACK
    static constexpr uint32_t TX_ACK_FAIL_FLASH_MS = 120;
    static constexpr uint8_t TX_ACK_FAIL_PHASES = 6; // three on/off pairs
    bool txAckWaiting = false;                       // a reliable send of ours is still unresolved
    uint8_t txAckFailPhases = 0;                     // on/off phases of the failure flash left to play
    uint32_t txAckFailPhaseStart = 0;
    /// Guards the three fields above. nRF52 publishes TX state from the BLE task as well as the
    /// loop task (see Router::deferredLock), and runOnce() read-modify-writes the phase counter.
    concurrency::Lock txAckLock;
#endif

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
