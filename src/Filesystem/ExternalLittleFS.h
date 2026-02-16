#pragma once

#include <Adafruit_LittleFS.h>
#include <Adafruit_SPIFlash.h>

class ExternalLittleFS : public Adafruit_LittleFS
{
  public:
    static constexpr uint32_t blockSize = 4096;

    ExternalLittleFS();

    bool prepare(Adafruit_SPIFlash *flashDevice);
    bool begin(Adafruit_SPIFlash *flashDevice);

    uint32_t bytesPerCluster() const { return blockSize; }
    uint32_t clusterCount() const { return blockCount; }
    uint32_t freeClusterCount();

  private:
    uint32_t blockCount = 0;
};

extern ExternalLittleFS externalFS;
