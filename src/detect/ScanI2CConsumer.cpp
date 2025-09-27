#include "ScanI2CConsumer.h"
#include <forward_list>

static std::forward_list<ScanI2CConsumer *> ScanI2CConsumers;

ScanI2CConsumer::ScanI2CConsumer()
{
    ScanI2CConsumers.push_front(this);
}

void ScanI2CCompleted(ScanI2C *i2cScanner)
{
    for (ScanI2CConsumer *consumer : ScanI2CConsumers) {
        consumer->i2cScanFinished(i2cScanner);
    }
}