#pragma once

#if defined(Nodara)

#include "input/InputBroker.h"
#include "concurrency/OSThread.h"

#define BTN_NUM_MAX (6)

class SixKeyNavInput : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    const char *_originName;

    SixKeyNavInput();
    bool init();
    int32_t runOnce() override;

    void fireEvent(input_broker_event evt);
    void fireKbEvent(uint8_t kbchar);

    void captureSnapshotFromISR();

  private:
    static const uint32_t DEBOUNCE_MS = 20;
    uint32_t lastTime;

    struct {
      bool press;
      bool release;
    } btn_state[BTN_NUM_MAX];
};

extern SixKeyNavInput *sixKeyNavInput;

#endif // Nodara
