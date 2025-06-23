#include "configuration.h"
namespace graphics
{

class menuHandler
{
  public:
    enum screenMenus { menu_none, lora_picker, TZ_picker, twelve_hour_picker, clock_menu };
    static screenMenus menuQueue;

    static void LoraRegionPicker(uint32_t duration = 30000);
    static void handleMenuSwitch();
    static void clockMenu();
    static void TZPicker();
    static void TwelveHourPicker();
    static void messageResponseMenu();
};

} // namespace graphics