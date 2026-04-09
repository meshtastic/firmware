#include "AmbientLightingEffects.h"

#include <Arduino.h>

#if defined(TINYLORA_MV_WS2812_EFFECTS)
static volatile uint32_t messageEffectUntil = 0;

void ambientLightingTriggerMessageEffect()
{
    messageEffectUntil = millis() + 2800;
}

bool ambientLightingMessageEffectActive()
{
    return static_cast<int32_t>(messageEffectUntil - millis()) > 0;
}
#else
void ambientLightingTriggerMessageEffect() {}

bool ambientLightingMessageEffectActive()
{
    return false;
}
#endif
