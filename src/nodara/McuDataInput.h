#pragma once
#if defined(Nodara)

namespace nodara
{
class McuDataInput
{
  public:
    void setup();
    void loop();

  private:
    void handleCommand03();
    void handleCommand05();
    void handlePowerOnConfirm();
    void processCompletedFrame();
};
} // namespace nodara
#endif
