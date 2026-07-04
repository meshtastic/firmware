#pragma once

#include "InputBroker.h"
#include "OneButton.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#if defined(M5STACK_UNITC6L) || defined(HELTEC_RC32)

class i2cButtonThread : public Observable<const InputEvent *>,
#if defined(HELTEC_RC32)
                        public InputPollable,
#endif
                        public concurrency::OSThread
{
  public:
    const char *_originName;
    explicit i2cButtonThread(const char *name);
    int32_t runOnce() override;
#if defined(HELTEC_RC32)
    bool init();
    void pollOnce() override;

  private:
    static void interruptHandler();
    void powerSensorBus();
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readInput(uint8_t &value);
    void handleTransition(uint8_t newState);

    uint8_t inputState;
    uint32_t lastEventMs = 0;
    bool ready = false;
    bool activeLowPhase = false;

    static i2cButtonThread *instance;
#endif
};

extern i2cButtonThread *i2cButton;
#endif
