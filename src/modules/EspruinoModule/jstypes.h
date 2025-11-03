/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * Basic variable types
 * ----------------------------------------------------------------------------
 */
#ifndef JSTYPES_H_
#define JSTYPES_H_

#ifndef ESPR_EMBED
#include <stdint.h>
#endif

/// Forward declarations
struct JsVarStruct;
typedef struct JsVarStruct JsVar;

/// Built-in int/float types
typedef int32_t JsVarInt;
typedef uint32_t JsVarIntUnsigned;
#ifdef USE_FLOATS
typedef float JsVarFloat;
#else
typedef double JsVarFloat;
#endif

/// Exception type passed to jsExceptionHere
typedef enum {
  JSET_STRING,
  JSET_ERROR,
  JSET_SYNTAXERROR,
  JSET_TYPEERROR,
  JSET_INTERNALERROR,
  JSET_REFERENCEERROR
} JsExceptionType;


#endif /* JSTYPES_H_ */
