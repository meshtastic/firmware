#pragma once

#include "configuration.h"

#if HAS_SCREEN && defined(GAT562)
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

class GAT562Arcade : public concurrency::OSThread
{
  public:
    static GAT562Arcade &instance();

    void start();
    void stop();
    bool isActive() const { return active; }
    int handleInputEvent(const InputEvent *event);
    void draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

  protected:
    int32_t runOnce() override;

  private:
    GAT562Arcade();

    bool active = false;
    uint8_t buttons = 0;
    uint8_t heldButtons = 0;
    uint8_t holdFrames = 0;
    uint32_t nextDisplayRefresh = 0;
};

} // namespace graphics
#endif
