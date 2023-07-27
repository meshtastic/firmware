#ifndef AMBIENT_LIGHTING_MODULE_H
#define AMBIENT_LIGHTING_MODULE_H

#include "main.h"
#include "mesh/generated/meshtastic/module_config.pb.h"

#ifdef HAS_NCP5623
#include <NCP5623.h>
extern NCP5623 rgb;
#endif

class AmbientLightingModule
{
  public:
    void handleConfig(const meshtastic::ModuleConfig &config);
};

#endif