#include "kbUsbImpl.h"
#include "InputBroker.h"

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2

KbUsbImpl *kbUsbImpl;

KbUsbImpl::KbUsbImpl() : KbUsbBase("usbKB") {}

void KbUsbImpl::init()
{
    inputBroker->registerSource(this);
}

#endif // INPUTBROKER_MATRIX_TYPE