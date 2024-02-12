#pragma once
#include "PowerStatus.h"
#include "concurrency/OSThread.h"
#ifdef ARCH_ESP32
#include <esp_adc_cal.h>
#include <soc/adc_channel.h>
#endif

#ifndef NUM_OCV_POINTS
#define NUM_OCV_POINTS 11
#endif

#ifndef OCV_ARRAY
//{4200,4050,3990,3890,3790,3700,3650,3550,3450,3300,3200} //4.2 to 3.2
//{4200,4050,3990,3890,3790,3700,3650,3550,3400,3300,3000} //4.2 to 3.0
//{4150,4050,3990,3890,3790,3690,3620,3520,3420,3300,3100} //4.15 to 3.1
#define OCV_ARRAY {4150,4050,3990,3890,3790,3690,3620,3520,3420,3300,3100}
#endif

#ifndef NUM_CELLS
#define NUM_CELLS 1
#endif

#ifdef BAT_MEASURE_ADC_UNIT
extern RTC_NOINIT_ATTR uint64_t RTC_reg_b;
#include "soc/sens_reg.h" // needed for adc pin reset
#endif

#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO)
#include "modules/Telemetry/Sensor/INA219Sensor.h"
#include "modules/Telemetry/Sensor/INA260Sensor.h"
#include "modules/Telemetry/Sensor/INA3221Sensor.h"
extern INA260Sensor ina260Sensor;
extern INA219Sensor ina219Sensor;
extern INA3221Sensor ina3221Sensor;
#endif

class Power : private concurrency::OSThread
{

  public:
    Observable<const meshtastic::PowerStatus *> newStatus;

    Power();

    void shutdown();
    void readPowerStatus();
    virtual bool setup();
    virtual int32_t runOnce() override;
    void setStatusHandler(meshtastic::PowerStatus *handler) { statusHandler = handler; }
    const uint16_t OCV[11] = OCV_ARRAY;
  protected:
    meshtastic::PowerStatus *statusHandler;

    /// Setup a xpowers chip axp192/axp2101, return true if found
    bool axpChipInit();
    /// Setup a simple ADC input based battery sensor
    bool analogInit();

  private:
    //open circuit voltage lookup table
    uint8_t low_voltage_counter;
#ifdef DEBUG_HEAP
    uint32_t lastheap;
#endif
};

extern Power *power;