#pragma once
#include "PowerStatus.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#ifdef ARCH_ESP32
// "legacy adc calibration driver is deprecated, please migrate to use esp_adc/adc_cali.h and esp_adc/adc_cali_scheme.h
#include <esp_adc_cal.h>
#include <soc/adc_channel.h>
#endif

#ifndef NUM_OCV_POINTS
#define NUM_OCV_POINTS 11
#endif

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
#define OCV_ARRAY 2700, 2560, 2540, 2520, 2500, 2460, 2420, 2400, 2380, 2320, 1500
#elif defined(TRACKER_T1000_E)
#define OCV_ARRAY 4190, 4042, 3957, 3885, 3820, 3776, 3746, 3725, 3696, 3644, 3100
#elif defined(HELTEC_MESH_POCKET_BATTERY_5000)
#define OCV_ARRAY 4300, 4240, 4120, 4000, 3888, 3800, 3740, 3698, 3655, 3580, 3400
#elif defined(HELTEC_MESH_POCKET_BATTERY_10000)
#define OCV_ARRAY 4100, 4060, 3960, 3840, 3729, 3625, 3550, 3500, 3420, 3345, 3100
#elif defined(SEEED_WIO_TRACKER_L1)
#define OCV_ARRAY 4200, 3876, 3826, 3763, 3713, 3660, 3573, 3485, 3422, 3359, 3300
#elif defined(SEEED_SOLAR_NODE)
#define OCV_ARRAY 4200, 3986, 3922, 3812, 3734, 3645, 3527, 3420, 3281, 3087, 2786
#elif defined(R1_NEO)
#define OCV_ARRAY 4330, 4292, 4254, 4216, 4178, 4140, 4102, 4064, 4026, 3988, 3950
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

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#include "modules/Telemetry/Sensor/nullSensor.h"
#if __has_include(<Adafruit_INA219.h>)
#include "modules/Telemetry/Sensor/INA219Sensor.h"
extern INA219Sensor ina219Sensor;
#else
extern NullSensor ina219Sensor;
#endif

#if __has_include(<INA226.h>)
#include "modules/Telemetry/Sensor/INA226Sensor.h"
extern INA226Sensor ina226Sensor;
#else
extern NullSensor ina226Sensor;
#endif

#if __has_include(<Adafruit_INA260.h>)
#include "modules/Telemetry/Sensor/INA260Sensor.h"
extern INA260Sensor ina260Sensor;
#else
extern NullSensor ina260Sensor;
#endif

#if __has_include(<INA3221.h>)
#include "modules/Telemetry/Sensor/INA3221Sensor.h"
extern INA3221Sensor ina3221Sensor;
#else
extern NullSensor ina3221Sensor;
#endif

#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#if __has_include(<Adafruit_MAX1704X.h>)
#include "modules/Telemetry/Sensor/MAX17048Sensor.h"
extern MAX17048Sensor max17048Sensor;
#else
extern NullSensor max17048Sensor;
#endif
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && HAS_RAKPROT
#include "modules/Telemetry/Sensor/RAK9154Sensor.h"
extern RAK9154Sensor rak9154Sensor;
#endif

#ifdef HAS_PMU
#include "XPowersAXP192.tpp"
#include "XPowersAXP2101.tpp"
#include "XPowersLibInterface.hpp"
extern XPowersLibInterface *PMU;
#endif

class Power : private concurrency::OSThread
{

  public:
    Observable<const meshtastic::PowerStatus *> newStatus;

    Power();

    void powerCommandsCheck();
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
    /// Setup a Lipo battery level sensor
    bool lipoInit();
    /// Setup a Lipo charger
    bool lipoChargerInit();
    /// Setup a meshSolar battery sensor
    bool meshSolarInit();

  private:
    void shutdown();
    void reboot();
    // open circuit voltage lookup table
    uint8_t low_voltage_counter;
    int32_t lastLogTime = 0;
#ifdef DEBUG_HEAP
    uint32_t lastheap;
#endif
};

void battery_adcEnable();

extern Power *power;
