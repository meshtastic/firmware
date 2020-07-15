#pragma once

#include "../concurrency/PeriodicTask.h"
#include "PowerStatus.h"

namespace powermanager {

class Power : public concurrency::PeriodicTask
{
   public:

    Observable<const PowerStatus *> newStatus;

    void readPowerStatus();
    void loop();
    virtual bool setup();
    virtual void doTask();
    void setStatusHandler(PowerStatus *handler)
    {
        statusHandler = handler;
    }
    
   protected:
    PowerStatus *statusHandler;
};

extern Power *power;

} // namespace powermanager