#include "DeviceInputProvider.h"
#include "input/TCA8418KeyboardBase.h"

std::unique_ptr<TCA8418KeyboardBase> DeviceInputProvider::createTca8418Keyboard()
{
    return nullptr;
}
