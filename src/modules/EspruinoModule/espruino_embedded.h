/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2022 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * Espruino 'embedded' single-file JS interpreter
 * ----------------------------------------------------------------------------
 */

#ifndef ESPRUINO_EMBEDDED_H_
#define ESPRUINO_EMBEDDED_H_

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h> // for va_args
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "jstypes.h"

#ifdef __cplusplus
extern "C" {
#endif

void jsiConsolePrintf(const char *fmt, ...);
uint64_t ejs_get_microseconds();
void ejs_print(const char *str);
struct ejs {
  JsVar *root;
  JsVar *hiddenRoot;
  JsVar *exception;
  unsigned char jsFlags, jsErrorFlags;
};
bool ejs_create(unsigned int varCount);
struct ejs *ejs_create_instance();
void ejs_set_instance(struct ejs *ejs);
void ejs_unset_instance();
struct ejs *ejs_get_active_instance();
void ejs_destroy_instance(struct ejs *ejs);
void ejs_destroy();
JsVar *ejs_exec(struct ejs *ejs, const char *src, bool stringIsStatic);
JsVar *ejs_execf(struct ejs *ejs, JsVar *func, JsVar *thisArg, int argCount, JsVar **argPtr);
void ejs_clear_exception();
size_t jsvGetString(const JsVar *v, char *str, size_t len);
JsVar *jsvAsString(JsVar *v);
size_t jsvGetStringLength(const JsVar *v);
JsVar *jswrap_json_stringify(JsVar *v, JsVar *replacer, JsVar *space);
JsVar *jswrap_json_parse(JsVar *v);
bool jsvIsBoolean(const JsVar *v);
bool jsvIsString(const JsVar *v);
bool jsvIsFunction(const JsVar *v);
bool jsvIsNumeric(const JsVar *v);
bool jsvIsObject(const JsVar *v);
bool jsvIsArray(const JsVar *v);
bool jsvIsNull(const JsVar *v);
JsVar *jsvNewFromString(const char *str);
JsVar *jsvNewFromInteger(JsVarInt value);
JsVar *jsvNewFromBool(bool value);
JsVar *jsvNewFromFloat(JsVarFloat value);
JsVar *jsvNewFromLongInteger(long long value);
JsVar *jsvNewEmptyArray();
JsVar *jsvNewArray(JsVar **elements, int elementCount);
JsVar *jsvObjectGetChild(JsVar *obj, const char *name, unsigned short createChild);
JsVar *jsvLockAgainSafe(JsVar *var);
void jsvUnLock(JsVar *var);
void jsExceptionHere(JsExceptionType type, const char *fmt, ...);
JsVar *ejs_catch_exception();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // ESPRUINO_EMBEDDED_H_
