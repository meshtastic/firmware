#pragma once
#ifdef ARCH_PORTDUINO

#include "Adafruit_seesaw.h"
#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#define SS_SWITCH 24
#define SS_NEOPIX 6

#define SEESAW_ADDR 0x36

class SeesawRotary : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    const char *_originName;
    bool init();
    SeesawRotary(const char *name);
    int32_t runOnce() override;

  private:
    Adafruit_seesaw ss;
    int32_t encoder_position;
    bool wasPressed = false;
};

extern SeesawRotary *seesawRotary;
#endif