#pragma once

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"
#include "motion/MotionSensor.h"

class AccelerometerThread;

namespace NicheGraphics::InkHUD
{

class StepsApplet : public Applet, private concurrency::OSThread
{
  public:
    static constexpr uint32_t GOALS[] = {5000, 10000, 15000, 20000, 25000, 30000};

    StepsApplet(AccelerometerThread &motion, MotionSensor &sensor);
    void onActivate() override;
    void onDeactivate() override;
    void onForeground() override;
    void onBackground() override;
    void onRender(bool full) override;
    StepsApplet *asStepsApplet() override { return this; }

    uint32_t getGoal() const { return goal; }
    void setGoal(uint32_t newGoal);
    void resetSession();

  private:
    int32_t runOnce() override;

    AccelerometerThread &motion;
    StepTracker &tracker;
    uint32_t goal = 15000;
    uint32_t renderedSteps = 0;
    uint64_t renderedMinutes = 0;
    bool renderedImperial = false;
};

} // namespace NicheGraphics::InkHUD
