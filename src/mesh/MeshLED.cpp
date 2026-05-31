#include "MeshLED.h"

// Default global instance: no-op dummy. Replace in variant initVariant() for device-specific behaviour.
std::shared_ptr<MeshLED> meshLED = std::make_shared<MeshLED>();
