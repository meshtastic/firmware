#pragma once
#if defined(NODARA)

namespace nodara
{
class McuDataInput
{
  public:
    void setup();
    void loop();

  private:
    void handleCommand05();
    void handlePowerOnConfirm();
    void processCompletedFrame();
};

void setup();
void loop();

/** True while handling external MCU power-cut (0x0005), until System OFF. */
bool isExternalPowerOff();

/** Release the external MCU UART before System OFF. */
void endExternalUart();
} // namespace nodara
#endif
