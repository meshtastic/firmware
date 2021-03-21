#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

/*
  TX_LOG      - Time on air this device has transmitted

  RX_LOG      - Time on air used by valid and routable mesh packets, does not include
                TX air time

  RX_ALL_LOG  - Time of all received lora packets. This includes packets that are not
                for meshtastic devices. Does not include TX air time.

  Example analytics:

  TX_LOG + RX_LOG = Total air time for a perticular meshtastic channel.

  TX_LOG + RX_LOG = Total air time for a perticular meshtastic channel, including
                    other lora radios.

  RX_ALL_LOG - RX_LOG = Other lora radios on our frequency channel.
*/
enum reportTypes { TX_LOG, RX_LOG, RX_ALL_LOG };

void logAirtime(reportTypes reportType, uint32_t airtime_ms);

void airtimeRotatePeriod();

uint8_t currentPeriodIndex();
uint8_t getPeriodsToLog();

uint32_t getSecondsSinceBoot();

uint32_t *airtimeReport(reportTypes reportType);

uint32_t getSecondsPerPeriod();

class AirTime : private concurrency::OSThread
{

  public:
    AirTime();

    void logAirtime(reportTypes reportType, uint32_t airtime_ms);

  protected:
    virtual int32_t runOnce() override;
};

extern AirTime *airTime;