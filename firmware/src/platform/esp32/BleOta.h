#ifndef BLEOTA_H
#define BLEOTA_H

#include <Arduino.h>
#include <functional>

class BleOta
{
  public:
    explicit BleOta(){};

    static String getOtaAppVersion();
    static bool switchToOtaApp();

  private:
    String mUserAgent;
    static const esp_partition_t *findEspOtaAppPartition();
};

#endif // BLEOTA_H