#pragma once
#include "concurrency/OSThread.h"
#include "sleep.h"

class WakeKeyInterruptThread : public concurrency::OSThread
{
  public:
    static WakeKeyInterruptThread *instance(void);
    void begin(void);

  protected:
    int32_t runOnce() override;

  private:
    enum class State : uint8_t {
        REST,
        IRQ_PENDING,
        PRESSED,
    };

    static constexpr uint32_t SAMPLE_MS = 15;
    static constexpr uint32_t DEBOUNCE_MS = 25;

    static void IRAM_ATTR isr(void);
    void onInterruptEdge(void);

    void startThread(void);
    void resetStateAndStop(void);

#ifdef ARCH_ESP32
    int onLightSleep(void *);
    int onLightSleepEnd(esp_sleep_wakeup_cause_t cause);

    CallbackObserver<WakeKeyInterruptThread, void *> lsObserver{this, &WakeKeyInterruptThread::onLightSleep};
    CallbackObserver<WakeKeyInterruptThread, esp_sleep_wakeup_cause_t> lsEndObserver{this,
                                                                                     &WakeKeyInterruptThread::onLightSleepEnd};
#endif

    bool sleeping = false;
    volatile bool rawIrqSignaled = false;
    volatile State state = State::REST;
    volatile uint32_t irqAtMs = 0;

    WakeKeyInterruptThread(void);
    static WakeKeyInterruptThread *wakeKey;
    WakeKeyInterruptThread *wakeKeyThread = nullptr;
};
