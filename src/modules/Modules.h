#pragma once

/**
 * Create module instances here.  If you are adding a new module, you must 'new' it here (or somewhere else)
 * 
 * 
 */
// src/modules/Modules.h
#ifdef USE_WS5500
#include "EthernetModule/EthernetModule.h"
extern EthernetModule ethernetModule; // Non-pointer
#endif
void setupModules();