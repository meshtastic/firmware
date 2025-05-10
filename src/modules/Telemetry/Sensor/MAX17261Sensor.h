#pragma once

#ifndef MAX17048_SENSOR_H
#define MAX17048_SENSOR_H

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_I2C && !defined(ARCH_STM32WL) && __has_include(<Adafruit_MAX1704X.h>)

// Samples to store in a buffer to determine if the battery is charging or discharging
#define MAX17048_CHARGING_SAMPLES 3

// Threshold to determine if the battery is on charge, in percent/hour
#define MAX17048_CHARGING_MINIMUM_RATE 1.0f

// Threshold to determine if the board has bus power
#define MAX17048_BUS_POWER_VOLTS 4.195f

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"

#include "meshUtils.h"
#include <Adafruit_MAX1704X.h>
#include <queue>

struct MAX17048ChargeSample {
    float cellPercent;
    float chargeRate;
};

enum MAX17048ChargeState { IDLE, EXPORT, IMPORT };

// Singleton wrapper for the Adafruit_MAX17048 class
class MAX17048Singleton : public Adafruit_MAX17048
{
  private:
    static MAX17048Singleton *pinstance;
    bool initialized = false;
    std::queue<MAX17048ChargeSample> chargeSamples;
    MAX17048ChargeState chargeState = IDLE;
    const String chargeLabels[3] = {F("idle"), F("export"), F("import")};
    const char *sensorStr = "MAX17048Sensor";

  protected:
    MAX17048Singleton();
    ~MAX17048Singleton();

  public:
    // Create a singleton instance (not thread safe)
    static MAX17048Singleton *GetInstance();

    // Singletons should not be cloneable.
    MAX17048Singleton(MAX17048Singleton &other) = delete;

    // Singletons should not be assignable.
    void operator=(const MAX17048Singleton &) = delete;

    // Initialise the sensor (not thread safe)
    virtual bool runOnce(TwoWire *theWire = &Wire);

    // Get the current bus voltage
    uint16_t getBusVoltageMv();

    // Get the state of charge in percent 0 to 100
    uint8_t getBusBatteryPercent();

    // Calculate the seconds to charge/discharge
    uint16_t getTimeToGoSecs();

    // Returns true if the battery sensor has started
    inline virtual bool isInitialised() { return initialized; };

    // Returns true if the battery is currently on charge (not thread safe)
    bool isBatteryCharging();

    // Returns true if a battery is actually connected
    bool isBatteryConnected();

    // Returns true if there is bus or external power connected
    bool isExternallyPowered();
};

#if (HAS_TELEMETRY && (!MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY))

class MAX17048Sensor : public TelemetrySensor, VoltageSensor
{
  private:
    MAX17048Singleton *max17048 = nullptr;

  protected:
    virtual void setup() override;

  public:
    MAX17048Sensor();

    // Initialise the sensor
    virtual int32_t runOnce() override;

    // Get the current bus voltage and state of charge
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;

    // Get the current bus voltage
    virtual uint16_t getBusVoltageMv() override;
};

#endif

#endif

#endif