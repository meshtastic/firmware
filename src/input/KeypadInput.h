#pragma once

#include "InputBroker.h"

/**
 * @brief Polls a six-key keypad through a hardware-independent callback, debouncing presses and repeating directional actions.
 * The callback returns success and fills a bitmask of pressed Key values; actions are emitted through InputBroker.
 */
class KeypadInput : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    enum Key : uint8_t { UP = 1U << 0, DOWN = 1U << 1, LEFT = 1U << 2, RIGHT = 1U << 3, CONFIRM = 1U << 4, CANCEL = 1U << 5 };
    using ReadState = bool (*)(uint8_t &pressedKeys);

    // Create a keypad input source using the supplied state-reader callback.
    explicit KeypadInput(ReadState readState);
    // Read the initial key state and register with InputBroker; return false on failure.
    bool init();

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint8_t KEY_COUNT = 6;
    static constexpr uint8_t KEY_MASK = UP | DOWN | LEFT | RIGHT | CONFIRM | CANCEL;
    static constexpr uint32_t POLL_INTERVAL_MS = 10;
    static constexpr uint32_t DEBOUNCE_MS = 25;
    static constexpr uint32_t REPEAT_DELAY_MS = 400;
    static constexpr uint32_t REPEAT_INTERVAL_MS = 150;

    void emitEvent(uint8_t keyIndex);

    ReadState readState;
    bool ready = false;
    uint8_t sampledKeys = 0;
    uint8_t stableKeys = 0;
    uint8_t repeatingKeys = 0;
    uint32_t changedAtMs[KEY_COUNT] = {};
    uint32_t repeatAtMs[4] = {};
};