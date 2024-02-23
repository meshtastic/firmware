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

// 3400,3350,3320,3290,3270,3260,3250,3230,3200,3120,3000 //3.4 to 3.0 LiFePO4
// 2120,2090,2070,2050,2030,2010,1990,1980,1970,1960,1950 //2.12 to 1.95 Lead Acid
// 4200,4050,3990,3890,3790,3700,3650,3550,3450,3300,3200 //4.2 to 3.2 LiIon/LiPo
// 4200,4050,3990,3890,3790,3700,3650,3550,3400,3300,3000 //4.2 to 3.0 LiIon/LiPo
// 4150,4050,3990,3890,3790,3690,3620,3520,3420,3300,3100 //4.15 to 3.1 LiIon/LiPo
// 2770,2650,2540,2420,2300,2180,2060,1940,1800,1680,1550 //2.8 to 1.5 Lithium Titanate

#ifndef OCV_ARRAY
#ifdef CELL_TYPE_LIFEPO4
#define OCV_ARRAY 3400, 3350, 3320, 3290, 3270, 3260, 3250, 3230, 3200, 3120, 3000
#elif defined(CELL_TYPE_LEADACID)
#define OCV_ARRAY 2120, 2090, 2070, 2050, 2030, 2010, 1990, 1980, 1970, 1960, 1950
#elif defined(CELL_TYPE_ALKALINE)
#define OCV_ARRAY 1580, 1400, 1350, 1300, 1280, 1250, 1230, 1190, 1150, 1100, 1000
#elif defined(CELL_TYPE_NIMH)
#define OCV_ARRAY 1400, 1300, 1280, 1270, 1260, 1250, 1240, 1230, 1210, 1150, 1000
#elif defined(CELL_TYPE_LTO)
#define OCV_ARRAY 2770, 2650, 2540, 2420, 2300, 2180, 2060, 1940, 1800, 1680, 1550
#else // LiIon
#define OCV_ARRAY 4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100
#endif
#endif

/*Note: 12V lead acid is 6 cells, most board accept only 1 cell LiIon/LiPo*/
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
    const uint16_t OCV[11] = {OCV_ARRAY};

  protected:
    meshtastic::PowerStatus *statusHandler;

    /// Setup a xpowers chip axp192/axp2101, return true if found
    bool axpChipInit();
    /// Setup a simple ADC input based battery sensor
    bool analogInit();

  private:
    // open circuit voltage lookup table
    uint8_t low_voltage_counter;
#ifdef DEBUG_HEAP
    uint32_t lastheap;
#endif
};

extern Power *power;