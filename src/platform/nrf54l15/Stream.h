// Stream.h — shim for nRF54L15/Zephyr
// StreamAPI.h and other Meshtastic headers include <Stream.h>.
// Redirect to our Arduino.h shim which defines the Stream base class.
#pragma once
#include "Arduino.h"
