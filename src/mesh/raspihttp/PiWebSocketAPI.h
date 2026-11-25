#pragma once
#ifdef ARCH_PORTDUINO
#if __has_include(<ulfius.h>)
#include "ulfius-cfg.h"
#ifndef U_DISABLE_WEBSOCKET

#define HAS_PI_WEBSOCKET_API 1

struct _u_instance;

/// Serve the PhoneAPI at /api/v1/ws: one ToRadio or FromRadio protobuf per binary message.
/// Call before the framework starts, once per listener.
void initPiWebSocketAPI(struct _u_instance *instance);

#endif
#endif
#endif
