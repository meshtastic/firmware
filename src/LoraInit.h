#ifndef LORA_INIT_H
#define LORA_INIT_H

#include "LLCC68Interface.h"
#include "LR1110Interface.h"
#include "LR1120Interface.h"
#include "LR1121Interface.h"
#include "NodeDB.h"
#include "RF95Interface.h"
#include "SX1262Interface.h"
#include "SX1268Interface.h"
#include "SX1280Interface.h"
#include "configuration.h"
#include "detect/LoRaRadioType.h"
#include "graphics/Screen.h"
#include <Arduino.h> // hardwareSPI and etc

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#include "platform/portduino/SimRadio.h"
#include "platform/portduino/USBHal.h"
#endif

bool initLoRa();
extern RadioInterface *rIf;
extern RadioLibHal *RadioLibHAL;
extern uint32_t rebootAtMsec;
#endif