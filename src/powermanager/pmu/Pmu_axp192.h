#include "axp20x.h"
#include "Pmu.h"

namespace powermanager {

class Pmu_axp192: public Pmu {
public:
    void init(bool);

    bool isBatteryConnect();

    float getBattVoltage();

    uint8_t getBattPercentage();

    bool isChargeing();

    bool isVBUSPlug();

    bool getHasBattery();

    bool getHasUSB();

    bool status();

    void IRQloop();

    void setIRQ(bool);
private:
    AXP20X_Class _axp;
    bool _irq;
};

} // namespace powermanager